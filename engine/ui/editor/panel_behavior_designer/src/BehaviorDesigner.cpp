// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_behavior_designer/src/BehaviorDesigner.cpp
//
// phase556-557-558 — cd::editor::panel::behavior_designer  implementation
// =============================================================================
#include <cd/editor/panel_behavior_designer/BehaviorDesigner.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace cd::editor::panel::behavior_designer
{

// ---------------------------------------------------------------------------
// Tree root binding API
// ---------------------------------------------------------------------------

void BehaviorDesigner::set_tree_root(BehaviorNodeId id) noexcept
{
    root_id_ = id;
}

BehaviorNodeId BehaviorDesigner::tree_root() const noexcept
{
    return root_id_;
}

// ---------------------------------------------------------------------------
// Node selection API
// ---------------------------------------------------------------------------

void BehaviorDesigner::set_selected(BehaviorNodeId id) noexcept
{
    selected_id_ = id;
}

BehaviorNodeId BehaviorDesigner::selected() const noexcept
{
    return selected_id_;
}

// ---------------------------------------------------------------------------
// View pan API
// ---------------------------------------------------------------------------

void BehaviorDesigner::set_pan_offset(float x, float y) noexcept
{
    pan_x_ = x;
    pan_y_ = y;
}

float BehaviorDesigner::pan_x() const noexcept
{
    return pan_x_;
}

float BehaviorDesigner::pan_y() const noexcept
{
    return pan_y_;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void BehaviorDesigner::draw(cd::ui::renderer::DrawBatcher& batcher,
                            const cd::ui::widgets::Theme&  theme,
                            const cd::ui::widgets::Rect&   bounds) const
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

    const float graph_y = bounds.y + kPad * 2.0F + kBarH;
    const float graph_h = bounds.h - kPad * 3.0F - kBarH;
    const float graph_w = row_w;
    const float graph_x = bounds.x + kPad;

    if (graph_h <= 0.0F || graph_w <= 0.0F)
        return;

    // ---- Placeholder grid ---------------------------------------------------
    // Horizontal lines (thin quads, 1 px high) every 32 px, shifted by pan.
    {
        constexpr float kGridLineH   = 1.0F;
        constexpr float kGridSpacing = 32.0F;

        const cd::ui::renderer::Color grid_col {
            theme.text_dim.r,
            theme.text_dim.g,
            theme.text_dim.b,
            40U };

        // Horizontal lines.
        {
            const float offset_y = std::fmod(pan_y_, kGridSpacing);
            float y = graph_y + offset_y;
            while (y < graph_y + graph_h)
            {
                if (y >= graph_y)
                {
                    batcher.quad(graph_x, y,
                                 graph_w, kGridLineH,
                                 grid_col);
                }
                y += kGridSpacing;
            }
        }

        // Vertical lines.
        {
            const float offset_x = std::fmod(pan_x_, kGridSpacing);
            float x = graph_x + offset_x;
            while (x < graph_x + graph_w)
            {
                if (x >= graph_x)
                {
                    batcher.quad(x, graph_y,
                                 kGridLineH, graph_h,
                                 grid_col);
                }
                x += kGridSpacing;
            }
        }
    }

    // ---- Sprint-1 demo nodes ------------------------------------------------
    // Three fixed nodes (in graph-local coordinates, panned by pan_x_/pan_y_).
    //
    //   Node 0 (Root / Selector)   — top-centre
    //   Node 1 (Sequence / Child A) — bottom-left
    //   Node 2 (Action / Child B)  — bottom-right
    //
    // Node ids: 1U = Root, 2U = Child A, 3U = Child B (matching demo BT ids).
    //
    // Colours:
    //   Root     — mid-blue  (Selector)
    //   Sequence — green     (Composite)
    //   Action   — amber     (Leaf)

    constexpr float kNodeW  = 80.0F;
    constexpr float kNodeH  = 30.0F;
    constexpr float kBorderT = 2.0F;  // selection border thickness

    struct DemoNode
    {
        BehaviorNodeId id;
        float          lx;   // local x (before pan)
        float          ly;   // local y (before pan)
        cd::ui::renderer::Color fill;
    };

    const float mid_x  = graph_w * 0.5F - kNodeW * 0.5F;
    const float top_y  = graph_h * 0.25F - kNodeH * 0.5F;
    const float bot_y  = graph_h * 0.65F - kNodeH * 0.5F;
    const float left_x = graph_w * 0.20F - kNodeW * 0.5F;
    const float right_x = graph_w * 0.80F - kNodeW * 0.5F;

    const std::array<DemoNode, 3U> demo_nodes {{
        { 1U, mid_x,   top_y,  cd::ui::renderer::Color { 60U,  120U, 200U, 220U } }, // Root
        { 2U, left_x,  bot_y,  cd::ui::renderer::Color { 60U,  180U, 80U,  220U } }, // Sequence
        { 3U, right_x, bot_y,  cd::ui::renderer::Color { 210U, 160U, 40U,  220U } }, // Action
    }};

    // Compute absolute positions with pan applied.
    auto node_abs_x = [&](const DemoNode& n) noexcept {
        return graph_x + n.lx + pan_x_;
    };
    auto node_abs_y = [&](const DemoNode& n) noexcept {
        return graph_y + n.ly + pan_y_;
    };

    // ---- Edges (drawn before nodes so nodes render on top) ------------------
    // Edge: Root -> Child A  (thin horizontal + vertical bars)
    // Edge: Root -> Child B
    {
        constexpr float kEdgeT = 2.0F;

        const cd::ui::renderer::Color edge_col {
            theme.text_dim.r,
            theme.text_dim.g,
            theme.text_dim.b,
            160U };

        const DemoNode& root    = demo_nodes[0];
        const DemoNode& child_a = demo_nodes[1];
        const DemoNode& child_b = demo_nodes[2];

        // Root bottom-centre connector point.
        const float rx = node_abs_x(root) + kNodeW * 0.5F;
        const float ry = node_abs_y(root) + kNodeH;

        // Child A top-centre.
        const float ax = node_abs_x(child_a) + kNodeW * 0.5F;
        const float ay = node_abs_y(child_a);

        // Child B top-centre.
        const float bx = node_abs_x(child_b) + kNodeW * 0.5F;
        const float by = node_abs_y(child_b);

        // Mid-point Y (horizontal bus).
        const float mid_y = ry + (ay - ry) * 0.5F;

        // Vertical stem from Root down to bus.
        batcher.quad(rx - kEdgeT * 0.5F, ry,
                     kEdgeT, mid_y - ry,
                     edge_col);

        // Horizontal bus: leftmost x to rightmost x.
        const float bus_left  = std::min(ax, bx) - kEdgeT * 0.5F;
        const float bus_right = std::max(ax, bx) + kEdgeT * 0.5F;
        batcher.quad(bus_left, mid_y - kEdgeT * 0.5F,
                     bus_right - bus_left, kEdgeT,
                     edge_col);

        // Drop from bus to Child A.
        batcher.quad(ax - kEdgeT * 0.5F, mid_y,
                     kEdgeT, ay - mid_y,
                     edge_col);

        // Drop from bus to Child B.
        batcher.quad(bx - kEdgeT * 0.5F, mid_y,
                     kEdgeT, by - mid_y,
                     edge_col);
    }

    // ---- Nodes --------------------------------------------------------------
    for (const DemoNode& n : demo_nodes)
    {
        const float nx = node_abs_x(n);
        const float ny = node_abs_y(n);

        // Selection border: drawn as a slightly larger background quad.
        const bool is_selected = (n.id == selected_id_) && (selected_id_ != kInvalidNodeId);
        if (is_selected)
        {
            batcher.quad(nx - kBorderT, ny - kBorderT,
                         kNodeW + kBorderT * 2.0F, kNodeH + kBorderT * 2.0F,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             255U });
        }

        // Node body.
        batcher.quad(nx, ny, kNodeW, kNodeH, n.fill);

        // Dim inner label strip (simulates a text placeholder).
        constexpr float kLabelH   = 6.0F;
        constexpr float kLabelPad = 6.0F;
        batcher.quad(nx + kLabelPad, ny + (kNodeH - kLabelH) * 0.5F,
                     kNodeW - kLabelPad * 2.0F, kLabelH,
                     cd::ui::renderer::Color {
                         theme.surface.r,
                         theme.surface.g,
                         theme.surface.b,
                         120U });
    }

    // ---- No-root indicator --------------------------------------------------
    // When no tree root is bound, draw a dim placeholder row to signal the
    // empty state (similar pattern to Inspector's "no selection" row).
    if (root_id_ == kInvalidNodeId)
    {
        batcher.quad(graph_x, graph_y + graph_h - 20.0F,
                     graph_w * 0.3F, 14.0F,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         60U });
    }
}

}  // namespace cd::editor::panel::behavior_designer
