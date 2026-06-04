// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_scene_palette/src/ScenePalette.cpp
//
// phase717 — cd::editor::panel::scene_palette  implementation
// =============================================================================
#include <cd/editor/panel_scene_palette/ScenePalette.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cd::editor::panel::scene_palette
{

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

void ScenePalette::fill_tokens(const cd::ui::widgets::Theme& theme,
                               TokenDesc                     out[kTokenCount]) noexcept
{
    // Order: visual reading order — background first, accents last.
    // Exactly kTokenCount (15) entries.
    out[ 0] = { "background",     theme.background    };
    out[ 1] = { "surface",        theme.surface       };
    out[ 2] = { "surface_subtle", theme.surface_subtle};
    out[ 3] = { "surface_hover",  theme.surface_hover };
    out[ 4] = { "surface_press",  theme.surface_press };
    out[ 5] = { "divider",        theme.divider       };
    out[ 6] = { "text",           theme.text          };
    out[ 7] = { "text_dim",       theme.text_dim      };
    out[ 8] = { "dim_overlay",    theme.dim_overlay   };
    out[ 9] = { "accent",         theme.accent        };
    out[10] = { "accent_hover",   theme.accent_hover  };
    out[11] = { "focus_ring",     theme.focus_ring    };
    out[12] = { "accent_warning", theme.accent_warning};
    out[13] = { "accent_error",   theme.accent_error  };
    out[14] = { "accent_success", theme.accent_success};
}

void ScenePalette::color_to_hex(cd::ui::widgets::Color c, char buf[8]) noexcept
{
    // "#RRGGBB" — 7 chars + NUL.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    (void)std::snprintf(buf, 8U, "#%02X%02X%02X",
                        static_cast<unsigned>(c.r),
                        static_cast<unsigned>(c.g),
                        static_cast<unsigned>(c.b));
}

cd::ui::widgets::Rect
ScenePalette::swatch_rect(std::size_t i,
                           const cd::ui::widgets::Rect& bounds) noexcept
{
    const float cols      = kCols;
    const float cell_w    = (bounds.w - 2.0F * kPad - (cols - 1.0F) * kGap) / cols;
    const float col       = static_cast<float>(i % static_cast<std::size_t>(kCols));
    const float row       = static_cast<float>(i / static_cast<std::size_t>(kCols));

    const float x = bounds.x + kPad + col * (cell_w + kGap);
    const float y = bounds.y + kPad + row * (kSwatchH + kGap);

    return { x, y, cell_w, kSwatchH };
}

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void ScenePalette::set_palette(const cd::ui::widgets::Theme* palette) noexcept
{
    palette_ = palette;
}

std::optional<std::string> ScenePalette::selected_token() const
{
    return selected_;
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void ScenePalette::simulate_click(float x, float y,
                                   const cd::ui::widgets::Rect& bounds) noexcept
{
    // Bounds guard.
    if (x < bounds.x || x > bounds.x + bounds.w ||
        y < bounds.y || y > bounds.y + bounds.h)
    {
        return;
    }

    // Use a default theme for hit-testing geometry when no palette is set —
    // layout is independent of colour values.
    const cd::ui::widgets::Theme fallback{};
    const cd::ui::widgets::Theme& src = (palette_ != nullptr) ? *palette_ : fallback;

    TokenDesc tokens[kTokenCount];
    fill_tokens(src, tokens);

    for (std::size_t i = 0U; i < kTokenCount; ++i)
    {
        const auto r = swatch_rect(i, bounds);
        if (x >= r.x && x < r.x + r.w &&
            y >= r.y && y < r.y + r.h)
        {
            selected_ = std::string(tokens[i].name);
            return;
        }
    }
    // Click landed in a gap — leave selection unchanged.
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void ScenePalette::draw(cd::ui::renderer::DrawBatcher& batcher,
                         const cd::ui::widgets::Theme&  palette,
                         const cd::ui::widgets::Rect&   bounds) const
{
    // ---- 1. Panel background ------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     palette.background.r,
                     palette.background.g,
                     palette.background.b,
                     palette.background.a });

    if (!bounds.is_valid())
        return;

    // ---- 2. Swatch grid -----------------------------------------------------
    TokenDesc tokens[kTokenCount];
    fill_tokens(palette, tokens);

    for (std::size_t i = 0U; i < kTokenCount; ++i)
    {
        const auto r = swatch_rect(i, bounds);

        // Swatch background: the token colour itself.
        batcher.quad(r.x, r.y, r.w, r.h,
                     cd::ui::renderer::Color {
                         tokens[i].color.r,
                         tokens[i].color.g,
                         tokens[i].color.b,
                         tokens[i].color.a });

        // Selection highlight: a kBorderW-thick inset border strip (top edge).
        const bool is_selected =
            selected_.has_value() && selected_.value() == tokens[i].name;

        if (is_selected)
        {
            // Draw four thin border quads (top / bottom / left / right).
            const float bw = kBorderW;
            // Top
            batcher.quad(r.x, r.y, r.w, bw,
                         cd::ui::renderer::Color { 255U, 220U, 100U, 240U });
            // Bottom
            batcher.quad(r.x, r.y + r.h - bw, r.w, bw,
                         cd::ui::renderer::Color { 255U, 220U, 100U, 240U });
            // Left
            batcher.quad(r.x, r.y, bw, r.h,
                         cd::ui::renderer::Color { 255U, 220U, 100U, 240U });
            // Right
            batcher.quad(r.x + r.w - bw, r.y, bw, r.h,
                         cd::ui::renderer::Color { 255U, 220U, 100U, 240U });
        }

        // Label strip at the bottom of the swatch (darker translucent band).
        const float label_h  = 16.0F;
        const float label_y  = r.y + r.h - label_h;
        batcher.quad(r.x, label_y, r.w, label_h,
                     cd::ui::renderer::Color { 0U, 0U, 0U, 140U });
    }
}

}  // namespace cd::editor::panel::scene_palette
