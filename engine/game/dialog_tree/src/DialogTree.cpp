// =============================================================================
// CHROMODYNAMIC - cd/game/dialog_tree/DialogTree.cpp
// Phase 645 - cd::game::dialog_tree (BG3-style branching dialog graph)
//
// Implementation notes:
//
// * load() copies the caller's node vector into nodes_ and rebuilds the
//   O(1) hash-index. Copying is deliberate: the caller may release or mutate
//   the original DialogTree (hot-reload scenario) without invalidating this
//   runtime's state.
//
// * go_to() is the central transition function. It resolves a node id and,
//   when it lands on a kCondition node, immediately evaluates the boolean
//   store and recurses to the appropriate branch. This transparent chaining
//   means kCondition nodes are never visible to current() — they are pure
//   routing nodes, matching BG3's "script check" concept.
//
// * advance() for kSay/kEnd/kCondition ignores choice_index by design: the
//   API contract keeps the call-site uniform regardless of node kind. The
//   player UI only needs to supply an index for kChoice nodes; all other
//   kinds call advance(0) (or advance() with the default).
//
// * is_done() returns true both when done_ is set AND when current_id_ is
//   empty (not started). This prevents null-deref if the caller forgets
//   start().
// =============================================================================
#include <cd/game/dialog_tree/DialogTree.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace cd::game::dialog_tree
{

// =============================================================================
// Internal helpers
// =============================================================================

const DialogNode* DialogTreeRuntime::find_node(const std::string& id) const noexcept
{
    const auto it = index_.find(id);
    if (it == index_.end())
    {
        return nullptr;
    }
    return &nodes_.at(it->second);
}

bool DialogTreeRuntime::eval_condition(const std::string& var_name) const noexcept
{
    const auto it = cond_vars_.find(var_name);
    if (it == cond_vars_.end())
    {
        return false;  // undefined var → false branch
    }
    return it->second;
}

// Maximum kCondition chain depth before we consider the graph cyclic.
// 1 024 is orders of magnitude beyond any realistic dialog graph.
static constexpr std::size_t kMaxConditionDepth = 1024;

void DialogTreeRuntime::go_to(const std::string& node_id, std::size_t depth)
{
    // Cycle guard: if we have recursed through more kCondition nodes than the
    // ceiling allows, the graph contains a cycle — terminate gracefully instead
    // of stack-overflowing.
    if (depth > kMaxConditionDepth)
    {
        done_       = true;
        current_id_ = {};
        return;
    }

    if (node_id.empty())
    {
        done_       = true;
        current_id_ = {};
        return;
    }

    const DialogNode* node = find_node(node_id);
    if (node == nullptr)
    {
        // Designer typo / broken link → end gracefully
        done_       = true;
        current_id_ = {};
        return;
    }

    if (node->kind == NodeKind::kEnd)
    {
        done_       = true;
        current_id_ = node->node_id;
        return;
    }

    if (node->kind == NodeKind::kCondition)
    {
        // Transparent routing: evaluate and chain without surfacing to caller.
        const bool        result = eval_condition(node->condition_var);
        const std::size_t idx    = result ? std::size_t{0} : std::size_t{1};
        if (idx < node->next_ids.size())
        {
            go_to(node->next_ids[idx], depth + 1);
        }
        else
        {
            // Branch target missing → end
            done_       = true;
            current_id_ = {};
        }
        return;
    }

    // kSay or kChoice: surface to caller
    current_id_ = node->node_id;
    done_       = false;
}

// =============================================================================
// DialogTreeRuntime — public API
// =============================================================================

void DialogTreeRuntime::load(const DialogTree& tree)
{
    nodes_.clear();
    index_.clear();
    root_id_    = tree.root_id;
    current_id_ = {};
    done_       = true;
    // NOTE: cond_vars_ is intentionally NOT cleared on load() so that
    // callers can set condition vars before load() or between loads.

    nodes_.reserve(tree.nodes.size());
    for (const DialogNode& node : tree.nodes)
    {
        const std::size_t idx = nodes_.size();
        nodes_.push_back(node);
        index_[node.node_id] = idx;
    }
}

void DialogTreeRuntime::set_condition_var(std::string_view name, bool value)
{
    cond_vars_[std::string(name)] = value;
}

void DialogTreeRuntime::start()
{
    done_       = true;
    current_id_ = {};

    if (nodes_.empty() || root_id_.empty())
    {
        return;
    }

    go_to(root_id_);
}

const DialogNode* DialogTreeRuntime::current() const noexcept
{
    if (current_id_.empty())
    {
        return nullptr;
    }
    return find_node(current_id_);
}

bool DialogTreeRuntime::advance(std::size_t choice_index)
{
    if (done_ || current_id_.empty())
    {
        return false;
    }

    const DialogNode* node = find_node(current_id_);
    if (node == nullptr)
    {
        done_ = true;
        return false;
    }

    switch (node->kind)
    {
        case NodeKind::kEnd:
        {
            done_ = true;
            return false;
        }

        case NodeKind::kSay:
        {
            if (node->next_ids.empty())
            {
                done_       = true;
                current_id_ = {};
                return true;  // final say line consumed successfully
            }
            go_to(node->next_ids[0]);
            return true;
        }

        case NodeKind::kChoice:
        {
            if (choice_index >= node->next_ids.size())
            {
                return false;  // out-of-range selection
            }
            go_to(node->next_ids[choice_index]);
            return true;
        }

        case NodeKind::kCondition:
        {
            // Should not surface here (go_to is transparent), but handle
            // defensively in case the caller constructed current_id_ manually.
            const bool result      = eval_condition(node->condition_var);
            const std::size_t idx  = result ? std::size_t{0} : std::size_t{1};
            if (idx < node->next_ids.size())
            {
                go_to(node->next_ids[idx]);
                return true;
            }
            done_ = true;
            return false;
        }
    }

    // Unreachable with well-formed NodeKind values.
    done_ = true;
    return false;
}

bool DialogTreeRuntime::is_done() const noexcept
{
    return done_;
}

}  // namespace cd::game::dialog_tree
