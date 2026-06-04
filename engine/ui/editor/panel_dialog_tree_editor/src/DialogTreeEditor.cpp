// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_dialog_tree_editor/src/DialogTreeEditor.cpp
//
// phase666 — cd::editor::panel::dialog_tree_editor  implementation
// =============================================================================
#include <cd/editor/panel_dialog_tree_editor/DialogTreeEditor.hpp>

#include <algorithm>
#include <cstddef>
#include <queue>
#include <unordered_map>

namespace cd::editor::panel::dialog_tree_editor
{

// ---------------------------------------------------------------------------
// Node colour helpers — colour-coded by NodeKind.
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::ui::renderer::Color node_fill_color(cd::game::dialog_tree::NodeKind kind) noexcept
{
    switch (kind)
    {
    case cd::game::dialog_tree::NodeKind::kSay:
        // Blue — NPC speech
        return cd::ui::renderer::Color { 60U, 120U, 210U, 220U };
    case cd::game::dialog_tree::NodeKind::kChoice:
        // Yellow — player decision
        return cd::ui::renderer::Color { 210U, 185U, 50U, 220U };
    case cd::game::dialog_tree::NodeKind::kCondition:
        // Orange — runtime gate (invisible to player)
        return cd::ui::renderer::Color { 210U, 120U, 40U, 220U };
    case cd::game::dialog_tree::NodeKind::kEnd:
        // Grey — conversation terminal
        return cd::ui::renderer::Color { 110U, 110U, 120U, 200U };
    }
    return cd::ui::renderer::Color { 128U, 128U, 128U, 200U };
}

}  // namespace

// ---------------------------------------------------------------------------
// Tree binding API
// ---------------------------------------------------------------------------

void DialogTreeEditor::set_tree(const cd::game::dialog_tree::DialogTree* tree)
{
    tree_       = tree;
    selected_id_.clear();
    node_rects_.clear();
}

// ---------------------------------------------------------------------------
// Selection API
// ---------------------------------------------------------------------------

std::optional<std::string> DialogTreeEditor::selected_node_id() const
{
    if (selected_id_.empty())
        return std::nullopt;
    return selected_id_;
}

void DialogTreeEditor::simulate_click(float x, float y,
                                      const cd::ui::widgets::Rect& /*bounds*/)
{
    for (const NodeRect& nr : node_rects_)
    {
        if (nr.contains(x, y))
        {
            selected_id_ = nr.node_id;
            return;
        }
    }
    // No node hit — clear selection.
    selected_id_.clear();
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void DialogTreeEditor::draw(cd::ui::renderer::DrawBatcher&  batcher,
                            const cd::ui::widgets::Theme&   theme,
                            const cd::ui::widgets::Rect&    bounds) const
{
    // Background fill.
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    constexpr float kPad  = 6.0F;
    constexpr float kBarH = 4.0F;
    const float     row_w = bounds.w - 2.0F * kPad;

    // Separator bar under the title area (accent colour).
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    const float graph_x = bounds.x + kPad;
    const float graph_y = bounds.y + kPad * 2.0F + kBarH;
    const float graph_w = row_w;
    const float graph_h = bounds.h - kPad * 3.0F - kBarH;

    if (graph_h <= 0.0F || graph_w <= 0.0F || tree_ == nullptr || tree_->nodes.empty())
    {
        // Empty-state placeholder bar.
        batcher.quad(graph_x, graph_y,
                     graph_w * 0.4F, 14.0F,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         60U });
        return;
    }

    // ---- Build node-id -> index + depth map via BFS from root --------------
    // node_depth maps node_id -> depth level (0 = root).
    std::unordered_map<std::string, std::size_t> node_depth;
    node_depth.reserve(tree_->nodes.size());

    // BFS order: root first.
    {
        std::queue<std::string> queue;
        if (!tree_->root_id.empty())
        {
            queue.push(tree_->root_id);
            node_depth[tree_->root_id] = 0U;
        }

        // Build a quick lookup: node_id -> pointer to node.
        std::unordered_map<std::string, const cd::game::dialog_tree::DialogNode*> node_index;
        node_index.reserve(tree_->nodes.size());
        for (const auto& n : tree_->nodes)
            node_index[n.node_id] = &n;

        while (!queue.empty())
        {
            const std::string id = queue.front();
            queue.pop();

            const auto it = node_index.find(id);
            if (it == node_index.end())
                continue;

            const std::size_t depth = node_depth.at(id);
            for (const auto& next_id : it->second->next_ids)
            {
                if (node_depth.find(next_id) == node_depth.end())
                {
                    node_depth[next_id] = depth + 1U;
                    queue.push(next_id);
                }
            }
        }

        // Nodes not reachable from root get depth 0 (they still render).
        for (const auto& n : tree_->nodes)
        {
            if (node_depth.find(n.node_id) == node_depth.end())
                node_depth[n.node_id] = 0U;
        }
    }

    // ---- Determine the maximum depth and column populations ----------------
    std::size_t max_depth = 0U;
    for (const auto& kv : node_depth)
        max_depth = std::max(max_depth, kv.second);

    // Count how many nodes sit at each depth (for within-column vertical
    // spacing).
    std::vector<std::size_t> depth_count(max_depth + 1U, 0U);
    for (const auto& kv : node_depth)
        depth_count[kv.second]++;

    // ---- Compute node rects (graph-local coordinates) ----------------------
    // Layout: left-to-right per depth level.
    //   x = graph_x + depth * (kNodeW + kColGap)
    //   y = graph_y + within_depth_row * (kNodeH + kRowGap), centred in graph_h
    //
    // We keep a per-depth row counter to position within a column.
    constexpr float kNodeW   = 80.0F;
    constexpr float kNodeH   = 24.0F;
    constexpr float kColGap  = 32.0F;
    constexpr float kRowGap  = 12.0F;

    // Map node_id -> NodeRect.
    std::unordered_map<std::string, NodeRect> rect_map;
    rect_map.reserve(tree_->nodes.size());

    std::vector<std::size_t> depth_row_counter(max_depth + 1U, 0U);

    // Process in tree_.nodes order for deterministic layout.
    for (const auto& node : tree_->nodes)
    {
        const std::size_t depth = node_depth.at(node.node_id);
        const std::size_t row   = depth_row_counter[depth]++;

        const std::size_t rows_at_depth = depth_count[depth];

        // Centre the column vertically in graph_h.
        const float col_total_h =
            static_cast<float>(rows_at_depth) * kNodeH
            + static_cast<float>(rows_at_depth > 0U ? rows_at_depth - 1U : 0U) * kRowGap;

        const float col_start_y = graph_y + (graph_h - col_total_h) * 0.5F;
        const float nx = graph_x + static_cast<float>(depth) * (kNodeW + kColGap);
        const float ny = col_start_y + static_cast<float>(row) * (kNodeH + kRowGap);

        NodeRect nr;
        nr.node_id = node.node_id;
        nr.x = nx;
        nr.y = ny;
        nr.w = kNodeW;
        nr.h = kNodeH;
        rect_map[node.node_id] = nr;
    }

    // ---- Edges: draw before nodes so nodes paint over them -----------------
    // For each node with next_ids, draw an L-shaped connector to each child:
    //   vertical stem down from parent bottom-centre
    //   horizontal bar at mid-Y
    //   drop to child top-centre
    {
        constexpr float kEdgeT = 1.5F;
        const cd::ui::renderer::Color edge_col {
            theme.text_dim.r,
            theme.text_dim.g,
            theme.text_dim.b,
            140U };

        for (const auto& node : tree_->nodes)
        {
            const auto it_p = rect_map.find(node.node_id);
            if (it_p == rect_map.end())
                continue;
            const NodeRect& pr = it_p->second;

            if (node.next_ids.empty())
                continue;

            // Parent connector: bottom-centre of parent.
            const float px = pr.x + pr.w * 0.5F;
            const float py = pr.y + pr.h;  // parent bottom

            for (const auto& next_id : node.next_ids)
            {
                const auto it_c = rect_map.find(next_id);
                if (it_c == rect_map.end())
                    continue;
                const NodeRect& cr = it_c->second;

                // Child connector: top-centre of child.
                const float cx = cr.x + cr.w * 0.5F;
                const float cy = cr.y;  // child top

                // Mid Y for the horizontal bar.
                const float mid_y = (py + cy) * 0.5F;

                // Vertical stem from parent down to mid-Y.
                if (mid_y > py)
                {
                    batcher.quad(px - kEdgeT * 0.5F, py,
                                 kEdgeT, mid_y - py,
                                 edge_col);
                }

                // Horizontal bar connecting parent-x to child-x at mid-Y.
                const float hbar_left  = std::min(px, cx) - kEdgeT * 0.5F;
                const float hbar_right = std::max(px, cx) + kEdgeT * 0.5F;
                batcher.quad(hbar_left, mid_y - kEdgeT * 0.5F,
                             hbar_right - hbar_left, kEdgeT,
                             edge_col);

                // Drop from mid-Y to child top.
                if (cy > mid_y)
                {
                    batcher.quad(cx - kEdgeT * 0.5F, mid_y,
                                 kEdgeT, cy - mid_y,
                                 edge_col);
                }
            }
        }
    }

    // ---- Nodes: draw on top of edges ----------------------------------------
    constexpr float kBorderT = 2.0F;

    node_rects_.clear();
    node_rects_.reserve(tree_->nodes.size());

    for (const auto& node : tree_->nodes)
    {
        const auto it = rect_map.find(node.node_id);
        if (it == rect_map.end())
            continue;
        const NodeRect& nr = it->second;

        // Store for hit-testing.
        node_rects_.push_back(nr);

        const bool is_selected = (!selected_id_.empty() && node.node_id == selected_id_);

        // Selection border (accent quad behind the node body).
        if (is_selected)
        {
            batcher.quad(nr.x - kBorderT, nr.y - kBorderT,
                         nr.w + kBorderT * 2.0F, nr.h + kBorderT * 2.0F,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             255U });
        }

        // Node body (colour-coded by kind).
        batcher.quad(nr.x, nr.y, nr.w, nr.h,
                     node_fill_color(node.kind));

        // Dim inner label strip (simulates a text placeholder without
        // requiring a font atlas).
        constexpr float kLabelH   = 5.0F;
        constexpr float kLabelPad = 6.0F;
        batcher.quad(nr.x + kLabelPad,
                     nr.y + (nr.h - kLabelH) * 0.5F,
                     nr.w - kLabelPad * 2.0F, kLabelH,
                     cd::ui::renderer::Color {
                         theme.surface.r,
                         theme.surface.g,
                         theme.surface.b,
                         100U });
    }
}

}  // namespace cd::editor::panel::dialog_tree_editor
