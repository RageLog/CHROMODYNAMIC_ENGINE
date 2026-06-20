// =============================================================================
// CHROMODYNAMIC — cd/ui/Widget.cpp
// =============================================================================
#include <ranges>
#include <cd/ui/Widget.hpp>

#include <vector>

namespace cd::ui
{

Widget* Widget::hit_test(float px, float py) noexcept
{
    if (!visible_ || !enabled_ || !bounds_.contains(px, py))
        return nullptr;
    // Translate the query into our local frame before descending: each
    // child's bounds are expressed relative to MY origin, so we must
    // subtract OUR bounds.x/.y from the parent-frame point.
    const float lx = px - bounds_.x;
    const float ly = py - bounds_.y;
    // Walk children back-to-front so the topmost (last-added) widget wins.
    for (const auto& child : children_ | std::views::reverse)
    {
        if (auto* hit = child->hit_test(lx, ly))
            return hit;
    }
    return this;
}

void Widget::dispatch_click(float px, float py) noexcept
{
    if (auto* w = hit_test(px, py))
        w->on_click_();
}

void Widget::collect_draw_commands_(std::vector<DrawCommand>& out, float ox, float oy) const
{
    if (!visible_)
        return;
    // `(ox, oy)` is my parent's absolute origin. `emit_draw_` takes the
    // same convention so each concrete widget composes its absolute
    // position via `(ox + bounds.x, oy + bounds.y)`.
    emit_draw_(out, ox, oy);
    // For children, our absolute origin becomes their parent origin.
    const float nx = ox + bounds_.x;
    const float ny = oy + bounds_.y;
    for (const auto& c : children_)
    {
        c->collect_draw_commands_(out, nx, ny);
    }
}

}  // namespace cd::ui
