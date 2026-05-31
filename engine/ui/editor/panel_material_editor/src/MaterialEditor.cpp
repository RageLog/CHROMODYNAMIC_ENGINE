// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_material_editor/src/MaterialEditor.cpp
//
// phase556-557-558 — cd::editor::panel::material_editor  implementation
// =============================================================================
#include <cd/editor/panel_material_editor/MaterialEditor.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cd::editor::panel::material_editor
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void MaterialEditor::set_material_id(MaterialId id) noexcept
{
    material_id_ = id;
}

MaterialId MaterialEditor::material_id() const noexcept
{
    return material_id_;
}

void MaterialEditor::set_base_color(float r, float g, float b) noexcept
{
    base_color_r_ = std::clamp(r, 0.0F, 1.0F);
    base_color_g_ = std::clamp(g, 0.0F, 1.0F);
    base_color_b_ = std::clamp(b, 0.0F, 1.0F);
}

float MaterialEditor::base_color_r() const noexcept { return base_color_r_; }
float MaterialEditor::base_color_g() const noexcept { return base_color_g_; }
float MaterialEditor::base_color_b() const noexcept { return base_color_b_; }

void MaterialEditor::set_metallic(float value) noexcept
{
    metallic_ = std::clamp(value, 0.0F, 1.0F);
}

float MaterialEditor::metallic() const noexcept { return metallic_; }

void MaterialEditor::set_roughness(float value) noexcept
{
    roughness_ = std::clamp(value, 0.0F, 1.0F);
}

float MaterialEditor::roughness() const noexcept { return roughness_; }

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void MaterialEditor::draw(cd::ui::renderer::DrawBatcher& batcher,
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

    constexpr float kRowH   = 20.0F;
    constexpr float kPad    = 6.0F;
    constexpr float kBarH   = 4.0F;
    const float     row_w   = bounds.w - 2.0F * kPad;

    // Separator bar under the title area (accent colour).
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    float cursor_y = bounds.y + kPad * 2.0F + kBarH;

    // Helper: draw a labelled single-channel fill bar.
    // `value` is already in [0..1]. `fill_col` is the bar tint.
    auto draw_param_row = [&](float value,
                              cd::ui::renderer::Color fill_col)
    {
        // Label placeholder (accent bar on the left).
        batcher.quad(bounds.x + kPad, cursor_y,
                     6.0F, kRowH,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         200U });

        // Track background.
        const float track_x = bounds.x + kPad + 6.0F + kPad * 0.5F;
        const float track_w = row_w - 6.0F - kPad;
        batcher.quad(track_x, cursor_y,
                     track_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });

        // Fill.
        const float fill_w = track_w * std::clamp(value, 0.0F, 1.0F);
        if (fill_w > 0.0F)
        {
            batcher.quad(track_x, cursor_y,
                         fill_w, kRowH,
                         fill_col);
        }

        cursor_y += kRowH + kPad;
    };

    // ---- Row 1: Base colour — three sub-strips (R / G / B) -----------------
    // Render the three channels as separate narrow bars side-by-side.
    {
        // Label placeholder.
        batcher.quad(bounds.x + kPad, cursor_y,
                     6.0F, kRowH,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         200U });

        const float track_x  = bounds.x + kPad + 6.0F + kPad * 0.5F;
        const float track_w  = row_w - 6.0F - kPad;
        const float strip_w  = (track_w - 4.0F) / 3.0F;

        // R strip.
        batcher.quad(track_x, cursor_y,
                     strip_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });
        if (base_color_r_ > 0.0F)
        {
            batcher.quad(track_x, cursor_y,
                         strip_w * base_color_r_, kRowH,
                         cd::ui::renderer::Color { 220U, 70U, 70U, 220U });
        }

        // G strip.
        const float gx = track_x + strip_w + 2.0F;
        batcher.quad(gx, cursor_y,
                     strip_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });
        if (base_color_g_ > 0.0F)
        {
            batcher.quad(gx, cursor_y,
                         strip_w * base_color_g_, kRowH,
                         cd::ui::renderer::Color { 70U, 220U, 70U, 220U });
        }

        // B strip.
        const float bx = track_x + (strip_w + 2.0F) * 2.0F;
        batcher.quad(bx, cursor_y,
                     strip_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });
        if (base_color_b_ > 0.0F)
        {
            batcher.quad(bx, cursor_y,
                         strip_w * base_color_b_, kRowH,
                         cd::ui::renderer::Color { 70U, 70U, 220U, 220U });
        }

        cursor_y += kRowH + kPad;
    }

    // ---- Row 2: Metallic (grey tint) ---------------------------------------
    draw_param_row(metallic_,
                   cd::ui::renderer::Color { 180U, 180U, 200U, 220U });

    // ---- Row 3: Roughness (warm tint) --------------------------------------
    draw_param_row(roughness_,
                   cd::ui::renderer::Color { 200U, 160U, 80U, 220U });

    // ---- Live preview rectangle --------------------------------------------
    // Shows the base-colour tint as a solid swatch. Height = 2 * kRowH so it
    // reads clearly even at small panel widths.
    constexpr float kPreviewH = kRowH * 2.0F;
    const float     preview_x = bounds.x + kPad;
    const float     preview_w = row_w;

    // Preview background (slightly darker than surface so a pure-white tint
    // is distinguishable from the panel background).
    batcher.quad(preview_x, cursor_y,
                 preview_w, kPreviewH,
                 cd::ui::renderer::Color {
                     theme.background.r,
                     theme.background.g,
                     theme.background.b,
                     theme.background.a });

    // Tint overlay: alpha = 200 so the background still bleeds through slightly
    // and gives a sense of the "gamma" of the material colour.
    const auto tint_r = static_cast<std::uint8_t>(
        std::clamp(std::lround(base_color_r_ * 255.0F), 0L, 255L));
    const auto tint_g = static_cast<std::uint8_t>(
        std::clamp(std::lround(base_color_g_ * 255.0F), 0L, 255L));
    const auto tint_b = static_cast<std::uint8_t>(
        std::clamp(std::lround(base_color_b_ * 255.0F), 0L, 255L));

    batcher.quad(preview_x, cursor_y,
                 preview_w, kPreviewH,
                 cd::ui::renderer::Color { tint_r, tint_g, tint_b, 200U });
}

}  // namespace cd::editor::panel::material_editor
