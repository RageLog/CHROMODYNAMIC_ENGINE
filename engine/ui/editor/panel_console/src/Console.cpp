// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_console/src/Console.cpp
//
// phase544 — cd::editor::panel::console  implementation
// =============================================================================
#include <cd/editor/panel_console/Console.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cd::editor::panel::console
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void Console::push_log(std::string_view msg)
{
    entries_.emplace_back(msg);
    while (entries_.size() > kMaxEntries)
        entries_.pop_front();
}

void Console::clear() noexcept
{
    entries_.clear();
}

std::size_t Console::entry_count() const noexcept
{
    return entries_.size();
}

const std::deque<std::string>& Console::entries() const noexcept
{
    return entries_;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void Console::draw(cd::ui::renderer::DrawBatcher& batcher,
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

    constexpr float kRowH   = 18.0F;
    constexpr float kPad    = 4.0F;
    constexpr float kBarH   = 3.0F;

    const float row_w = bounds.w - 2.0F * kPad;

    // Accent bar at the top of the panel (title separator).
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    if (entries_.empty())
        return;

    // Draw entries in reverse-chronological order (newest at top).
    // We iterate from back() to front() so the most recent entry appears
    // immediately below the accent bar.
    float cursor_y = bounds.y + kPad + kBarH + kPad;

    for (auto it = entries_.crbegin(); it != entries_.crend(); ++it)
    {
        if (cursor_y + kRowH > bounds.y + bounds.h)
            break;  // No more vertical space.

        // Row background (alternating subtle tint using index parity).
        const auto idx = static_cast<std::size_t>(
            std::distance(entries_.crbegin(), it));
        const bool even = (idx % 2U) == 0U;

        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kRowH,
                     cd::ui::renderer::Color {
                         even ? theme.surface_hover.r : theme.surface.r,
                         even ? theme.surface_hover.g : theme.surface.g,
                         even ? theme.surface_hover.b : theme.surface.b,
                         180U });

        // Leading accent pip on the left edge of each row.
        batcher.quad(bounds.x + kPad, cursor_y,
                     3.0F, kRowH,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         140U });

        cursor_y += kRowH + 2.0F;
    }
}

}  // namespace cd::editor::panel::console
