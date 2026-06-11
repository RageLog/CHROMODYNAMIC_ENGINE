// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/TreeView.hpp
//
// Phase 683 / M13 W5B — hierarchical tree widget in cd::ui::widgets.
//
// Design contract
// ---------------
// TreeView is a stateful, retained-mode widget that owns:
//   * A flat Node array (caller-supplied via set_nodes; internal copy).
//   * Expand/collapse state per node (stored inside the Node copy).
//   * A single-node selection index (optional).
//
// The tree structure is encoded as a flat array with each Node carrying a
// vector of child_indices (into the same flat array). Root nodes are nodes
// that appear in no other node's child_indices list; root_count() returns
// that count. This matches the pattern used by scene trees and asset browsers
// where IDs are stable but parent pointers may be absent in the data model.
//
// Public API (headless-safe — no GPU)
// ------------------------------------
//   set_nodes(span<const Node>)           -- replace node list (resets selection)
//   root_count()        -> size_t         -- number of root nodes
//   toggle_expand(idx)                    -- flip expanded flag for one node
//   selected_node_index() -> optional     -- current selection (nullopt = none)
//   simulate_click(x, y, bounds)          -- hit-test + state transition
//   draw(DrawBatcher, Theme, Rect)        -- emit quads for visible rows
//
// Draw layout within `bounds`
// ---------------------------
//   Only expanded subtrees are visited; collapsed nodes hide their children.
//   Each visible node occupies a row of height kRowHeight.
//   Indentation per depth level: kIndentWidth pixels.
//   A 1-pixel indent-guide line is drawn for each depth level.
//   Selected node receives a highlight quad (theme.accent, 50% alpha).
//
// simulate_click hit-test
// -----------------------
// Maps (y) into the visible row list (same DFS traversal as draw).
// An x-click on the expand arrow column (< kArrowWidth from the indent) is
// treated as a toggle_expand; elsewhere it is a selection. For headless
// test simplicity, simulate_click always selects the target node regardless
// of x position (expand/collapse is exposed separately via toggle_expand).
//
// Deep nesting
// ------------
// Arbitrary nesting depth is supported; draw clips rows that fall below
// bounds.y + bounds.h. This keeps draw O(visible_nodes) even for large trees.
// =============================================================================
#pragma once

#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cd::ui::renderer
{
class DrawBatcher;
} // namespace cd::ui::renderer

namespace cd::ui::widgets
{

// ---- Node ------------------------------------------------------------------

/// One node in the tree. id is a stable string key; label is display text.
/// child_indices are indices into the flat node array passed to set_nodes.
/// expanded controls visibility of the subtree.
struct Node
{
    std::string              id            {};
    std::string              label         {};
    std::vector<std::size_t> child_indices {};
    bool                     expanded      { false };
};

// ---- TreeView --------------------------------------------------------------

/// Retained-mode tree widget. Owns a copy of the node array and the
/// expand/selection state. Renderer-agnostic for headless testing.
class TreeView
{
public:
    static constexpr float kRowHeight   { 20.0F };  ///< Height of one visible row (px)
    static constexpr float kIndentWidth { 16.0F };  ///< Indentation per depth level (px)
    static constexpr float kArrowWidth  { 12.0F };  ///< Expand arrow column width (px)

    TreeView() = default;

    TreeView(const TreeView&)            = default;
    TreeView& operator=(const TreeView&) = default;
    TreeView(TreeView&&)                 = default;
    TreeView& operator=(TreeView&&)      = default;

    // ---- Node setup ---------------------------------------------------------

    /// Replace node list with a copy of `nodes`. Clears selection.
    /// The caller is responsible for index validity inside child_indices.
    void set_nodes(std::span<const Node> nodes);

    // ---- Accessors ----------------------------------------------------------

    /// Total node count (not just roots).
    [[nodiscard]] std::size_t node_count() const noexcept
    {
        return nodes_.size();
    }

    /// Number of root nodes (nodes that appear in no other node's
    /// child_indices). Computed lazily on first call after set_nodes.
    [[nodiscard]] std::size_t root_count() const noexcept;

    /// Read-only access to the internal node copy.
    [[nodiscard]] const Node& node(std::size_t idx) const noexcept
    {
        return nodes_[idx];
    }

    // ---- Selection ----------------------------------------------------------

    /// Current selected node index (nullopt = nothing selected).
    [[nodiscard]] std::optional<std::size_t> selected_node_index() const noexcept
    {
        return selected_;
    }

    /// Clear selection.
    void clear_selection() noexcept { selected_.reset(); }

    // ---- Expand / collapse --------------------------------------------------

    /// Flip the expanded flag for node at `node_index`. No-op when out of
    /// range. Nodes with no children can be toggled but draw ignores them.
    void toggle_expand(std::size_t node_index);

    // ---- Input (headless / test) --------------------------------------------

    /// Map (x, y) inside `bounds` to a visible row and update selection.
    /// Out-of-bounds y clears selection.
    void simulate_click(float x, float y, const Rect& bounds);

    // ---- Rendering ----------------------------------------------------------

    /// Emit draw commands for all currently visible nodes inside `bounds`.
    /// Clips rows below bounds.y + bounds.h. Uses DFS traversal respecting
    /// expanded flags. Emits:
    ///   * One row background quad per visible node.
    ///   * One selection highlight quad for the selected node.
    ///   * One indent-guide line quad per depth level (kIndentWidth offset).
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const Theme&                   theme,
              const Rect&                    bounds) const;

private:
    // ---- Internal types -----------------------------------------------------

    struct VisibleRow
    {
        std::size_t node_index { 0U };
        std::size_t depth      { 0U };
    };

    // ---- Helpers ------------------------------------------------------------

    /// Build the root-index list. A root is a node not referenced by any
    /// other node's child_indices.
    void rebuild_roots_() const;

    /// DFS walk of the expanded tree; appends to `out`.
    void collect_visible_(std::size_t node_idx,
                          std::size_t depth,
                          std::vector<VisibleRow>& out) const;

    /// Walk visible rows (respects expand state) and return the list.
    [[nodiscard]] std::vector<VisibleRow> visible_rows_() const;

    // ---- State --------------------------------------------------------------

    std::vector<Node>        nodes_    {};
    std::optional<std::size_t> selected_ {};

    // Cached root indices — rebuilt when nodes_ changes.
    mutable std::vector<std::size_t> roots_   {};
    mutable bool                     roots_dirty_ { true };
};

}  // namespace cd::ui::widgets
