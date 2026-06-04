// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_behavior_designer/src/BehaviorDesigner.cpp
//
// phase556-557-558 — Sprint-1 placeholder.
// phase740         — Sprint-2: real BT graph rendering.
//
// Auto-layout algorithm (top-down):
//   Each node occupies one column; the subtree width is the maximum of
//   (1, sum_of_child_widths). Siblings are placed adjacently, parents are
//   centred above their children.  A single pass over the tree produces
//   (lx, ly) in graph-local pixel coordinates, which are then shifted by
//   the current pan offset at draw time.
//
// Colours per NodeKind (matching Unreal's BT editor palette for familiarity):
//   Selector  — blue   (60, 120, 200)
//   Sequence  — green  (60, 180,  80)
//   Parallel  — teal   (40, 180, 160)
//   Decorator — purple (160, 80, 200)
//   Leaf      — amber  (210, 160,  40)
// =============================================================================
#include <cd/editor/panel_behavior_designer/BehaviorDesigner.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cd::editor::panel::behavior_designer
{

namespace
{
    // -------------------------------------------------------------------------
    // Color palette per NodeKind.
    // -------------------------------------------------------------------------
    [[nodiscard]] cd::ui::renderer::Color color_for_kind(cd::game::ai_bt::NodeKind k) noexcept
    {
        using K = cd::game::ai_bt::NodeKind;
        switch (k)
        {
            case K::kSelector:  return { 60U,  120U, 200U, 220U };  // blue
            case K::kSequence:  return { 60U,  180U,  80U, 220U };  // green
            case K::kParallel:  return { 40U,  180U, 160U, 220U };  // teal
            case K::kDecorator: return { 160U,  80U, 200U, 220U };  // purple
            case K::kLeaf:      return { 210U, 160U,  40U, 220U };  // amber
        }
        return { 200U, 200U, 200U, 220U };  // fallback grey
    }

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Tree binding API
// ---------------------------------------------------------------------------

void BehaviorDesigner::set_tree(const cd::game::ai_bt::BehaviorTree* tree) noexcept
{
    tree_ = tree;
}

const cd::game::ai_bt::BehaviorTree* BehaviorDesigner::tree() const noexcept
{
    return tree_;
}

// ---------------------------------------------------------------------------
// Legacy tree root binding API
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
// measure_subtree_ — recursive layout.
//
// Returns the total width (in pixels) consumed by this subtree so the
// parent can centre itself above it.
// ---------------------------------------------------------------------------
float BehaviorDesigner::measure_subtree_(
    const cd::game::ai_bt::Node* node,
    BehaviorNodeId                parent_id,
    std::uint32_t                 depth,
    float                         col_offset,  // left edge of allowed band (px)
    float                         node_w,
    float                         node_h,
    float                         x_stride,
    float                         y_stride,
    BehaviorNodeId&               next_id,
    std::vector<LayoutNode>&      nodes_out)
{
    if (node == nullptr) { return 0.0F; }

    // Reserve the slot index before recursing (recursion may push_back more
    // entries, reallocating the vector — so we must NOT hold a reference
    // across recursive calls; access via index after recursion).
    const BehaviorNodeId my_id   = next_id++;
    const std::size_t    my_slot = nodes_out.size();

    // Push a placeholder; fill lx/ly after the children are measured.
    nodes_out.push_back(LayoutNode {
        my_id, node,
        cd::game::ai_bt::node_kind(node),
        0.0F, 0.0F,           // lx, ly — set after recursion
        node_w, node_h,
        parent_id
    });

    // Gather children.
    const std::vector<const cd::game::ai_bt::Node*> children =
        cd::game::ai_bt::node_children(node);

    float subtree_width = 0.0F;

    if (children.empty())
    {
        // Leaf: width is exactly one node slot.
        subtree_width = x_stride;
    }
    else
    {
        // Recurse into each child; accumulate total width.
        // NOTE: nodes_out may reallocate during these calls — do NOT cache
        // a pointer/reference to nodes_out[my_slot] across them.
        float child_col = col_offset;
        for (const cd::game::ai_bt::Node* child : children)
        {
            const float cw = measure_subtree_(child,
                                              my_id,
                                              depth + 1U,
                                              child_col,
                                              node_w, node_h,
                                              x_stride, y_stride,
                                              next_id,
                                              nodes_out);
            child_col     += cw;
            subtree_width += cw;
        }
    }

    // All children done — vector is stable now; index-access is safe.
    const float band_centre = col_offset + subtree_width * 0.5F;
    nodes_out.at(my_slot).lx = band_centre - node_w * 0.5F;
    nodes_out.at(my_slot).ly = static_cast<float>(depth) * y_stride;

    return subtree_width;
}

// ---------------------------------------------------------------------------
// draw_real_tree_ — emit quads for the real BT graph.
// ---------------------------------------------------------------------------
void BehaviorDesigner::draw_real_tree_(cd::ui::renderer::DrawBatcher& batcher,
                                       const cd::ui::widgets::Theme&  theme,
                                       float graph_x, float graph_y,
                                       float graph_w, float graph_h) const
{
    const cd::game::ai_bt::Node* root_node = tree_->root();
    if (root_node == nullptr) { return; }

    // ---- Build layout -------------------------------------------------------
    constexpr float kNodeW   = 88.0F;
    constexpr float kNodeH   = 32.0F;
    constexpr float kXStride = 104.0F;  // horizontal slot width (node + gap)
    constexpr float kYStride = 68.0F;   // vertical step between depth levels

    std::vector<LayoutNode> nodes;
    nodes.reserve(64U);
    BehaviorNodeId next_id = 1U;  // 0 == kInvalidNodeId

    measure_subtree_(root_node,
                     kInvalidNodeId,
                     0U,
                     0.0F,
                     kNodeW, kNodeH,
                     kXStride, kYStride,
                     next_id,
                     nodes);

    // ---- Compute bounding box and centre in graph area ----------------------
    float min_x =  1e9F;
    float max_x = -1e9F;
    float max_y = -1e9F;
    for (const LayoutNode& ln : nodes)
    {
        min_x = std::min(min_x, ln.lx);
        max_x = std::max(max_x, ln.lx + ln.w);
        max_y = std::max(max_y, ln.ly + ln.h);
    }
    const float tree_w = max_x - min_x;
    const float tree_h = max_y;

    // Shift so tree is centred horizontally; leave a small top margin.
    constexpr float kTopMargin = 16.0F;
    const float shift_x = (graph_w - tree_w) * 0.5F - min_x;
    const float shift_y = kTopMargin;

    // Helper: convert graph-local to absolute screen with pan.
    auto abs_x = [&](float lx) { return graph_x + lx + shift_x + pan_x_; };
    auto abs_y = [&](float ly) { return graph_y + ly + shift_y + pan_y_; };

    // ---- Edges (draw before nodes) ------------------------------------------
    constexpr float kEdgeT = 2.0F;
    const cd::ui::renderer::Color edge_col {
        theme.text_dim.r,
        theme.text_dim.g,
        theme.text_dim.b,
        160U };

    // Build a quick id→index map (linear scan is fine for ≤200 nodes).
    auto find_node = [&](BehaviorNodeId id) -> const LayoutNode*
    {
        for (const LayoutNode& ln : nodes)
        {
            if (ln.id == id) { return &ln; }
        }
        return nullptr;
    };

    for (const LayoutNode& ln : nodes)
    {
        if (ln.parent_id == kInvalidNodeId) { continue; }
        const LayoutNode* parent = find_node(ln.parent_id);
        if (parent == nullptr) { continue; }

        // Parent bottom-centre.
        const float px = abs_x(parent->lx) + parent->w * 0.5F;
        const float py = abs_y(parent->ly) + parent->h;
        // Child top-centre.
        const float cx = abs_x(ln.lx) + ln.w * 0.5F;
        const float cy = abs_y(ln.ly);

        // Midpoint bus Y.
        const float mid_y = (py + cy) * 0.5F;

        // Vertical stem: parent bottom to bus.
        const float stem_h = mid_y - py;
        if (stem_h > 0.0F)
        {
            batcher.quad(px - kEdgeT * 0.5F, py, kEdgeT, stem_h, edge_col);
        }

        // Horizontal bar at bus Y.
        const float bar_x0 = std::min(px, cx) - kEdgeT * 0.5F;
        const float bar_x1 = std::max(px, cx) + kEdgeT * 0.5F;
        if (bar_x1 > bar_x0)
        {
            batcher.quad(bar_x0, mid_y - kEdgeT * 0.5F,
                         bar_x1 - bar_x0, kEdgeT, edge_col);
        }

        // Vertical drop: bus to child top.
        const float drop_h = cy - mid_y;
        if (drop_h > 0.0F)
        {
            batcher.quad(cx - kEdgeT * 0.5F, mid_y, kEdgeT, drop_h, edge_col);
        }
    }

    // ---- Nodes --------------------------------------------------------------
    constexpr float kBorderT = 2.0F;
    constexpr float kLabelH  = 6.0F;
    constexpr float kLabelPad = 6.0F;

    // Clip to graph area (simple bounds check; avoids emitting invisible quads).
    const float graph_right  = graph_x + graph_w;
    const float graph_bottom = graph_y + graph_h;

    for (const LayoutNode& ln : nodes)
    {
        const float nx = abs_x(ln.lx);
        const float ny = abs_y(ln.ly);

        // Skip nodes that are entirely off-screen.
        if (nx + ln.w < graph_x || nx > graph_right)  { continue; }
        if (ny + ln.h < graph_y || ny > graph_bottom)  { continue; }

        // Selection border.
        const bool is_selected =
            (ln.id == selected_id_) && (selected_id_ != kInvalidNodeId);
        if (is_selected)
        {
            batcher.quad(nx - kBorderT, ny - kBorderT,
                         ln.w + kBorderT * 2.0F, ln.h + kBorderT * 2.0F,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             255U });
        }

        // Node body.
        batcher.quad(nx, ny, ln.w, ln.h, color_for_kind(ln.kind));

        // Dim label strip.
        batcher.quad(nx + kLabelPad,
                     ny + (ln.h - kLabelH) * 0.5F,
                     ln.w - kLabelPad * 2.0F, kLabelH,
                     cd::ui::renderer::Color {
                         theme.surface.r,
                         theme.surface.g,
                         theme.surface.b,
                         120U });
    }

    // ---- Empty-tree indicator -----------------------------------------------
    if (nodes.empty())
    {
        batcher.quad(graph_x, graph_y + graph_h - 20.0F,
                     graph_w * 0.3F, 14.0F,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         60U });
    }

    (void)tree_h;  // suppress unused-variable warning when clamping is skipped
}

// ---------------------------------------------------------------------------
// draw_demo_nodes_ — Sprint-1 fallback (3 hard-coded nodes).
// ---------------------------------------------------------------------------
void BehaviorDesigner::draw_demo_nodes_(cd::ui::renderer::DrawBatcher& batcher,
                                        const cd::ui::widgets::Theme&  theme,
                                        float graph_x, float graph_y,
                                        float graph_w, float graph_h) const
{
    constexpr float kNodeW   = 80.0F;
    constexpr float kNodeH   = 30.0F;
    constexpr float kBorderT = 2.0F;

    struct DemoNode
    {
        BehaviorNodeId id;
        float          lx;
        float          ly;
        cd::ui::renderer::Color fill;
    };

    const float mid_x   = graph_w * 0.5F - kNodeW * 0.5F;
    const float top_y   = graph_h * 0.25F - kNodeH * 0.5F;
    const float bot_y   = graph_h * 0.65F - kNodeH * 0.5F;
    const float left_x  = graph_w * 0.20F - kNodeW * 0.5F;
    const float right_x = graph_w * 0.80F - kNodeW * 0.5F;

    const std::array<DemoNode, 3U> demo_nodes {{
        { 1U, mid_x,   top_y,  { 60U,  120U, 200U, 220U } }, // Selector (blue)
        { 2U, left_x,  bot_y,  { 60U,  180U,  80U, 220U } }, // Sequence (green)
        { 3U, right_x, bot_y,  { 210U, 160U,  40U, 220U } }, // Leaf/Action (amber)
    }};

    auto node_ax = [&](const DemoNode& n) { return graph_x + n.lx + pan_x_; };
    auto node_ay = [&](const DemoNode& n) { return graph_y + n.ly + pan_y_; };

    // Edges.
    {
        constexpr float kEdgeT = 2.0F;
        const cd::ui::renderer::Color edge_col {
            theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 160U };

        const DemoNode& root    = demo_nodes[0];
        const DemoNode& child_a = demo_nodes[1];
        const DemoNode& child_b = demo_nodes[2];

        const float rx  = node_ax(root) + kNodeW * 0.5F;
        const float ry  = node_ay(root) + kNodeH;
        const float ax  = node_ax(child_a) + kNodeW * 0.5F;
        const float ay  = node_ay(child_a);
        const float bx  = node_ax(child_b) + kNodeW * 0.5F;
        const float by  = node_ay(child_b);
        const float mid = ry + (ay - ry) * 0.5F;

        batcher.quad(rx - kEdgeT * 0.5F, ry, kEdgeT, mid - ry, edge_col);

        const float bus_l = std::min(ax, bx) - kEdgeT * 0.5F;
        const float bus_r = std::max(ax, bx) + kEdgeT * 0.5F;
        batcher.quad(bus_l, mid - kEdgeT * 0.5F, bus_r - bus_l, kEdgeT, edge_col);

        batcher.quad(ax - kEdgeT * 0.5F, mid, kEdgeT, ay - mid, edge_col);
        batcher.quad(bx - kEdgeT * 0.5F, mid, kEdgeT, by - mid, edge_col);
    }

    // Nodes.
    for (const DemoNode& n : demo_nodes)
    {
        const float nx = node_ax(n);
        const float ny = node_ay(n);
        const bool is_selected = (n.id == selected_id_) && (selected_id_ != kInvalidNodeId);
        if (is_selected)
        {
            batcher.quad(nx - kBorderT, ny - kBorderT,
                         kNodeW + kBorderT * 2.0F, kNodeH + kBorderT * 2.0F,
                         { theme.accent.r, theme.accent.g, theme.accent.b, 255U });
        }
        batcher.quad(nx, ny, kNodeW, kNodeH, n.fill);

        constexpr float kLabelH   = 6.0F;
        constexpr float kLabelPad = 6.0F;
        batcher.quad(nx + kLabelPad, ny + (kNodeH - kLabelH) * 0.5F,
                     kNodeW - kLabelPad * 2.0F, kLabelH,
                     { theme.surface.r, theme.surface.g, theme.surface.b, 120U });
    }

    if (root_id_ == kInvalidNodeId)
    {
        batcher.quad(graph_x, graph_y + graph_h - 20.0F,
                     graph_w * 0.3F, 14.0F,
                     { theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 60U });
    }
}

// ---------------------------------------------------------------------------
// DrawBatcher path — top-level draw.
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

    // Separator bar under the title area.
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
    {
        constexpr float kGridLineH   = 1.0F;
        constexpr float kGridSpacing = 32.0F;
        const cd::ui::renderer::Color grid_col {
            theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 40U };

        const float offset_y = std::fmod(pan_y_, kGridSpacing);
        float y = graph_y + offset_y;
        while (y < graph_y + graph_h)
        {
            if (y >= graph_y)
                batcher.quad(graph_x, y, graph_w, kGridLineH, grid_col);
            y += kGridSpacing;
        }

        const float offset_x = std::fmod(pan_x_, kGridSpacing);
        float x = graph_x + offset_x;
        while (x < graph_x + graph_w)
        {
            if (x >= graph_x)
                batcher.quad(x, graph_y, kGridLineH, graph_h, grid_col);
            x += kGridSpacing;
        }
    }

    // ---- Nodes + edges ------------------------------------------------------
    if (tree_ != nullptr)
    {
        draw_real_tree_(batcher, theme, graph_x, graph_y, graph_w, graph_h);
    }
    else
    {
        draw_demo_nodes_(batcher, theme, graph_x, graph_y, graph_w, graph_h);
    }
}

}  // namespace cd::editor::panel::behavior_designer
