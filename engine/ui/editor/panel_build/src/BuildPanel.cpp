// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_build/src/BuildPanel.cpp
//
// phase699 — cd::editor::panel::build  implementation
// =============================================================================
#include <cd/editor/panel_build/BuildPanel.hpp>

#include <cstddef>
#include <cstdint>

namespace cd::editor::panel::build
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace
{

/// Clamp a uint32_t to uint8_t for Color construction.
[[nodiscard]] constexpr uint8_t u8(uint32_t v) noexcept
{
    return static_cast<uint8_t>(v > 255U ? 255U : v);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void BuildPanel::set_status(Status status) noexcept
{
    status_ = status;
}

void BuildPanel::push_event(const BuildEvent& event)
{
    events_.push_back(event);
}

void BuildPanel::clear_events() noexcept
{
    events_.clear();
    selected_.reset();
}

Status BuildPanel::current_status() const noexcept
{
    return status_;
}

std::size_t BuildPanel::event_count() const noexcept
{
    return events_.size();
}

std::optional<std::size_t> BuildPanel::selected_event_index() const noexcept
{
    return selected_;
}

// ---------------------------------------------------------------------------
// Interaction helpers
// ---------------------------------------------------------------------------

void BuildPanel::simulate_click(
    float x, float y, const cd::ui::widgets::Rect& bounds) noexcept
{
    // Outside panel?
    if (x < bounds.x || x > bounds.x + bounds.w ||
        y < bounds.y || y > bounds.y + bounds.h)
    {
        return;
    }

    const float rel_y = y - bounds.y;

    // Above the event list area?
    if (rel_y < kListOffsetY)
        return;

    // Hit-test each event row.
    for (std::size_t i = 0U; i < events_.size(); ++i)
    {
        const float top = row_top(i);
        if (rel_y >= top && rel_y < top + kRowH)
        {
            // Only select events that have a source_file (sprint-1 contract).
            if (!events_[i].source_file.empty())
                selected_ = i;
            return;
        }
    }
    // Click in a gap or below the list — keep existing selection.
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

// static
cd::ui::renderer::Color BuildPanel::status_color(Status s) noexcept
{
    switch (s)
    {
    case Status::kIdle:
        return { u8(140U), u8(140U), u8(140U), u8(220U) };  // grey
    case Status::kCompiling:
        return { u8(230U), u8(190U), u8(40U),  u8(220U) };  // yellow
    case Status::kSuccess:
        return { u8(70U),  u8(190U), u8(80U),  u8(220U) };  // green
    case Status::kFailed:
        return { u8(210U), u8(60U),  u8(60U),  u8(220U) };  // red
    default:
        return { u8(140U), u8(140U), u8(140U), u8(220U) };
    }
}

void BuildPanel::draw(
    cd::ui::renderer::DrawBatcher& batcher,
    const cd::ui::widgets::Theme&  theme,
    const cd::ui::widgets::Rect&   bounds) const
{
    // ---- 1. Panel background -----------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    const float inner_w = bounds.w - 2.0F * kPad;

    // ---- 2. Status badge ---------------------------------------------------
    const float badge_x = bounds.x + kPad;
    const float badge_y = bounds.y + kPad;

    batcher.quad(badge_x, badge_y, inner_w, kBadgeH,
                 status_color(status_));

    // Subtle inset: a thin darker outline so the badge reads as a card.
    constexpr float kOutlineT = 1.0F;
    batcher.quad(badge_x, badge_y, inner_w, kOutlineT,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     80U });

    // ---- 3. Accent separator bar ------------------------------------------
    const float bar_y = bounds.y + kPad + kBadgeH + kPad;
    batcher.quad(bounds.x + kPad, bar_y,
                 inner_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    // ---- 4. Event rows -----------------------------------------------------
    for (std::size_t i = 0U; i < events_.size(); ++i)
    {
        const float ry = bounds.y + row_top(i);

        // Clip rows that overflow the panel bottom.
        if (ry + kRowH > bounds.y + bounds.h - kPad)
            break;

        const bool is_selected = selected_.has_value() && *selected_ == i;
        const cd::ui::renderer::Color row_col = status_color(events_[i].severity);

        if (is_selected)
        {
            // Full-width accent highlight for the selected row.
            batcher.quad(bounds.x + kPad, ry, inner_w, kRowH,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             60U });
        }
        else
        {
            // Alternating background.
            const bool even = (i % 2U == 0U);
            const auto& bg = even ? theme.surface : theme.surface_hover;
            batcher.quad(bounds.x + kPad, ry, inner_w, kRowH,
                         cd::ui::renderer::Color {
                             bg.r, bg.g, bg.b, bg.a });
        }

        // Severity swatch: narrow colour bar on the left edge of the row.
        constexpr float kSwatchW = 3.0F;
        batcher.quad(bounds.x + kPad, ry + 2.0F,
                     kSwatchW, kRowH - 4.0F,
                     row_col);

        // Timestamp column: tinted surface_hover strip on the right of swatch.
        batcher.quad(bounds.x + kPad + kSwatchW + 2.0F, ry,
                     kTsColW, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         120U });
    }
}

}  // namespace cd::editor::panel::build
