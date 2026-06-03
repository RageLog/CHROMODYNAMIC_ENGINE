// =============================================================================
// CHROMODYNAMIC - cd/game/dialog_tree/DialogTree.hpp
// Phase 645 - cd::game::dialog_tree (BG3-style branching dialog graph)
//
// Responsibility: directed acyclic (or cyclic) dialog graph with typed node
// kinds and a boolean condition-variable store. This is a sibling to
// cd::game::dialogue (linear chain VM) — dialog_tree handles the richer
// BG3 / Mass Effect style where:
//
//   * A designer authors a graph of named nodes.
//   * Each node has a NodeKind (Say, Choice, Condition, End).
//   * kCondition nodes fork on a named boolean variable without showing text
//     to the player — pure runtime gating.
//   * kChoice nodes expose their `next_ids` vector as player-selectable
//     branches; the runtime advances to `next_ids[choice_index]`.
//   * kSay nodes advance automatically to `next_ids[0]` (or end the tree
//     when empty).
//   * kEnd nodes terminate the conversation immediately.
//
// The designer can write the entire tree as plain data (DialogNode +
// DialogTree PODs) without recompiling — satisfying the "without recompiling"
// moment from the brief.
//
// API:
//   DialogTreeRuntime rt;
//   rt.load(tree);
//   rt.start();
//   while (!rt.is_done()) {
//       const DialogNode* n = rt.current();
//       if (n->kind == NodeKind::kChoice) { rt.advance(player_picks); }
//       else                              { rt.advance(0); }
//   }
//
// Threading: DialogTreeRuntime is NOT thread-safe. Own one per entity.
//
// Design references:
//   * Larian Studios — BG3 dialog system (GDC 2024 vault, accessed 2026).
//   * Inon Zur / BioWare — Mass Effect dialog wheel UX design.
//   * Ink / Yarn Spinner narrative graph models (see cd::game::dialogue
//     header for full citation).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::game::dialog_tree
{

// -----------------------------------------------------------------------------
// NodeKind — semantic role of a node in the graph.
//
//   kSay       : NPC/narrator speaks; runtime auto-advances to next_ids[0].
//   kChoice    : Player picks from next_ids; advance(i) selects next_ids[i].
//   kCondition : Invisible branch gate. Runtime checks condition_var in the
//                boolean store; true -> next_ids[0], false -> next_ids[1].
//                If the required branch index is absent the tree ends.
//   kEnd       : Conversation over. advance() is a no-op.
// -----------------------------------------------------------------------------
enum class NodeKind : unsigned char
{
    kSay       = 0,
    kChoice    = 1,
    kCondition = 2,
    kEnd       = 3,
};

// -----------------------------------------------------------------------------
// DialogNode — one vertex in the designer-authored dialog graph.
//
// Fields:
//   node_id       : unique string key within the owning DialogTree. Must not
//                   be empty.
//   kind          : see NodeKind above.
//   text          : spoken/displayed line (empty for kCondition / kEnd).
//   next_ids      : ordered successor node ids.
//                   kSay       : 0 (end) or 1 entry.
//                   kChoice    : one entry per selectable branch, in display
//                                order (index 0 = first player option).
//                   kCondition : [0] = true-branch id, [1] = false-branch id.
//                   kEnd       : ignored.
//   condition_var : for kCondition nodes, the boolean variable name to look up
//                   in DialogTreeRuntime's condition store. Unused by other
//                   kinds.
// -----------------------------------------------------------------------------
struct DialogNode
{
    std::string              node_id       {};
    NodeKind                 kind          { NodeKind::kSay };
    std::string              text          {};
    std::vector<std::string> next_ids      {};
    std::string              condition_var {};
};

// -----------------------------------------------------------------------------
// DialogTree — the complete designer-authored graph (plain data, no runtime
// state). Pass by value to DialogTreeRuntime::load().
//
// Fields:
//   tree_id : opaque identifier for logging / hot-reload correlation.
//   root_id : id of the starting node. Must exist in `nodes`.
//   nodes   : unordered collection of all nodes; DialogTreeRuntime indexes
//             them into a hash map on load().
// -----------------------------------------------------------------------------
struct DialogTree
{
    std::string              tree_id {};
    std::string              root_id {};
    std::vector<DialogNode>  nodes   {};
};

// -----------------------------------------------------------------------------
// DialogTreeRuntime — walks a DialogTree with player choices + condition vars.
//
// Lifecycle:
//   1. load(tree)  — index the graph; previous session state cleared.
//   2. start()     — jump to root_id; required before querying current().
//   3. current()   — read the active node (nullptr before start / after done).
//   4. advance(i)  — step forward:
//                      kSay       : i is ignored; follows next_ids[0].
//                      kChoice    : selects next_ids[i] (returns false when
//                                   i >= next_ids.size()).
//                      kCondition : i is ignored; branches on condition_var.
//                      kEnd       : no-op, returns false.
//   5. is_done()   — true after kEnd or a dead-end (missing next_ids).
//
// Condition store:
//   set_condition_var(name, value) — insert or update a boolean flag.
//   The store is populated before start() so designers can pre-seed game
//   state (quest flags, relationship scores, etc.) and the graph routes
//   correctly from the first kCondition node it encounters.
// -----------------------------------------------------------------------------
class DialogTreeRuntime
{
public:
    DialogTreeRuntime() = default;

    DialogTreeRuntime(const DialogTreeRuntime&)            = delete;
    DialogTreeRuntime& operator=(const DialogTreeRuntime&) = delete;
    DialogTreeRuntime(DialogTreeRuntime&&)                 = default;
    DialogTreeRuntime& operator=(DialogTreeRuntime&&)      = default;

    ~DialogTreeRuntime() = default;

    // ------- structure -------------------------------------------------------

    /// Index the graph. Clears any previous session state. Does not call
    /// start() — the caller drives the start() call so they can seed
    /// condition vars before the first kCondition node is visited.
    void load(const DialogTree& tree);

    // ------- condition store -------------------------------------------------

    /// Insert or overwrite a named boolean flag consulted by kCondition nodes.
    void set_condition_var(std::string_view name, bool value);

    // ------- runtime ---------------------------------------------------------

    /// Reset cursor to root_id. Must be called after load() and after seeding
    /// condition vars. Automatically skips any leading kCondition nodes so that
    /// current() always returns a renderable node (kSay / kChoice) or nullptr.
    void start();

    /// The active node, or nullptr when the runtime has not started or is done.
    CD_NODISCARD const DialogNode* current() const noexcept;

    /// Advance one step:
    ///   - kSay / kEnd / kCondition: choice_index is ignored.
    ///   - kChoice: selects next_ids[choice_index]; returns false when index
    ///              is out of range.
    /// Returns true on successful advance, false on no-op / done / bad index.
    bool advance(std::size_t choice_index = 0);

    /// True when the conversation has terminated (kEnd node reached or
    /// next_ids exhausted). Also true before start() is called.
    CD_NODISCARD bool is_done() const noexcept;

private:
    // Move to a node by id, transparently following kCondition chains.
    void go_to(const std::string& node_id);

    CD_NODISCARD const DialogNode* find_node(const std::string& id) const noexcept;

    CD_NODISCARD bool eval_condition(const std::string& var_name) const noexcept;

    // Indexed view into the last load()ed tree (nodes owned by caller — we
    // copy them in to avoid dangling when a designer hot-reloads).
    std::vector<DialogNode>                     nodes_    {};
    std::unordered_map<std::string, std::size_t> index_   {};  ///< node_id -> nodes_ index
    std::string                                 root_id_  {};
    std::string                                 current_id_ {};
    std::unordered_map<std::string, bool>       cond_vars_ {};
    bool                                        done_     { true };
};

}  // namespace cd::game::dialog_tree
