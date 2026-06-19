// =============================================================================
// CHROMODYNAMIC — cd/ui/layout/Flex.cpp
//
// Phase 1.0 Flex layout solver. Yoga-shape semantics, no wrap, single
// solver pass per subtree. See Flex.hpp for the scope and the public
// API contract.
//
// Algorithm summary (per parent node):
//   1. inner = outer - padding   (rect we lay children into)
//   2. base[i]   = resolve(child width/height + intrinsic + flex_basis)
//      margin[i] = margin along main axis consumed per child
//   3. used_main = sum(base + margin_main) + gaps
//   4. free      = inner_main - used_main
//      free > 0 && grow_total > 0 -> distribute free proportionally
//      free < 0 && shrink_total>0 -> shrink proportionally to base*shrink
//   5. cross[i]  = if align_items=stretch then inner_cross else intrinsic;
//      clamped by min/max cross.
//   6. main_pos  = justify_content advance algorithm
//      cross_pos = align_items per-child + margin cross-start
//   7. Absolute children (kAbsolute): placed via inset_* offsets, not in flow.
//   8. recurse  into each child with its computed rect
//
// Incremental decomposition (private methods):
//   compute_main()         — steps 2-4 (grow/shrink, clamp)
//   compute_cross()        — step 5
//   position_children()    — steps 6-7 + 8
//   solve_subtree() calls all three in order; separating them supports
//   future dirty-flagging where only the changed phase needs to re-run.
//
// Out of scope (Phase 2+):
//   flex-wrap, grid, BiDi RTL. gap_cross is a multi-line concept;
//   it has no effect in single-line layout and is intentionally ignored.
// =============================================================================
#include <cd/ui/layout/Flex.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace cd::ui::layout
{

namespace
{

[[nodiscard]] bool is_auto(float v) noexcept
{
    return v < 0.0F;  // kAuto = -1.0F
}

[[nodiscard]] float pad_main_start(const EdgeInsets& p, FlexDirection d) noexcept
{
    const bool is_row = (d == FlexDirection::kRow || d == FlexDirection::kRowReverse);
    return is_row ? p.left : p.top;
}

[[nodiscard]] float pad_cross_start(const EdgeInsets& p, FlexDirection d) noexcept
{
    const bool is_row = (d == FlexDirection::kRow || d == FlexDirection::kRowReverse);
    return is_row ? p.top : p.left;
}

[[nodiscard]] bool axis_is_row(FlexDirection d) noexcept
{
    return d == FlexDirection::kRow || d == FlexDirection::kRowReverse;
}

/// Resolve a child's base main-axis size given parent direction.
[[nodiscard]] float
resolve_base_main(const FlexStyle& child, FlexDirection parent_dir) noexcept
{
    const float pinned = axis_is_row(parent_dir) ? child.width : child.height;
    if (!is_auto(pinned))
    {
        return pinned;
    }
    if (!is_auto(child.flex_basis))
    {
        return child.flex_basis;
    }
    const float intrinsic = axis_is_row(parent_dir) ? child.intrinsic_width : child.intrinsic_height;
    if (!is_auto(intrinsic))
    {
        return intrinsic;
    }
    return 0.0F;
}

/// Resolve a child's base cross-axis size given parent direction.
[[nodiscard]] float
resolve_base_cross(const FlexStyle& child, FlexDirection parent_dir, float parent_inner_cross) noexcept
{
    const float pinned = axis_is_row(parent_dir) ? child.height : child.width;
    if (!is_auto(pinned))
    {
        return pinned;
    }
    const float intrinsic = axis_is_row(parent_dir) ? child.intrinsic_height : child.intrinsic_width;
    if (!is_auto(intrinsic))
    {
        return intrinsic;
    }
    return parent_inner_cross;
}

[[nodiscard]] float clamp_min_max(float v, float min_v, float max_v) noexcept
{
    const float r = std::max(v, min_v);
    return is_auto(max_v) ? r : std::min(r, max_v);
}

/// Margin consumed on the main axis for one child.
[[nodiscard]] float margin_main_size(const EdgeInsets& m, FlexDirection d) noexcept
{
    return axis_is_row(d) ? (m.left + m.right) : (m.top + m.bottom);
}

/// Margin start offset on the main axis (before the box).
[[nodiscard]] float margin_main_start(const EdgeInsets& m, FlexDirection d) noexcept
{
    return axis_is_row(d) ? m.left : m.top;
}

/// Margin start offset on the cross axis.
[[nodiscard]] float margin_cross_start_val(const EdgeInsets& m, FlexDirection d) noexcept
{
    return axis_is_row(d) ? m.top : m.left;
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

NodeId FlexTree::create_node(const FlexStyle& style)
{
    NodeId id { static_cast<std::uint32_t>(nodes_.size()) };
    InternalNode n {};
    n.style = style;
    nodes_.push_back(std::move(n));
    return id;
}

void FlexTree::add_child(NodeId parent, NodeId child)
{
    if (parent.value >= nodes_.size() || child.value >= nodes_.size())
    {
        return;
    }
    node(parent).children.push_back(child);
}

void FlexTree::set_style(NodeId node, const FlexStyle& style)
{
    if (node.value >= nodes_.size())
    {
        return;
    }
    this->node(node).style = style;
}

Rect FlexTree::layout(NodeId node) const noexcept
{
    if (node.value >= nodes_.size())
    {
        return {};
    }
    return this->node(node).computed_rect;
}

void FlexTree::solve(NodeId root, float available_width, float available_height)
{
    if (root.value >= nodes_.size())
    {
        return;
    }
    auto& r = node(root);
    const float w = is_auto(r.style.width)  ? available_width  : r.style.width;
    const float h = is_auto(r.style.height) ? available_height : r.style.height;
    r.computed_rect = Rect { 0.0F, 0.0F, w, h };
    solve_subtree(root, w, h);
}

// ============================================================================
// Private — incremental solver phases
// ============================================================================

void FlexTree::solve_subtree(NodeId node_id, float outer_w, float outer_h)
{
    auto& n = node(node_id);
    if (n.children.empty())
    {
        return;
    }

    const FlexDirection dir = n.style.direction;
    const float padded_w = std::max(0.0F, outer_w - (n.style.padding.left + n.style.padding.right));
    const float padded_h = std::max(0.0F, outer_h - (n.style.padding.top  + n.style.padding.bottom));

    SolveContext ctx;
    ctx.inner_main  = axis_is_row(dir) ? padded_w : padded_h;
    ctx.inner_cross = axis_is_row(dir) ? padded_h : padded_w;

    const std::size_t k = n.children.size();
    ctx.base_main.assign(k, 0.0F);
    ctx.base_cross.assign(k, 0.0F);
    ctx.final_main.assign(k, 0.0F);
    ctx.final_cross.assign(k, 0.0F);
    ctx.margin_main.assign(k, 0.0F);
    ctx.margin_cross_start.assign(k, 0.0F);

    compute_main(node_id, ctx);
    compute_cross(node_id, ctx);
    position_children(node_id, ctx);
}

void FlexTree::compute_main(NodeId node_id, SolveContext& ctx)
{
    // Phase A: resolve base main sizes, apply grow/shrink, clamp min/max.
    // Absolute children are sized but excluded from flow-grow/shrink.
    auto& n = node(node_id);
    const FlexDirection dir = n.style.direction;
    const std::size_t k = n.children.size();
    const float inner_main = ctx.inner_main;

    float used_main = 0.0F;
    std::size_t flow_count = 0;
    for (std::size_t i = 0; i < k; ++i)
    {
        const auto& cs = node(n.children[i]).style;
        ctx.base_main[i]  = resolve_base_main(cs, dir);
        ctx.base_cross[i] = resolve_base_cross(cs, dir, ctx.inner_cross);
        ctx.margin_main[i]        = margin_main_size(cs.margin, dir);
        ctx.margin_cross_start[i] = margin_cross_start_val(cs.margin, dir);
        if (cs.position != PositionType::kAbsolute)
        {
            used_main += ctx.base_main[i] + ctx.margin_main[i];
            ++flow_count;
        }
    }
    // Gaps between flow children only (flow_count-1 gaps).
    const float gap_total = (flow_count > 1)
        ? n.style.gap_main * static_cast<float>(flow_count - 1)
        : 0.0F;
    used_main += gap_total;

    const float free_main = inner_main - used_main;

    if (free_main > 0.0F)
    {
        float grow_total = 0.0F;
        for (std::size_t i = 0; i < k; ++i)
        {
            if (node(n.children[i]).style.position != PositionType::kAbsolute)
            {
                grow_total += node(n.children[i]).style.flex_grow;
            }
        }
        if (grow_total > 0.0F)
        {
            for (std::size_t i = 0; i < k; ++i)
            {
                if (node(n.children[i]).style.position == PositionType::kAbsolute)
                {
                    ctx.final_main[i] = ctx.base_main[i];
                    continue;
                }
                const float g = node(n.children[i]).style.flex_grow;
                ctx.final_main[i] = ctx.base_main[i] + free_main * (g / grow_total);
            }
        }
        else
        {
            for (std::size_t i = 0; i < k; ++i)
            {
                ctx.final_main[i] = ctx.base_main[i];
            }
        }
    }
    else if (free_main < 0.0F)
    {
        float shrink_total = 0.0F;
        for (std::size_t i = 0; i < k; ++i)
        {
            if (node(n.children[i]).style.position != PositionType::kAbsolute)
            {
                shrink_total += node(n.children[i]).style.flex_shrink * ctx.base_main[i];
            }
        }
        if (shrink_total > 0.0F)
        {
            for (std::size_t i = 0; i < k; ++i)
            {
                if (node(n.children[i]).style.position == PositionType::kAbsolute)
                {
                    ctx.final_main[i] = ctx.base_main[i];
                    continue;
                }
                const float s = node(n.children[i]).style.flex_shrink;
                const float share = (s * ctx.base_main[i]) / shrink_total;
                ctx.final_main[i] = std::max(0.0F, ctx.base_main[i] + free_main * share);
            }
        }
        else
        {
            for (std::size_t i = 0; i < k; ++i)
            {
                ctx.final_main[i] = ctx.base_main[i];
            }
        }
    }
    else
    {
        for (std::size_t i = 0; i < k; ++i)
        {
            ctx.final_main[i] = ctx.base_main[i];
        }
    }

    // Clamp main against min/max.
    for (std::size_t i = 0; i < k; ++i)
    {
        const auto& cs = node(n.children[i]).style;
        const float min_v = axis_is_row(dir) ? cs.min_width  : cs.min_height;
        const float max_v = axis_is_row(dir) ? cs.max_width  : cs.max_height;
        ctx.final_main[i] = clamp_min_max(ctx.final_main[i], min_v, max_v);
    }
}

void FlexTree::compute_cross(NodeId node_id, SolveContext& ctx)
{
    // Phase B: resolve cross sizes for all children.
    auto& n = node(node_id);
    const FlexDirection dir = n.style.direction;
    const std::size_t k = n.children.size();

    for (std::size_t i = 0; i < k; ++i)
    {
        const auto& cs = node(n.children[i]).style;
        const float pinned_cross = axis_is_row(dir) ? cs.height : cs.width;
        const float intr_cross   = axis_is_row(dir) ? cs.intrinsic_height : cs.intrinsic_width;

        if (cs.position == PositionType::kAbsolute)
        {
            // Absolute: cross size from pinned, then intrinsic, then insets.
            if (!is_auto(pinned_cross))
            {
                ctx.final_cross[i] = pinned_cross;
            }
            else if (!is_auto(intr_cross))
            {
                ctx.final_cross[i] = intr_cross;
            }
            else
            {
                // Derive from opposite insets if both are set.
                const float cs_start = axis_is_row(dir) ? cs.inset_top    : cs.inset_left;
                const float cs_end   = axis_is_row(dir) ? cs.inset_bottom : cs.inset_right;
                if (!is_auto(cs_start) && !is_auto(cs_end))
                {
                    ctx.final_cross[i] = std::max(0.0F, ctx.inner_cross - cs_start - cs_end);
                }
                else
                {
                    ctx.final_cross[i] = ctx.inner_cross;
                }
            }
        }
        else if (!is_auto(pinned_cross))
        {
            ctx.final_cross[i] = pinned_cross;
        }
        else if (n.style.align_items == AlignItems::kStretch)
        {
            ctx.final_cross[i] = std::max(0.0F, ctx.inner_cross
                - ctx.margin_cross_start[i]
                - (axis_is_row(dir) ? cs.margin.bottom : cs.margin.right));
        }
        else if (!is_auto(intr_cross))
        {
            ctx.final_cross[i] = intr_cross;
        }
        else
        {
            ctx.final_cross[i] = ctx.base_cross[i];
        }

        // Clamp cross min/max.
        const float min_v = axis_is_row(dir) ? cs.min_height : cs.min_width;
        const float max_v = axis_is_row(dir) ? cs.max_height : cs.max_width;
        ctx.final_cross[i] = clamp_min_max(ctx.final_cross[i], min_v, max_v);
    }
}

void FlexTree::position_children(NodeId node_id, const SolveContext& ctx)
{
    // Phase C: assign computed_rect to every child and recurse.
    auto& n = node(node_id);
    const FlexDirection dir = n.style.direction;
    const std::size_t k = n.children.size();
    const float inner_main  = ctx.inner_main;
    const float inner_cross = ctx.inner_cross;
    const float parent_world_x = n.computed_rect.x;
    const float parent_world_y = n.computed_rect.y;
    const float pad_ms = pad_main_start(n.style.padding, dir);
    const float pad_cs = pad_cross_start(n.style.padding, dir);

    // ---- Absolute children: position from insets, bypass flow ----------
    // Process absolute children first so their rects are set before recursion.
    for (std::size_t i = 0; i < k; ++i)
    {
        auto& c = node(n.children[i]);
        if (c.style.position != PositionType::kAbsolute)
        {
            continue;
        }

        const float fm = ctx.final_main[i];
        const float fc = ctx.final_cross[i];

        // Determine main position from insets.
        float main_pos = 0.0F;
        const float inset_ms = axis_is_row(dir) ? c.style.inset_left : c.style.inset_top;
        const float inset_me = axis_is_row(dir) ? c.style.inset_right : c.style.inset_bottom;
        if (!is_auto(inset_ms))
        {
            main_pos = pad_ms + inset_ms;
        }
        else if (!is_auto(inset_me))
        {
            main_pos = pad_ms + inner_main - inset_me - fm;
        }
        // else: stays at 0 (pad_ms offset applied below via main_pos default 0).

        // Determine cross position from insets.
        float cross_pos = 0.0F;
        const float inset_cs = axis_is_row(dir) ? c.style.inset_top : c.style.inset_left;
        const float inset_ce = axis_is_row(dir) ? c.style.inset_bottom : c.style.inset_right;
        if (!is_auto(inset_cs))
        {
            cross_pos = pad_cs + inset_cs;
        }
        else if (!is_auto(inset_ce))
        {
            cross_pos = pad_cs + inner_cross - inset_ce - fc;
        }

        if (axis_is_row(dir))
        {
            c.computed_rect = Rect { parent_world_x + main_pos,
                                     parent_world_y + cross_pos,
                                     fm, fc };
        }
        else
        {
            c.computed_rect = Rect { parent_world_x + cross_pos,
                                     parent_world_y + main_pos,
                                     fc, fm };
        }
        solve_subtree(n.children[i], c.computed_rect.width, c.computed_rect.height);
    }

    // ---- Flow children: justify-content + align-items ------------------

    // Count flow children for justify math (excludes absolute).
    std::size_t flow_count = 0;
    float final_used_main = 0.0F;
    for (std::size_t i = 0; i < k; ++i)
    {
        if (node(n.children[i]).style.position == PositionType::kAbsolute)
        {
            continue;
        }
        final_used_main += ctx.final_main[i] + ctx.margin_main[i];
        ++flow_count;
    }
    const float gap_total = (flow_count > 1)
        ? n.style.gap_main * static_cast<float>(flow_count - 1)
        : 0.0F;
    final_used_main += gap_total;
    const float final_free = std::max(0.0F, inner_main - final_used_main);

    float start_offset = 0.0F;
    float extra_gap    = 0.0F;
    switch (n.style.justify)
    {
        case JustifyContent::kFlexStart:
            break;
        case JustifyContent::kCenter:
            start_offset = final_free * 0.5F;
            break;
        case JustifyContent::kFlexEnd:
            start_offset = final_free;
            break;
        case JustifyContent::kSpaceBetween:
            if (flow_count > 1)
            {
                extra_gap = final_free / static_cast<float>(flow_count - 1);
            }
            break;
        case JustifyContent::kSpaceAround:
            extra_gap    = (flow_count > 0)
                ? final_free / static_cast<float>(flow_count)
                : 0.0F;
            start_offset = extra_gap * 0.5F;
            break;
        case JustifyContent::kSpaceEvenly:
            extra_gap    = (flow_count > 0)
                ? final_free / static_cast<float>(flow_count + 1)
                : 0.0F;
            start_offset = extra_gap;
            break;
    }

    const bool reverse = (dir == FlexDirection::kRowReverse || dir == FlexDirection::kColumnReverse);
    float main_cursor = pad_ms + start_offset;

    for (std::size_t i = 0; i < k; ++i)
    {
        auto& c = node(n.children[i]);
        if (c.style.position == PositionType::kAbsolute)
        {
            continue;
        }

        const float fm = ctx.final_main[i];
        const float fc = ctx.final_cross[i];
        const float ms = margin_main_start(c.style.margin, dir);

        // Cross-axis alignment.
        float cross_offset = ctx.margin_cross_start[i];
        switch (n.style.align_items)
        {
            case AlignItems::kFlexStart:
            case AlignItems::kStretch:
                // cross_offset = margin_cross_start already.
                break;
            case AlignItems::kCenter:
                cross_offset = (inner_cross - fc) * 0.5F;
                break;
            case AlignItems::kFlexEnd:
                cross_offset = inner_cross - fc
                    - (axis_is_row(dir) ? c.style.margin.bottom : c.style.margin.right);
                break;
        }

        // Main-axis: include margin-start before the box.
        const float box_main_start = main_cursor + ms;

        // Reverse direction: flip the position within inner_main.
        const float main_pos = reverse
            ? (pad_ms + inner_main - (box_main_start - pad_ms) - fm)
            : box_main_start;

        if (axis_is_row(dir))
        {
            c.computed_rect = Rect {
                parent_world_x + main_pos,
                parent_world_y + pad_cs + cross_offset,
                fm, fc };
        }
        else
        {
            c.computed_rect = Rect {
                parent_world_x + pad_cs + cross_offset,
                parent_world_y + main_pos,
                fc, fm };
        }

        solve_subtree(n.children[i], c.computed_rect.width, c.computed_rect.height);

        main_cursor += ctx.margin_main[i] + fm + n.style.gap_main + extra_gap;
    }
}

}  // namespace cd::ui::layout
