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
//   3. used_main = sum(base) + gaps
//   4. free      = inner_main - used_main
//      free > 0 && grow_total > 0 -> distribute free proportionally
//      free < 0 && shrink_total>0 -> shrink proportionally to base*shrink
//   5. cross[i]  = if align_items=stretch then inner_cross else intrinsic
//   6. main_pos  = justify_content advance algorithm
//      cross_pos = align_items per-child
//   7. recurse  into each child with its computed rect (less child padding)
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

[[nodiscard]] float pad_main(const EdgeInsets& p, FlexDirection d) noexcept
{
    const bool is_row = (d == FlexDirection::kRow || d == FlexDirection::kRowReverse);
    return is_row ? (p.left + p.right) : (p.top + p.bottom);
}

[[nodiscard]] float pad_cross(const EdgeInsets& p, FlexDirection d) noexcept
{
    const bool is_row = (d == FlexDirection::kRow || d == FlexDirection::kRowReverse);
    return is_row ? (p.top + p.bottom) : (p.left + p.right);
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
    // The pinned size depends on which axis the PARENT is laying out
    // along: parent row -> child main = child.width; parent column ->
    // child main = child.height.
    const float pinned = axis_is_row(parent_dir) ? child.width : child.height;
    if (!is_auto(pinned))
    {
        return pinned;
    }
    if (!is_auto(child.flex_basis))
    {
        return child.flex_basis;
    }
    // Intrinsic hint along parent's main axis.
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
    // Inherits parent's inner cross extent at stretch resolution time.
    return parent_inner_cross;
}

[[nodiscard]] float clamp_min_max(float v, float min_v, float max_v) noexcept
{
    float r = std::max(v, min_v);
    if (!is_auto(max_v))
    {
        r = std::min(r, max_v);
    }
    return r;
}

}  // namespace

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
    // Root sits at (0, 0) by default; its own width / height honour pinned
    // values if the caller set them, otherwise fill `available_*`.
    const float w = is_auto(r.style.width)  ? available_width  : r.style.width;
    const float h = is_auto(r.style.height) ? available_height : r.style.height;
    r.computed_rect = Rect { 0.0F, 0.0F, w, h };
    solve_subtree(root, w, h);
}

void FlexTree::solve_subtree(NodeId node_id, float outer_w, float outer_h)
{
    auto& n = node(node_id);
    const FlexDirection dir = n.style.direction;

    const float inner_w = std::max(0.0F, outer_w - pad_main(n.style.padding, dir) * (axis_is_row(dir) ? 1.0F : 0.0F)
                                              - pad_cross(n.style.padding, dir) * (axis_is_row(dir) ? 0.0F : 1.0F));
    // Reset: just compute width/height-minus-padding sanely.
    const float padded_w = std::max(0.0F, outer_w - (n.style.padding.left + n.style.padding.right));
    const float padded_h = std::max(0.0F, outer_h - (n.style.padding.top  + n.style.padding.bottom));

    const float inner_main  = axis_is_row(dir) ? padded_w : padded_h;
    const float inner_cross = axis_is_row(dir) ? padded_h : padded_w;
    (void)inner_w;  // legacy local; using padded_w/_h directly is clearer.

    if (n.children.empty())
    {
        return;
    }

    const std::size_t k = n.children.size();
    std::vector<float> base_main(k, 0.0F);
    std::vector<float> base_cross(k, 0.0F);
    std::vector<float> final_main(k, 0.0F);
    std::vector<float> final_cross(k, 0.0F);

    // 1. Resolve base sizes.
    float used_main = 0.0F;
    for (std::size_t i = 0; i < k; ++i)
    {
        const auto& cs = node(n.children[i]).style;
        base_main[i]  = resolve_base_main(cs, dir);
        base_cross[i] = resolve_base_cross(cs, dir, inner_cross);
        used_main    += base_main[i];
    }
    // Gaps between children along main axis (k-1 gaps).
    const float gap_main_total = (k > 1) ? n.style.gap_main * static_cast<float>(k - 1) : 0.0F;
    used_main += gap_main_total;

    // 2. Free space along main axis.
    float free_main = inner_main - used_main;

    // 3. Grow / shrink distribution.
    if (free_main > 0.0F)
    {
        float grow_total = 0.0F;
        for (std::size_t i = 0; i < k; ++i)
        {
            grow_total += node(n.children[i]).style.flex_grow;
        }
        if (grow_total > 0.0F)
        {
            for (std::size_t i = 0; i < k; ++i)
            {
                const float g = node(n.children[i]).style.flex_grow;
                final_main[i] = base_main[i] + free_main * (g / grow_total);
            }
        }
        else
        {
            for (std::size_t i = 0; i < k; ++i)
            {
                final_main[i] = base_main[i];
            }
        }
    }
    else if (free_main < 0.0F)
    {
        // Shrink proportional to (base * shrink).
        float shrink_total = 0.0F;
        for (std::size_t i = 0; i < k; ++i)
        {
            shrink_total += node(n.children[i]).style.flex_shrink * base_main[i];
        }
        if (shrink_total > 0.0F)
        {
            for (std::size_t i = 0; i < k; ++i)
            {
                const float s = node(n.children[i]).style.flex_shrink;
                const float share = (s * base_main[i]) / shrink_total;
                final_main[i] = std::max(0.0F, base_main[i] + free_main * share);
            }
        }
        else
        {
            for (std::size_t i = 0; i < k; ++i)
            {
                final_main[i] = base_main[i];
            }
        }
    }
    else
    {
        for (std::size_t i = 0; i < k; ++i)
        {
            final_main[i] = base_main[i];
        }
    }

    // Clamp main against min/max + min 0.
    for (std::size_t i = 0; i < k; ++i)
    {
        const auto& cs = node(n.children[i]).style;
        const float min_v = axis_is_row(dir) ? cs.min_width  : cs.min_height;
        const float max_v = axis_is_row(dir) ? cs.max_width  : cs.max_height;
        final_main[i] = clamp_min_max(final_main[i], min_v, max_v);
    }

    // 4. Resolve cross sizes (stretch if align-items=stretch and unpinned).
    for (std::size_t i = 0; i < k; ++i)
    {
        const auto& cs = node(n.children[i]).style;
        const float pinned_cross = axis_is_row(dir) ? cs.height : cs.width;
        const float intr_cross   = axis_is_row(dir) ? cs.intrinsic_height : cs.intrinsic_width;
        if (!is_auto(pinned_cross))
        {
            final_cross[i] = pinned_cross;
        }
        else if (n.style.align_items == AlignItems::kStretch)
        {
            final_cross[i] = inner_cross;
        }
        else if (!is_auto(intr_cross))
        {
            final_cross[i] = intr_cross;
        }
        else
        {
            final_cross[i] = base_cross[i];
        }
        const float min_v = axis_is_row(dir) ? cs.min_height : cs.min_width;
        const float max_v = axis_is_row(dir) ? cs.max_height : cs.max_width;
        final_cross[i] = clamp_min_max(final_cross[i], min_v, max_v);
    }

    // 5. Recompute used_main from final sizes for justify-content offset math.
    float final_used_main = 0.0F;
    for (std::size_t i = 0; i < k; ++i)
    {
        final_used_main += final_main[i];
    }
    final_used_main += gap_main_total;
    const float final_free = std::max(0.0F, inner_main - final_used_main);

    // 6. Justify-content start offset + between-gap.
    float start_offset   = 0.0F;
    float extra_gap      = 0.0F;
    switch (n.style.justify)
    {
        case JustifyContent::kFlexStart:
            start_offset = 0.0F;
            break;
        case JustifyContent::kCenter:
            start_offset = final_free * 0.5F;
            break;
        case JustifyContent::kFlexEnd:
            start_offset = final_free;
            break;
        case JustifyContent::kSpaceBetween:
            if (k > 1)
            {
                extra_gap = final_free / static_cast<float>(k - 1);
            }
            break;
        case JustifyContent::kSpaceAround:
            extra_gap    = final_free / static_cast<float>(k);
            start_offset = extra_gap * 0.5F;
            break;
        case JustifyContent::kSpaceEvenly:
            extra_gap    = final_free / static_cast<float>(k + 1);
            start_offset = extra_gap;
            break;
    }

    // 7. Position children + recurse.
    const float pad_ms = pad_main_start(n.style.padding, dir);
    const float pad_cs = pad_cross_start(n.style.padding, dir);
    float main_cursor  = pad_ms + start_offset;
    for (std::size_t i = 0; i < k; ++i)
    {
        auto& c = node(n.children[i]);
        const float fm = final_main[i];
        const float fc = final_cross[i];

        // Cross-axis position.
        float cross_offset = 0.0F;
        switch (n.style.align_items)
        {
            case AlignItems::kFlexStart:
            case AlignItems::kStretch:
                cross_offset = 0.0F;
                break;
            case AlignItems::kCenter:
                cross_offset = (inner_cross - fc) * 0.5F;
                break;
            case AlignItems::kFlexEnd:
                cross_offset = inner_cross - fc;
                break;
        }

        // Reverse logic: main_cursor walks from end toward start.
        const bool reverse = (dir == FlexDirection::kRowReverse || dir == FlexDirection::kColumnReverse);
        const float reversed_pos = inner_main - (main_cursor - pad_ms) - fm + pad_ms;
        const float main_pos = reverse ? reversed_pos : main_cursor;

        // Project (main_pos, cross_offset) back to (x, y), then offset
        // by the parent's world position so layout() returns world rects
        // suitable for direct rendering / hit-testing without a parent
        // stack walk.
        const float parent_world_x = n.computed_rect.x;
        const float parent_world_y = n.computed_rect.y;
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

        // Recurse: child's outer rect is what we just computed.
        solve_subtree(n.children[i], c.computed_rect.width, c.computed_rect.height);

        main_cursor += fm + n.style.gap_main + extra_gap;
    }
}

void FlexTree::compute_main(NodeId, float)   { /* reserved for future incremental solver */ }
void FlexTree::compute_cross(NodeId, float)  { /* reserved */ }
void FlexTree::position_children(NodeId)     { /* reserved */ }

}  // namespace cd::ui::layout
