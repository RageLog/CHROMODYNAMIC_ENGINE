// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_asset_browser/src/AssetBrowser.cpp
//
// phase545 — cd::editor::panel::asset_browser  implementation
// =============================================================================
#include <cd/editor/panel_asset_browser/AssetBrowser.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cd::editor::panel::asset_browser
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void AssetBrowser::set_entries(std::span<const Entry> entries)
{
    entries_.assign(entries.begin(), entries.end());
    // Clear selection if it is now out-of-range.
    if (selected_ != npos && selected_ >= entries_.size())
    {
        selected_ = npos;
    }
}

std::size_t AssetBrowser::selected_index() const noexcept
{
    return selected_;
}

void AssetBrowser::set_selected_index(std::size_t idx) noexcept
{
    if (idx == npos || idx >= entries_.size())
    {
        selected_ = npos;
    }
    else
    {
        selected_ = idx;
    }
}

std::size_t AssetBrowser::entry_count() const noexcept
{
    return entries_.size();
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void AssetBrowser::draw(cd::ui::renderer::DrawBatcher& batcher,
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

    constexpr float kRowH  = 20.0F;
    constexpr float kPad   = 4.0F;
    constexpr float kBarH  = 3.0F;
    constexpr float kPipW  = 4.0F;

    const float row_w = bounds.w - 2.0F * kPad;

    // Accent bar at the top (title separator).
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    if (entries_.empty())
        return;

    float cursor_y = bounds.y + kPad + kBarH + kPad;

    for (std::size_t i = 0U; i < entries_.size(); ++i)
    {
        if (cursor_y + kRowH > bounds.y + bounds.h)
            break;  // No more vertical space.

        const bool is_selected = (i == selected_);
        const bool is_dir      = entries_[i].is_dir;

        // Row background — selected rows use the accent container colour;
        // directories use surface_hover; files use surface.
        cd::ui::renderer::Color row_bg;
        if (is_selected)
        {
            row_bg = cd::ui::renderer::Color {
                theme.accent_hover.r,
                theme.accent_hover.g,
                theme.accent_hover.b,
                200U };
        }
        else if (is_dir)
        {
            row_bg = cd::ui::renderer::Color {
                theme.surface_hover.r,
                theme.surface_hover.g,
                theme.surface_hover.b,
                160U };
        }
        else
        {
            row_bg = cd::ui::renderer::Color {
                theme.surface.r,
                theme.surface.g,
                theme.surface.b,
                140U };
        }

        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kRowH, row_bg);

        // Left pip: accent for directories, dim for files.
        const cd::ui::renderer::Color pip_col = is_dir
            ? cd::ui::renderer::Color {
                  theme.accent.r, theme.accent.g, theme.accent.b, 220U }
            : cd::ui::renderer::Color {
                  theme.text_dim.r, theme.text_dim.g, theme.text_dim.b, 120U };

        batcher.quad(bounds.x + kPad, cursor_y,
                     kPipW, kRowH, pip_col);

        cursor_y += kRowH + 2.0F;
    }
}

}  // namespace cd::editor::panel::asset_browser
