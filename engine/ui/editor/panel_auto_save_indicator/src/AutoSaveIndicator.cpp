// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_auto_save_indicator/src/AutoSaveIndicator.cpp
//
// phase719 — cd::editor::panel::auto_save_indicator::AutoSaveIndicator implementation.
//
// Draw layout (left → right, vertically centered within bounds):
//
//   [kPad] [icon 12×12] [kIconGap] [label bg: remaining width] [kPad]
//
// Colour scheme per status:
//   kIdle          — muted grey fill     (divider colour)
//   kPendingDirty  — amber accent_warning
//   kSaving        — yellow
//   kJustSaved     — green
//   kError         — red accent_error
//
// All draw calls use DrawBatcher::quad() only (no RHI, no ImGui).
// =============================================================================
#include <cd/editor/panel_auto_save_indicator/AutoSaveIndicator.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/theme/Theme.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <algorithm>
#include <cstdint>

namespace cd::editor::panel::auto_save_indicator
{

// ===========================================================================
// State API
// ===========================================================================

void AutoSaveIndicator::set_status(Status s) noexcept
{
    status_ = s;
}

void AutoSaveIndicator::mark_dirty() noexcept
{
    status_ = Status::kPendingDirty;
}

void AutoSaveIndicator::mark_saved() noexcept
{
    status_           = Status::kJustSaved;
    last_save_ms_ago_ = 0.0;
}

void AutoSaveIndicator::set_last_save_ms_ago(double ms) noexcept
{
    last_save_ms_ago_ = ms >= 0.0 ? ms : 0.0;
}

Status AutoSaveIndicator::current_status() const noexcept
{
    return status_;
}

double AutoSaveIndicator::last_save_ms_ago() const noexcept
{
    return last_save_ms_ago_;
}

// ===========================================================================
// Colour helpers
// ===========================================================================

cd::ui::renderer::Color
AutoSaveIndicator::status_fill_color(Status s) noexcept
{
    switch (s)
    {
    case Status::kIdle:
        // Muted grey — "nothing happening".
        return { 55U, 55U, 65U, 180U };

    case Status::kPendingDirty:
        // Amber / accent_warning — "attention needed".
        return { 200U, 130U, 30U, 200U };

    case Status::kSaving:
        // Yellow — "in progress".
        return { 210U, 190U, 40U, 200U };

    case Status::kJustSaved:
        // Green — "all good".
        return { 50U, 180U, 70U, 200U };

    case Status::kError:
        // Red / accent_error — "something went wrong".
        return { 200U, 50U, 50U, 210U };

    default:
        return { 55U, 55U, 65U, 180U };
    }
}

cd::ui::renderer::Color
AutoSaveIndicator::status_icon_color(Status s) noexcept
{
    switch (s)
    {
    case Status::kIdle:
        return { 110U, 110U, 120U, 220U };

    case Status::kPendingDirty:
        return { 230U, 160U, 50U, 255U };

    case Status::kSaving:
        return { 240U, 220U, 60U, 255U };

    case Status::kJustSaved:
        return { 80U, 210U, 100U, 255U };

    case Status::kError:
        return { 230U, 70U, 70U, 255U };

    default:
        return { 110U, 110U, 120U, 220U };
    }
}

// ===========================================================================
// Draw
// ===========================================================================

void AutoSaveIndicator::draw(cd::ui::renderer::DrawBatcher& batcher,
                              const cd::ui::theme::Theme&    theme,
                              const cd::ui::widgets::Rect&   bounds) const
{
    if (bounds.w <= 0.0F || bounds.h <= 0.0F) { return; }

    // ---- Background fill (colour-coded per status) -------------------------
    const cd::ui::renderer::Color fill = status_fill_color(status_);
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h, fill);

    // ---- 1 px border (theme primary, subtle) --------------------------------
    const auto& primary = theme.color(cd::ui::theme::PaletteSlot::kPrimary);
    const cd::ui::renderer::Color border {
        static_cast<std::uint8_t>(std::clamp(primary.r * 255.0F, 0.0F, 255.0F)),
        static_cast<std::uint8_t>(std::clamp(primary.g * 255.0F, 0.0F, 255.0F)),
        static_cast<std::uint8_t>(std::clamp(primary.b * 255.0F, 0.0F, 255.0F)),
        100U
    };
    // Top / bottom border strips.
    batcher.quad(bounds.x, bounds.y,                          bounds.w, kBorderW, border);
    batcher.quad(bounds.x, bounds.y + bounds.h - kBorderW,   bounds.w, kBorderW, border);

    // ---- Layout: vertical center -------------------------------------------
    const float icon_y = bounds.y + (bounds.h - kIconSize) * 0.5F;
    const float icon_x = bounds.x + kPad;

    // ---- Icon quad (12×12) --------------------------------------------------
    const cd::ui::renderer::Color icon_col = status_icon_color(status_);
    batcher.quad(icon_x, icon_y, kIconSize, kIconSize, icon_col);

    // ---- Label background quad ---------------------------------------------
    const float label_x = icon_x + kIconSize + kIconGap;
    const float label_w = bounds.x + bounds.w - kPad - label_x;
    if (label_w > 0.0F)
    {
        // Slightly transparent dark bg — text will be overlaid by the caller.
        const cd::ui::renderer::Color label_bg { 18U, 18U, 22U, 160U };
        batcher.quad(label_x, icon_y, label_w, kIconSize, label_bg);

        // Left accent stripe matching status colour.
        batcher.quad(label_x, icon_y, 2.0F, kIconSize, icon_col);
    }
}

}  // namespace cd::editor::panel::auto_save_indicator
