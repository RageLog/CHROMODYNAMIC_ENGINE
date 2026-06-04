// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_material_preview/src/MaterialPreview.cpp
//
// phase678 — cd::editor::panel::material_preview implementation
// =============================================================================
#include <cd/editor/panel_material_preview/MaterialPreview.hpp>

#include <cd/material/AlphaMode.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cd::editor::panel::material_preview
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void MaterialPreview::set_material(
    const cd::asset::material_authoring::AuthoredMaterial* mat) noexcept
{
    mat_ = mat;
}

const cd::asset::material_authoring::AuthoredMaterial*
MaterialPreview::material() const noexcept
{
    return mat_;
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void MaterialPreview::draw(cd::ui::renderer::DrawBatcher& batcher,
                           const cd::ui::widgets::Theme&  theme,
                           const cd::ui::widgets::Rect&   bounds) const
{
    // ---- 1. Background fill -------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    constexpr float kPad  = 6.0F;
    constexpr float kBarH = 4.0F;
    const float     row_w = bounds.w - 2.0F * kPad;

    // ---- 2. Accent separator bar --------------------------------------------
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    float cursor_y = bounds.y + kPad * 2.0F + kBarH;

    // Early out if no material is bound.
    if (mat_ == nullptr)
        return;

    // ---- Helpers ------------------------------------------------------------

    // Convert a normalised float [0..1] to a uint8 channel.
    auto to_u8 = [](float v) -> std::uint8_t
    {
        const long rounded = std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F);
        return static_cast<std::uint8_t>(std::clamp(rounded, 0L, 255L));
    };

    constexpr float kRowH = 20.0F;

    // Draw a labelled fill-bar row (track + fill).
    //   cursor_y is advanced by (kRowH + kPad) after the call.
    auto draw_fill_bar = [&](float value,
                             cd::ui::renderer::Color fill_col)
    {
        // Label accent pip on the left.
        batcher.quad(bounds.x + kPad, cursor_y,
                     6.0F, kRowH,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         200U });

        const float track_x = bounds.x + kPad + 6.0F + kPad * 0.5F;
        const float track_w = row_w - 6.0F - kPad;

        // Track background.
        batcher.quad(track_x, cursor_y,
                     track_w, kRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });

        // Fill (non-zero values only).
        const float fill_w = track_w * std::clamp(value, 0.0F, 1.0F);
        if (fill_w > 0.0F)
        {
            batcher.quad(track_x, cursor_y,
                         fill_w, kRowH,
                         fill_col);
        }

        cursor_y += kRowH + kPad;
    };

    // ---- 3. Sphere swatch ---------------------------------------------------
    // Sprint-1: a square quad tinted with base_color.  Sprint-2 will replace
    // this with a real PBR sphere rendered via cd::material UI variant pipeline.
    constexpr float kSphereH = 80.0F;
    const float     sphere_x = bounds.x + kPad;
    const float     sphere_w = row_w;

    // Dark background so even a pure-black tint is distinguishable.
    batcher.quad(sphere_x, cursor_y,
                 sphere_w, kSphereH,
                 cd::ui::renderer::Color {
                     theme.background.r,
                     theme.background.g,
                     theme.background.b,
                     theme.background.a });

    // Tint overlay at alpha=210 — the darker background bleeds through to give
    // a crude sense of the sphere "edge" fade.
    {
        const std::uint8_t tint_r = to_u8(mat_->base_color[0]);
        const std::uint8_t tint_g = to_u8(mat_->base_color[1]);
        const std::uint8_t tint_b = to_u8(mat_->base_color[2]);
        batcher.quad(sphere_x, cursor_y,
                     sphere_w, kSphereH,
                     cd::ui::renderer::Color { tint_r, tint_g, tint_b, 210U });
    }

    cursor_y += kSphereH + kPad;

    // ---- 4. Metallic bar (accent_success tint) -------------------------------
    // accent_success: green-ish — full bar = fully metallic (conductor).
    draw_fill_bar(mat_->metallic,
                  cd::ui::renderer::Color { 80U, 200U, 120U, 220U });

    // ---- 5. Roughness bar (accent_warning tint) ------------------------------
    // accent_warning: amber-ish — full bar = maximally rough (Lambertian).
    draw_fill_bar(mat_->roughness,
                  cd::ui::renderer::Color { 220U, 160U, 60U, 220U });

    // ---- 6. Alpha cutoff bar (error tint — kMask only) ----------------------
    if (mat_->alpha_mode == cd::material::AlphaMode::kMask)
    {
        // Error tint: red-ish — conveys "fragments below this threshold are cut".
        draw_fill_bar(mat_->alpha_cutoff,
                      cd::ui::renderer::Color { 210U, 60U, 60U, 220U });
    }

    // ---- 7. Texture path pills ----------------------------------------------
    // Each path is shown as a thin quad: present = accent colour, absent = grey.
    constexpr float kPillH  = 10.0F;
    constexpr float kPillGap = 4.0F;

    auto draw_path_pill = [&](bool present)
    {
        const cd::ui::renderer::Color col = present
            ? cd::ui::renderer::Color { theme.accent.r, theme.accent.g,
                                        theme.accent.b, 180U }
            : cd::ui::renderer::Color { 90U, 90U, 90U, 120U };

        batcher.quad(bounds.x + kPad, cursor_y, row_w, kPillH, col);
        cursor_y += kPillH + kPillGap;
    };

    draw_path_pill(!mat_->albedo_texture_path.empty());
    draw_path_pill(!mat_->normal_texture_path.empty());
    draw_path_pill(!mat_->mr_texture_path.empty());
}

}  // namespace cd::editor::panel::material_preview
