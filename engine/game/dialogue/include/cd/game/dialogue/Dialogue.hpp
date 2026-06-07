// =============================================================================
// CHROMODYNAMIC - cd/game/dialogue/Dialogue.hpp
// Phase 484 - cd::game::dialogue (G4.1: Branching Dialogue VM)
//
// A small, allocation-light branching dialogue virtual machine. The contract
// mirrors the canonical narrative-tool shape popularised by Ink (inkle), Yarn
// Spinner (Secret Lab) and the Twine/Harlowe family:
//
//   * A dialogue is a directed graph of DialogueNode objects keyed by a
//     stable string id ("intro", "shopkeep_greeting", "endA").
//   * Each node carries a speaker tag, a body of text, and zero or more
//     DialogueChoice options. A node with zero choices is terminal.
//   * Each choice carries its own id (unique within the owning node), the
//     prompt text shown to the player, an optional Condition predicate that
//     decides whether the choice is visible, and the id of the next node.
//
// The DialogueVM is the runtime that walks that graph: load the tree, query
// the current node, ask which choices are currently available given the
// player's blackboard state, then commit a single choice to advance.
//
// Why a typed Blackboard at all?  Conditional choices need access to game
// state (quest flags, inventory, relationship scores, ...). Rather than a
// raw `std::function<bool()>` -- which couples every Condition to whatever
// closure-capture the caller happened to set up -- we adopt the same shape
// cd::game::ai_bt uses: a string-keyed variant store. This keeps Condition
// callables pure-of-side-effect (they receive a `const Blackboard&`), keeps
// the dialogue tree fully data-describable, and means a serialised tree
// round-trips even when its predicates are recreated from text.
//
// Tree authoring: trees are normally authored as `DialogueNode` aggregates
// in code, then handed to `DialogueVM::load_tree`. For data-driven flows
// the companion `parse_tree` helper accepts a tiny line-oriented DSL --
// see Dialogue.cpp for the grammar. The DSL deliberately has no condition
// syntax (predicates are code, not strings); callers wire predicates by
// running over the parsed tree and assigning `DialogueChoice::condition`
// for the choices that need it.
//
// Threading: a DialogueVM owns its tree + state. Not thread-safe -- treat
// it like an entity controller and tick from the owning thread.
//
// Dependencies (CLAUDE.md S7): cd::core only at the public header level.
//
// Design references:
//   * Ingold, Joseph. "Ink: A Narrative Scripting Language for Games",
//     inkle Ltd. (https://www.inklestudios.com/ink/), accessed 2026.
//   * Secret Lab. "Yarn Spinner Specification" v2.4
//     (https://docs.yarnspinner.dev/getting-started/writing-in-yarn),
//     accessed 2026.
//   * Mateas & Stern. "Facade: An Experiment in Building a Fully-Realized
//     Interactive Drama." Game Developers Conference, 2003.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cd::game::dialogue
{

// -----------------------------------------------------------------------------
// Blackboard - string-keyed typed scratch store consulted by Condition
// predicates. The variant alternatives match cd::game::ai_bt's blackboard
// shape on purpose: predicates / counters / magnitudes / identifiers cover
// every gameplay-relevant predicate without dragging structured payloads
// into the dialogue tree.
// -----------------------------------------------------------------------------
class Blackboard
{
public:
    using Value = std::variant<bool, int, float, std::string>;

    Blackboard() = default;

    // ------- set ----------------------------------------------------------
    void set_bool  (const std::string& key, bool        v) { values_[key] = v; }
    void set_int   (const std::string& key, int         v) { values_[key] = v; }
    void set_float (const std::string& key, float       v) { values_[key] = v; }
    void set_string(const std::string& key, std::string v) { values_[key] = std::move(v); }

    /// Type-erased setter for callers that already hold a Value.
    void set(const std::string& key, Value v) { values_[key] = std::move(v); }

    // ------- query --------------------------------------------------------
    CD_NODISCARD bool has(const std::string& key) const noexcept
    {
        return values_.find(key) != values_.end();
    }

    CD_NODISCARD bool get_bool(const std::string& key, bool fallback = false) const noexcept
    {
        const auto it = values_.find(key);
        if (it == values_.end())
        {
            return fallback;
        }
        const bool* p = std::get_if<bool>(&it->second);
        return p != nullptr ? *p : fallback;
    }

    CD_NODISCARD int get_int(const std::string& key, int fallback = 0) const noexcept
    {
        const auto it = values_.find(key);
        if (it == values_.end())
        {
            return fallback;
        }
        const int* p = std::get_if<int>(&it->second);
        return p != nullptr ? *p : fallback;
    }

    CD_NODISCARD float get_float(const std::string& key, float fallback = 0.0F) const noexcept
    {
        const auto it = values_.find(key);
        if (it == values_.end())
        {
            return fallback;
        }
        const float* p = std::get_if<float>(&it->second);
        return p != nullptr ? *p : fallback;
    }

    CD_NODISCARD std::string get_string(const std::string& key,
                                        std::string        fallback = {}) const
    {
        const auto it = values_.find(key);
        if (it == values_.end())
        {
            return fallback;
        }
        // Branch around the ternary so the move actually fires for the
        // fallback path (the ternary's other branch returns a *const ref*,
        // forcing the deduced type to const& and silently disabling the
        // move on the moved branch).
        const auto* p = std::get_if<std::string>(&it->second);
        if (p != nullptr)
            return *p;
        return fallback;
    }

    void erase(const std::string& key) { values_.erase(key); }

    void clear() noexcept { values_.clear(); }

    CD_NODISCARD std::size_t size() const noexcept { return values_.size(); }

private:
    std::unordered_map<std::string, Value> values_ {};
};

// -----------------------------------------------------------------------------
// Condition - predicate evaluated against the player's Blackboard to decide
// whether a DialogueChoice is currently visible. An empty (default-constructed)
// Condition is treated as "always visible" so unconditional choices stay
// data-only.
// -----------------------------------------------------------------------------
using Condition = std::function<bool(const Blackboard&)>;

// -----------------------------------------------------------------------------
// DialogueChoice - one option presented at a DialogueNode.
//
// Fields:
//   * id        : stable id, unique within the owning node. Callers pass this
//                 back to `DialogueVM::select_choice` to commit the choice.
//                 Empty id is reserved and rejected by load_tree.
//   * text      : prompt text shown to the player ("Ask about the rumour").
//   * condition : optional predicate; when empty the choice is always shown.
//                 When set it receives the current Blackboard and must return
//                 true for the choice to be listed by `available_choices`.
//   * next_node : id of the DialogueNode to advance to when this choice is
//                 selected. May reference the same node (loop) or a
//                 not-yet-registered node id (forward reference); both are
//                 allowed - validation happens at `select_choice` time so
//                 trees can be built up incrementally.
// -----------------------------------------------------------------------------
struct DialogueChoice
{
    std::string id {};
    std::string text {};
    Condition   condition {};
    std::string next_node {};
};

// -----------------------------------------------------------------------------
// DialogueNode - one node in the dialogue graph.
//
// A node is terminal when its `choices` vector is empty; `DialogueVM::is_at_end`
// reports that condition. Terminal nodes still carry speaker + text so the
// final line of a branch is visible before the conversation closes.
// -----------------------------------------------------------------------------
struct DialogueNode
{
    std::string                 id {};
    std::string                 speaker {};
    std::string                 text {};
    std::vector<DialogueChoice> choices {};
};

// -----------------------------------------------------------------------------
// LoadResult - status returned by `DialogueVM::load_tree`. The granular
// failure tags let callers report which authoring mistake hit them without
// std::expected / std::variant ceremony at the call-site.
// -----------------------------------------------------------------------------
enum class LoadResult : std::uint8_t
{
    kOk                  = 0,
    kEmptyTree           = 1,  ///< no nodes supplied
    kDuplicateNodeId     = 2,  ///< two nodes shared an id
    kEmptyNodeId         = 3,  ///< a node had id == ""
    kDuplicateChoiceId   = 4,  ///< two choices on one node shared an id
    kEmptyChoiceId       = 5,  ///< a choice had id == ""
    kMissingStartNode    = 6,  ///< explicit start id not present in the tree
};

// -----------------------------------------------------------------------------
// SelectResult - status returned by `DialogueVM::select_choice`.
//
// kNoOp covers the brief's "unknown choice id = no-op" contract: the
// machine returns the tag instead of advancing, so callers can show UI
// feedback without crashing on stale input.
// -----------------------------------------------------------------------------
enum class SelectResult : std::uint8_t
{
    kAdvanced         = 0,  ///< current_node updated successfully
    kNoOp             = 1,  ///< choice id not present on current node
    kConditionFailed  = 2,  ///< choice exists but its predicate returned false
    kNotStarted       = 3,  ///< select_choice before load_tree / after failed load
    kAtEnd            = 4,  ///< current node is terminal
    kBrokenLink       = 5,  ///< next_node id not present in the tree
};

// -----------------------------------------------------------------------------
// DialogueVM - the branching dialogue virtual machine.
//
// Lifecycle:
//   1. `load_tree(nodes [, start_id])` - takes ownership of a graph and seeds
//      the current node. The first entry in `nodes` is the default start; pass
//      a non-empty `start_id` to override.
//   2. `current_node()` - read the active node (speaker / text / choices) for
//      UI rendering. Returns nullptr if the tree never loaded.
//   3. `available_choices(blackboard)` - filtered visible-choices snapshot.
//      Each returned pointer aliases a choice owned by the active node and is
//      valid until the next mutating call (`select_choice`, `reset`,
//      `load_tree`).
//   4. `select_choice(choice_id [, blackboard])` - commit one choice and
//      advance current_node. The optional blackboard re-runs the choice's
//      Condition; passing nullptr skips the check (matches a UI that already
//      filtered the visible set).
//   5. `reset()` - jump back to the start node without re-loading the tree.
//
// Thread-safety: NOT thread-safe.
// -----------------------------------------------------------------------------
class DialogueVM
{
public:
    DialogueVM() = default;

    DialogueVM(const DialogueVM&)            = delete;
    DialogueVM& operator=(const DialogueVM&) = delete;
    DialogueVM(DialogueVM&&) noexcept            = default;
    DialogueVM& operator=(DialogueVM&&) noexcept = default;

    ~DialogueVM() = default;

    // ------- structure ----------------------------------------------------

    /// Install a tree by value. The first node is the default start; pass a
    /// non-empty `start_id` to pick a specific one. On any non-kOk result the
    /// VM is left empty (`current_node()` returns nullptr).
    LoadResult load_tree(std::vector<DialogueNode> nodes, std::string start_id = {});

    // ------- runtime queries ----------------------------------------------

    CD_NODISCARD const DialogueNode* current_node() const noexcept;

    CD_NODISCARD const std::string& current_node_id() const noexcept { return current_id_; }

    CD_NODISCARD const std::string& start_node_id() const noexcept { return start_id_; }

    /// Convenience: speaker tag of the active node, or empty string when the
    /// VM has not loaded a tree.
    CD_NODISCARD std::string_view current_speaker() const noexcept;

    /// True once `load_tree` succeeded.
    CD_NODISCARD bool is_loaded() const noexcept { return !nodes_.empty(); }

    /// True when the current node has zero choices (i.e. is terminal).
    CD_NODISCARD bool is_at_end() const noexcept;

    /// Number of nodes in the tree.
    CD_NODISCARD std::size_t node_count() const noexcept { return nodes_.size(); }

    /// All choices on the current node whose Condition passes against `bb`.
    /// Unconditional choices (empty Condition) are always included. Pointers
    /// alias into the owned tree; do not store across mutating calls.
    CD_NODISCARD std::vector<const DialogueChoice*>
        available_choices(const Blackboard& bb) const;

    /// Overload: returns every choice on the current node regardless of
    /// condition. Useful for editor / debug panels.
    CD_NODISCARD std::vector<const DialogueChoice*> all_choices() const;

    // ------- runtime mutations --------------------------------------------

    /// Commit a choice by id. When `bb` is non-null the choice's Condition is
    /// re-evaluated and a falsey result yields `kConditionFailed` without
    /// advancing. Unknown ids yield `kNoOp` (the brief's spec).
    SelectResult select_choice(std::string_view choice_id, const Blackboard* bb = nullptr);

    /// Convenience: select by id with a blackboard.
    SelectResult select_choice(std::string_view choice_id, const Blackboard& bb)
    {
        return select_choice(choice_id, &bb);
    }

    /// Reset current_node to the configured start. No-op when no tree loaded.
    void reset() noexcept;

private:
    // Lookup helpers --------------------------------------------------------
    CD_NODISCARD const DialogueNode* find_node(const std::string& id) const noexcept;
    CD_NODISCARD const DialogueChoice* find_choice(const DialogueNode& node,
                                                   std::string_view    id) const noexcept;

    std::vector<DialogueNode>                      nodes_ {};
    std::unordered_map<std::string, std::size_t>   index_ {};   ///< node_id -> nodes_ index
    std::string                                    start_id_ {};
    std::string                                    current_id_ {};
};

// -----------------------------------------------------------------------------
// parse_tree - tiny line-oriented DSL parser for the common authoring case
// (no Conditions). Grammar (one statement per line, '#' comments stripped):
//
//   NODE <id> [SPEAKER <speaker>]
//   TEXT <body...>             ; body extends to end-of-line; multiple TEXT
//                              ; lines on one node are joined with '\n'.
//   CHOICE <id> -> <next_node> : <prompt text>
//   END                        ; optional explicit end-of-node sentinel.
//
// A new NODE statement (or END) closes the previous node. Blank lines are
// ignored. The returned vector preserves source order so `nodes.front()` is
// the natural start node when no override is supplied to load_tree.
//
// Errors are reported via the returned optional: nullopt + a parse-time error
// describing the first offending line. The function never throws.
// -----------------------------------------------------------------------------
struct ParsedTree
{
    std::vector<DialogueNode> nodes {};
};

struct ParseError
{
    std::size_t line {0};   ///< 1-based line number
    std::string message {}; ///< human-readable
};

CD_NODISCARD std::variant<ParsedTree, ParseError> parse_tree(std::string_view source);

}  // namespace cd::game::dialogue
