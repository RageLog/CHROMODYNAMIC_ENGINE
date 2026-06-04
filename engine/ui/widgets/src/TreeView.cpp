// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/TreeView.cpp
//
// Phase 683 / M13 W5B implementation of cd::ui::widgets::TreeView.
//
// Traversal strategy
// ------------------
// Tree structure is stored as a flat node array with explicit child_indices.
// Root nodes are computed once after set_nodes by marking every node that
// appears as a child; the unmarked nodes are roots.
//
// visible_rows_() performs a DFS starting from each root in order, pushing
// a VisibleRow { node_index, depth } for each expanded-reachable node.
// The result is the same sequence that draw() and simulate_click() use, so
// hit-test geometry exactly matches the rendered layout.
//
// Draw
// ----
// Emits solid-colour quads only (no font / glyph path — same policy as
// Table in Phase 621):
//   * Row background alternates theme.background / theme.surface.
//   * Selected row gets a highlight quad (theme.accent, 50% alpha).
//   * Each depth level gets a 1 px wide indent-guide quad (theme.text_dim).
// Rows are clipped below bounds.y + bounds.h; this keeps draw O(visible)
// for trees with large collapsed subtrees.
//
// simulate_click
// --------------
// Maps the y coordinate into a visible-row index using the same kRowHeight
// constant as draw(). A click on a valid row selects that node. A click
// above or below all rows clears the selection.
// =============================================================================
#include <cd/ui/widgets/TreeView.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cd::ui::widgets
{

// ---- helpers ----------------------------------------------------------------

namespace
{

[[nodiscard]] cd::ui::renderer::Color to_rcolor(Color c) noexcept
{
    return cd::ui::renderer::Color { c.r, c.g, c.b, c.a };
}

[[nodiscard]] cd::ui::renderer::Color with_alpha(Color c, std::uint8_t a) noexcept
{
    return cd::ui::renderer::Color { c.r, c.g, c.b, a };
}

}  // namespace

// ---- set_nodes --------------------------------------------------------------

void TreeView::set_nodes(std::span<const Node> nodes)
{
    nodes_.assign(nodes.begin(), nodes.end());
    selected_.reset();
    roots_dirty_ = true;
}

// ---- root_count -------------------------------------------------------------

std::size_t TreeView::root_count() const noexcept
{
    if (roots_dirty_)
    {
        rebuild_roots_();
    }
    return roots_.size();
}

// ---- toggle_expand ----------------------------------------------------------

void TreeView::toggle_expand(std::size_t node_index)
{
    if (node_index >= nodes_.size())
    {
        return;
    }
    nodes_[node_index].expanded = !nodes_[node_index].expanded;
}

// ---- simulate_click ---------------------------------------------------------

void TreeView::simulate_click(float /*x*/, float y, const Rect& bounds)
{
    if (y < bounds.y || y >= bounds.y + bounds.h)
    {
        clear_selection();
        return;
    }

    const auto rows = visible_rows_();

    const float  rel_y   = y - bounds.y;
    const auto   row_idx = static_cast<std::size_t>(rel_y / kRowHeight);

    if (row_idx >= rows.size())
    {
        clear_selection();
        return;
    }

    selected_ = rows[row_idx].node_index;
}

// ---- draw -------------------------------------------------------------------

void TreeView::draw(cd::ui::renderer::DrawBatcher& batcher,
                    const Theme&                   theme,
                    const Rect&                    bounds) const
{
    if (!bounds.is_valid())
    {
        return;
    }

    const auto rows        = visible_rows_();
    const float bottom     = bounds.y + bounds.h;
    constexpr float kGuideW = 1.0F;

    for (std::size_t r = 0U; r < rows.size(); ++r)
    {
        const float ry = bounds.y + static_cast<float>(r) * kRowHeight;

        // Clip rows below the bounds.
        if (ry >= bottom)
        {
            break;
        }

        const std::size_t node_idx = rows[r].node_index;
        const std::size_t depth    = rows[r].depth;

        // Alternating row background.
        const Color row_bg = (r % 2U == 0U) ? theme.background : theme.surface;
        batcher.quad(bounds.x, ry, bounds.w, kRowHeight, to_rcolor(row_bg));

        // Selection highlight.
        if (selected_ && *selected_ == node_idx)
        {
            batcher.quad(bounds.x, ry, bounds.w, kRowHeight,
                         with_alpha(theme.accent, 128U));
        }

        // Indent-guide lines — one per ancestor depth level.
        for (std::size_t d = 0U; d < depth; ++d)
        {
            const float guide_x = bounds.x
                                + static_cast<float>(d) * kIndentWidth
                                + kIndentWidth * 0.5F;
            batcher.quad(guide_x, ry, kGuideW, kRowHeight,
                         to_rcolor(theme.text_dim));
        }
    }
}

// ---- rebuild_roots_ ---------------------------------------------------------

void TreeView::rebuild_roots_() const
{
    roots_.clear();

    // Mark nodes that appear as children.
    std::vector<bool> is_child(nodes_.size(), false);
    for (const Node& n : nodes_)
    {
        for (std::size_t ci : n.child_indices)
        {
            if (ci < nodes_.size())
            {
                is_child[ci] = true;
            }
        }
    }

    // Roots are the unmarked nodes.
    for (std::size_t i = 0U; i < nodes_.size(); ++i)
    {
        if (!is_child[i])
        {
            roots_.push_back(i);
        }
    }

    roots_dirty_ = false;
}

// ---- collect_visible_ -------------------------------------------------------

void TreeView::collect_visible_(std::size_t node_idx,
                                std::size_t depth,
                                std::vector<VisibleRow>& out) const
{
    if (node_idx >= nodes_.size())
    {
        return;
    }

    out.push_back(VisibleRow { node_idx, depth });

    const Node& n = nodes_[node_idx];
    if (n.expanded)
    {
        for (std::size_t ci : n.child_indices)
        {
            collect_visible_(ci, depth + 1U, out);
        }
    }
}

// ---- visible_rows_ ----------------------------------------------------------

std::vector<TreeView::VisibleRow> TreeView::visible_rows_() const
{
    if (roots_dirty_)
    {
        rebuild_roots_();
    }

    std::vector<VisibleRow> rows;
    rows.reserve(nodes_.size());

    for (std::size_t root_idx : roots_)
    {
        collect_visible_(root_idx, 0U, rows);
    }

    return rows;
}

}  // namespace cd::ui::widgets
