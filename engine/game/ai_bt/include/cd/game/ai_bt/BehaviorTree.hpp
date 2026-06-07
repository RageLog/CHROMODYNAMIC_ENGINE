// =============================================================================
// CHROMODYNAMIC — cd/game/ai_bt/BehaviorTree.hpp
// Phase 473 — cd::game::ai_bt (G2.2: Behavior Tree)
//
// A small, allocation-light Behavior Tree built around the canonical
// formulation popularised by Champandard ("Behavior Trees for Next-Gen Game
// AI", AIGameDev / GDC 2008) and refined by Marzinotto et al. ("Towards a
// Unified Behavior Trees Framework for Robot Control", ICRA 2014).
//
// Topology:
//   * Composite : SequenceNode, SelectorNode, ParallelNode.
//   * Decorator : InverterNode, RepeaterNode, UntilSuccessNode.
//   * Leaf      : LeafNode<F> wrapping any callable convertible to
//                 `Status(Blackboard&)`. Action / Condition leaves share the
//                 same machinery — a condition just never returns kRunning.
//
// Ticking model:
//   * `Status tick(Blackboard&)` is the universal contract.
//   * Composites tick their children in order, short-circuiting on the first
//     determining outcome (Sequence stops on failure, Selector on success).
//   * Parallel runs every child every tick and resolves the aggregate via a
//     simple success/failure threshold (Colledanchise & Ögren, "Behavior
//     Trees in Robotics and AI", 2018, §2.3).
//
// Blackboard:
//   * A typed key-value store keyed by `std::string` whose value is a
//     `std::variant<bool,int,float,std::string>`. Tree leaves read / write
//     gameplay scratch values through it so the tree itself stays
//     stateless (mirrors Unreal `UBlackboardComponent`, Godot `Blackboard`
//     resource, and Behavior Designer's `SharedVariable` pattern).
//
// Allocation:
//   * Children are owned via `std::unique_ptr<Node>` (CLAUDE.md §1). Tree
//     construction is a one-time setup cost; ticking touches no heap.
//
// Threading:
//   * Not thread-safe; treat a `BehaviorTree` like an entity's controller
//     and tick it from the owning thread.
//
// Dependencies (CLAUDE.md §7): cd::core only at the public header level.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cd::game::ai_bt
{

// -----------------------------------------------------------------------------
// Status — universal return type for `Node::tick`.
//
// `kRunning` is what makes a BT a BT rather than a decision tree: a leaf may
// span many frames before reporting success or failure (think "walk-to" or
// "play-animation"). Parents propagate `kRunning` upward so the next tick
// re-enters the same in-progress branch.
// -----------------------------------------------------------------------------
enum class Status : std::uint8_t
{
    kRunning = 0,
    kSuccess = 1,
    kFailure = 2,
};

// -----------------------------------------------------------------------------
// Blackboard — string-keyed, variant-valued scratch store.
//
// The variant alternatives cover the four shapes every gameplay BT needs in
// practice: predicates (bool), counters / handles (int), magnitudes (float),
// and identifiers (string). Vector / object payloads belong on the entity,
// not in the blackboard — the BT references them by id.
// -----------------------------------------------------------------------------
class Blackboard
{
public:
    using Value = std::variant<bool, int, float, std::string>;

    Blackboard() = default;

    // ------- set ----------------------------------------------------------
    void set_bool  (const std::string& key, bool         v) { values_[key] = v; }
    void set_int   (const std::string& key, int          v) { values_[key] = v; }
    void set_float (const std::string& key, float        v) { values_[key] = v; }
    void set_string(const std::string& key, std::string  v) { values_[key] = std::move(v); }

    /// Type-erased setter for callers that already hold a Value.
    void set(const std::string& key, Value v) { values_[key] = std::move(v); }

    // ------- has / erase --------------------------------------------------
    [[nodiscard]] bool has(const std::string& key) const noexcept
    {
        return values_.contains(key);
    }
    bool erase(const std::string& key) { return values_.erase(key) != 0U; }

    // ------- get (typed, with default fallback) --------------------------
    // Returns the stored value if present and of the requested type;
    // otherwise returns the supplied default. Never throws — callers can
    // rely on a deterministic value in tick code.
    [[nodiscard]] bool get_bool(const std::string& key, bool fallback = false) const noexcept
    {
        const auto* p = find_typed<bool>(key);
        return p != nullptr ? *p : fallback;
    }
    [[nodiscard]] int get_int(const std::string& key, int fallback = 0) const noexcept
    {
        const auto* p = find_typed<int>(key);
        return p != nullptr ? *p : fallback;
    }
    [[nodiscard]] float get_float(const std::string& key, float fallback = 0.0F) const noexcept
    {
        const auto* p = find_typed<float>(key);
        return p != nullptr ? *p : fallback;
    }
    [[nodiscard]] std::string get_string(const std::string& key,
                                         const std::string& fallback = std::string{}) const
    {
        const auto* p = find_typed<std::string>(key);
        return p != nullptr ? *p : fallback;
    }

    /// Raw variant access; returns nullptr if absent.
    [[nodiscard]] const Value* find(const std::string& key) const noexcept
    {
        auto it = values_.find(key);
        return it == values_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    void clear() noexcept { values_.clear(); }

private:
    template <typename T>
    [[nodiscard]] const T* find_typed(const std::string& key) const noexcept
    {
        const auto* v = find(key);
        if (v == nullptr) { return nullptr; }
        return std::get_if<T>(v);
    }

    std::unordered_map<std::string, Value> values_ {};
};

// -----------------------------------------------------------------------------
// Node — abstract base.
//
// Subclasses override `tick(Blackboard&)`. `reset()` is invoked by composite
// parents when a sibling branch wins, so any in-flight leaf state must be
// cleared. Default implementation is a no-op (leaves with no state need not
// override).
// -----------------------------------------------------------------------------
class Node
{
public:
    Node() = default;
    virtual ~Node() = default;

    Node(const Node&)            = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&)                 = delete;
    Node& operator=(Node&&)      = delete;

    [[nodiscard]] virtual Status tick(Blackboard& bb) = 0;

    virtual void reset() noexcept {}
};

// -----------------------------------------------------------------------------
// LeafNode — adapt any `Status(Blackboard&)`-callable into a Node.
//
// Templated so the captured lambda is stored without indirection on the
// fast path (one virtual call to tick, then a direct call to the lambda).
// A non-template `make_leaf` factory below hides the template parameter for
// 99% of call sites.
// -----------------------------------------------------------------------------
template <typename F>
class LeafNode final : public Node
{
public:
    explicit LeafNode(F fn) : fn_(std::move(fn)) {}

    [[nodiscard]] Status tick(Blackboard& bb) override { return fn_(bb); }

private:
    F fn_;
};

template <typename F>
[[nodiscard]] std::unique_ptr<Node> make_leaf(F fn)
{
    return std::make_unique<LeafNode<F>>(std::move(fn));
}

// -----------------------------------------------------------------------------
// Composite base — owns an ordered list of children.
//
// Subclasses inherit `add_child()` and the child container, then implement
// `tick()` per their selection policy. `reset()` recursively resets every
// child so re-entry starts from a clean state.
// -----------------------------------------------------------------------------
class CompositeNode : public Node
{
public:
    CompositeNode() = default;

    void add_child(std::unique_ptr<Node> child)
    {
        children_.push_back(std::move(child));
    }

    [[nodiscard]] std::size_t child_count() const noexcept { return children_.size(); }

    /// Non-owning accessor for graph traversal (e.g. editor panels).
    /// Returns nullptr when `index` is out of range.
    [[nodiscard]] const Node* child_at(std::size_t index) const noexcept
    {
        return (index < children_.size()) ? children_[index].get() : nullptr;
    }

    void reset() noexcept override
    {
        for (auto& c : children_) { c->reset(); }
    }

protected:
    std::vector<std::unique_ptr<Node>> children_ {};
};

// -----------------------------------------------------------------------------
// SequenceNode — AND.
//
// Returns kSuccess only if every child succeeds (in order). Stops on the
// first failure (returns kFailure) or first running child (returns
// kRunning). Resumes from the running child on the next tick — children
// before it are NOT re-ticked, matching Champandard's "memory sequence".
// -----------------------------------------------------------------------------
class SequenceNode final : public CompositeNode
{
public:
    [[nodiscard]] Status tick(Blackboard& bb) override;
    void reset() noexcept override
    {
        cursor_ = 0;
        CompositeNode::reset();
    }

private:
    std::size_t cursor_ { 0 };
};

// -----------------------------------------------------------------------------
// SelectorNode — OR.
//
// Returns kSuccess on the first child that succeeds. Returns kFailure only
// if every child fails. Like SequenceNode, resumes from the running child
// on the next tick.
// -----------------------------------------------------------------------------
class SelectorNode final : public CompositeNode
{
public:
    [[nodiscard]] Status tick(Blackboard& bb) override;
    void reset() noexcept override
    {
        cursor_ = 0;
        CompositeNode::reset();
    }

private:
    std::size_t cursor_ { 0 };
};

// -----------------------------------------------------------------------------
// ParallelNode — runs ALL children every tick.
//
// Returns kSuccess once at least `success_threshold` children have reported
// kSuccess; returns kFailure once `failure_threshold` have failed;
// kRunning otherwise. The thresholds default to (N, 1) which is the
// "succeed when all succeed, fail on first failure" policy (a.k.a.
// "parallel-AND") used by most production engines.
//
// Note: this is the simple per-tick aggregator. The richer "running
// children keep running while others finish" semantics from BehaviorTree.CPP
// can be layered on top by callers; we keep the surface small and
// orthogonal.
// -----------------------------------------------------------------------------
class ParallelNode final : public CompositeNode
{
public:
    explicit ParallelNode(std::size_t success_threshold = 0,
                          std::size_t failure_threshold = 1) noexcept
        : success_threshold_(success_threshold)
        , failure_threshold_(failure_threshold)
    {}

    [[nodiscard]] Status tick(Blackboard& bb) override;

    // 0 in success_threshold_ means "all children" — resolved at tick time.
    [[nodiscard]] std::size_t success_threshold() const noexcept { return success_threshold_; }
    [[nodiscard]] std::size_t failure_threshold() const noexcept { return failure_threshold_; }

private:
    std::size_t success_threshold_ { 0 };
    std::size_t failure_threshold_ { 1 };
};

// -----------------------------------------------------------------------------
// Decorator base — single-child wrapper.
// -----------------------------------------------------------------------------
class DecoratorNode : public Node
{
public:
    explicit DecoratorNode(std::unique_ptr<Node> child) : child_(std::move(child)) {}

    /// Non-owning accessor for graph traversal (e.g. editor panels).
    /// Returns nullptr when no child has been set.
    [[nodiscard]] const Node* child() const noexcept { return child_.get(); }

    void reset() noexcept override
    {
        if (child_) { child_->reset(); }
    }

protected:
    std::unique_ptr<Node> child_;
};

// -----------------------------------------------------------------------------
// InverterNode — flips kSuccess ↔ kFailure; passes kRunning through.
// -----------------------------------------------------------------------------
class InverterNode final : public DecoratorNode
{
public:
    using DecoratorNode::DecoratorNode;
    [[nodiscard]] Status tick(Blackboard& bb) override;
};

// -----------------------------------------------------------------------------
// RepeaterNode — tick the child up to N times, propagating the LAST result.
//
// `count == 0` means "repeat forever" (the child must terminate by some
// outside influence, e.g. a parent decorator with a time budget). Returns
// kRunning while the loop is still in progress, kSuccess / kFailure with
// the child's final result when the loop completes.
// -----------------------------------------------------------------------------
class RepeaterNode final : public DecoratorNode
{
public:
    explicit RepeaterNode(std::unique_ptr<Node> child, std::size_t count = 1) noexcept
        : DecoratorNode(std::move(child))
        , count_(count)
    {}

    [[nodiscard]] Status tick(Blackboard& bb) override;

    void reset() noexcept override
    {
        iter_ = 0;
        last_ = Status::kFailure;
        DecoratorNode::reset();
    }

    [[nodiscard]] std::size_t target_count() const noexcept { return count_; }
    [[nodiscard]] std::size_t current_iter() const noexcept { return iter_; }

private:
    std::size_t count_ { 1 };
    std::size_t iter_  { 0 };
    Status      last_  { Status::kFailure };
};

// -----------------------------------------------------------------------------
// UntilSuccessNode — re-tick the child until it returns kSuccess.
//
// Returns kRunning while the child is failing (so the tree can do other
// work), and kSuccess the moment the child succeeds. Useful for "retry"
// style patterns ("walk to point, retrying if pathfinding fails").
// -----------------------------------------------------------------------------
class UntilSuccessNode final : public DecoratorNode
{
public:
    using DecoratorNode::DecoratorNode;
    [[nodiscard]] Status tick(Blackboard& bb) override;
};

// -----------------------------------------------------------------------------
// BehaviorTree — owns the root node and exposes a single tick entry point.
//
// `dt` is forwarded for callers that want a time-aware blackboard slot;
// the tree itself does not interpret it. Storing it in the blackboard
// (under e.g. "dt") is the conventional way to hand it to leaves.
// -----------------------------------------------------------------------------
class BehaviorTree
{
public:
    BehaviorTree() = default;
    explicit BehaviorTree(std::unique_ptr<Node> root) noexcept : root_(std::move(root)) {}

    void set_root(std::unique_ptr<Node> root) noexcept { root_ = std::move(root); }
    [[nodiscard]] Node* root() const noexcept { return root_.get(); }

    /// Tick the tree once. `dt` is published into the blackboard under the
    /// well-known key `"dt"` (overwritten every call) so leaves that need
    /// it can read it without bespoke wiring. Returns kFailure if no root
    /// is set.
    [[nodiscard]] Status tick(Blackboard& bb, float dt = 0.0F);

    /// Recursively reset the tree to its initial state (drops cursors in
    /// composites, counters in decorators, etc.).
    void reset() noexcept
    {
        if (root_) { root_->reset(); }
    }

private:
    std::unique_ptr<Node> root_ {};
};

// -----------------------------------------------------------------------------
// NodeKind — coarse classification used by tooling (editor panels, loggers).
//
// The enum deliberately does not expose Parallel separately from other
// composites: visual tools colour composites uniformly unless they choose to
// dynamic_cast<ParallelNode*> themselves.
// -----------------------------------------------------------------------------
enum class NodeKind : std::uint8_t
{
    kSequence  = 0,  ///< SequenceNode (AND-composite)
    kSelector  = 1,  ///< SelectorNode (OR-composite)
    kParallel  = 2,  ///< ParallelNode (all-children composite)
    kDecorator = 3,  ///< Any DecoratorNode subclass
    kLeaf      = 4,  ///< Any leaf / action / condition node
};

/// Classify a node pointer by dynamic type.
/// Returns kLeaf for nullptr so callers don't need to null-check separately.
[[nodiscard]] inline NodeKind node_kind(const Node* node) noexcept
{
    if (node == nullptr) { return NodeKind::kLeaf; }
    if (dynamic_cast<const SequenceNode*>(node) != nullptr) { return NodeKind::kSequence; }
    if (dynamic_cast<const SelectorNode*>(node) != nullptr) { return NodeKind::kSelector; }
    if (dynamic_cast<const ParallelNode*>(node) != nullptr) { return NodeKind::kParallel; }
    if (dynamic_cast<const DecoratorNode*>(node) != nullptr) { return NodeKind::kDecorator; }
    return NodeKind::kLeaf;
}

/// Collect non-owning child pointers of a node.
/// Composites emit child_count() pointers; decorators emit 1 (or 0 if null);
/// leaves emit nothing.
[[nodiscard]] inline std::vector<const Node*> node_children(const Node* node)
{
    if (node == nullptr) { return {}; }
    if (const auto* c = dynamic_cast<const CompositeNode*>(node))
    {
        std::vector<const Node*> out;
        out.reserve(c->child_count());
        for (std::size_t i = 0; i < c->child_count(); ++i)
        {
            out.push_back(c->child_at(i));
        }
        return out;
    }
    if (const auto* d = dynamic_cast<const DecoratorNode*>(node))
    {
        if (d->child() != nullptr) { return { d->child() }; }
        return {};
    }
    return {};
}

}  // namespace cd::game::ai_bt
