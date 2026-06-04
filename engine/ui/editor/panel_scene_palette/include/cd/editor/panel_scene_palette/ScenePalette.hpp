// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_scene_palette/ScenePalette.hpp
//
// phase717 — cd::editor::panel::scene_palette  (panel_scene_palette library)
//
// Scene Palette panel: live theme token preview grid.
//
// A UI designer opens this panel and sees every palette token rendered as a
// large, labelled colour swatch — no memorised hex values required. All 12+
// semantic tokens from cd::ui::widgets::Theme (background, surface,
// surface_hover, surface_press, surface_subtle, divider, accent, accent_hover,
// accent_warning, accent_error, accent_success, focus_ring, text, text_dim,
// dim_overlay) are laid out in a uniform grid. Each swatch shows:
//   * A filled colour rectangle (the token colour itself).
//   * The token name ("surface_subtle", "accent_warning", …) as a label.
//   * The hex value (#RRGGBB) as a sub-label.
//
// Clicking a swatch selects its token name, which can be retrieved via
// selected_token() — intended for future copy-to-clipboard integration.
//
// State API:
//   set_palette(const cd::ui::widgets::Theme*)
//                             — replace the live palette pointer. A null
//                               pointer causes draw() to emit only the panel
//                               background (safe no-op; does not crash).
//   selected_token() const    — returns the token name of the last clicked
//                               swatch, or std::nullopt when nothing is selected.
//   simulate_click(x, y, bounds)
//                             — hit-test the swatch grid; selects the swatch
//                               under the click. No-op when outside the panel
//                               or over an empty grid cell.
//
// Draw (DrawBatcher path — DockSpace / apps/editor):
//   draw(batcher, palette, bounds) const
//     — emits quads for:
//         1. Panel background fill.
//         2. One swatch per token: filled colour rect + lighter border strip.
//         3. Selection highlight border on the currently selected swatch.
//
// Lifetime contract:
//   ScenePalette is default-constructible and owns no external resources.
//   The palette pointer (set_palette) is non-owning; the caller is responsible
//   for the Theme object's lifetime. Thread-safe for read; write API must be
//   called from a single thread.
//
// MOMENT: A UI designer sees all 12+ palette tokens at-a-glance with actual
// colour swatches — not memorised hex values. Theme reskins are validated
// visually in seconds.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cd::editor::panel::scene_palette
{

// ---------------------------------------------------------------------------
// ScenePalette
// ---------------------------------------------------------------------------
class ScenePalette
{
public:
    // Default-constructible; starts with no palette and no selection.
    ScenePalette() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Point the panel at a live theme. Non-owning; caller manages lifetime.
    /// Pass nullptr to detach (draw() will emit only the background).
    void set_palette(const cd::ui::widgets::Theme* palette) noexcept;

    /// Returns the token name of the last clicked swatch, or std::nullopt
    /// when nothing has been selected.
    [[nodiscard]] std::optional<std::string> selected_token() const;

    // ---- Interaction helpers (testing + shell integration) ------------------

    /// Perform a hit-test at (x, y) in the same coordinate space as `bounds`.
    /// Selects the swatch under the click.  No-op when outside the panel
    /// or over an empty cell in the grid.
    void simulate_click(float x, float y, const cd::ui::widgets::Rect& bounds) noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (in order):
    ///   1. Panel background fill.
    ///   2. Swatch grid: one filled rect per token + selection highlight.
    ///
    /// `palette` is the theme to preview (same as the pointer held internally
    /// via set_palette() — provided explicitly here so callers can pass an
    /// interpolated / live-blended theme without storing a second pointer).
    ///
    /// Thread-safety: call from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   palette,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    // ---- Token descriptor ---------------------------------------------------
    // Each entry binds a human-readable name, its hex string, and a colour.
    struct TokenDesc
    {
        std::string_view       name;
        cd::ui::widgets::Color color;
    };

    // Build the ordered token list from a theme.
    [[nodiscard]] static constexpr std::size_t token_count() noexcept;

    // ---- Internal state -----------------------------------------------------
    const cd::ui::widgets::Theme* palette_  { nullptr };
    std::optional<std::string>    selected_ {};         ///< selected token name

    // ---- Layout constants ---------------------------------------------------
    static constexpr float kPad       =  8.0F;  ///< panel outer padding, px
    static constexpr float kCols      =  3.0F;  ///< number of swatch columns
    static constexpr float kSwatchH   = 52.0F;  ///< swatch cell height, px
    static constexpr float kGap       =  4.0F;  ///< gap between cells, px
    static constexpr float kBorderW   =  2.0F;  ///< selection border thickness

    // Derived: total token count (15 semantic slots).
    static constexpr std::size_t kTokenCount = 15U;

    // Compute the rect for swatch at index `i` given panel `bounds`.
    [[nodiscard]] static cd::ui::widgets::Rect
    swatch_rect(std::size_t i, const cd::ui::widgets::Rect& bounds) noexcept;

    // Build the ordered token list from a theme (fills `out`, length kTokenCount).
    static void fill_tokens(const cd::ui::widgets::Theme& theme,
                            TokenDesc                     out[kTokenCount]) noexcept;

    // Format a Color as "#RRGGBB" into `buf` (must be at least 8 chars).
    static void color_to_hex(cd::ui::widgets::Color c, char buf[8]) noexcept;
};

}  // namespace cd::editor::panel::scene_palette
