// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_light_editor/src/LightEditor.cpp
//
// phase664 — cd::editor::panel::light_editor  implementation
// =============================================================================
#include <cd/editor/panel_light_editor/LightEditor.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace cd::editor::panel::light_editor
{

// ---------------------------------------------------------------------------
// State API
// ---------------------------------------------------------------------------

void LightEditor::set_lights(
    std::span<const cd::render::lighting_clusters::PointLight> lights)
{
    lights_.assign(lights.begin(), lights.end());
    // Invalidate selection if out of range after resize.
    if (selected_.has_value() && *selected_ >= lights_.size())
        selected_.reset();
}

void LightEditor::set_grid(
    const cd::render::lighting_clusters::ClusterGrid& grid) noexcept
{
    grid_ = grid;
}

std::optional<std::size_t> LightEditor::selected_light() const noexcept
{
    return selected_;
}

void LightEditor::simulate_click(
    float x, float y, const cd::ui::widgets::Rect& bounds) noexcept
{
    // Outside panel?
    if (x < bounds.x || x > bounds.x + bounds.w ||
        y < bounds.y || y > bounds.y + bounds.h)
    {
        return;
    }

    const float rel_y = y - bounds.y;

    for (std::size_t i = 0; i < lights_.size(); ++i)
    {
        const float top = row_top(i);
        if (rel_y >= top && rel_y < top + kRowH)
        {
            selected_ = i;
            return;
        }
    }
    // Click landed in the gap or stats section — keep existing selection.
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void LightEditor::draw(
    cd::ui::renderer::DrawBatcher& batcher,
    const cd::ui::widgets::Theme&  theme,
    const cd::ui::widgets::Rect&   bounds) const
{
    // ---- 1. Panel background ------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    const float row_w = bounds.w - 2.0F * kPad;

    // ---- 2. Accent separator bar --------------------------------------------
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    // ---- 3. Light rows ------------------------------------------------------
    // Layout per row:
    //   [colour swatch 14px] [gap 4px] [x-strip] [gap 2px] [y-strip] [gap 2px]
    //   [z-strip] [gap 4px] [radius-strip] [gap 4px] [intensity-strip]
    constexpr float kSwatchW   = 14.0F;
    constexpr float kSwatchGap = 4.0F;
    constexpr float kStripGap  = 2.0F;
    const float     avail_w    = row_w - kSwatchW - kSwatchGap;
    // 5 strips (X, Y, Z, radius, intensity) with 4 gaps between them.
    const float kStripW = (avail_w - 4.0F * kStripGap) / 5.0F;

    for (std::size_t i = 0; i < lights_.size(); ++i)
    {
        const auto& light = lights_[i];
        const float ry    = bounds.y + row_top(i);
        const bool  sel   = selected_.has_value() && *selected_ == i;

        // Row highlight for selected entry.
        if (sel)
        {
            batcher.quad(bounds.x + kPad, ry,
                         row_w, kRowH,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             60U });
        }

        // Colour swatch — uses linear RGB from PointLight::color.
        const auto  cr      = static_cast<std::uint8_t>(
                                  std::clamp(light.color[0], 0.0F, 1.0F) * 255.0F);
        const auto  cg      = static_cast<std::uint8_t>(
                                  std::clamp(light.color[1], 0.0F, 1.0F) * 255.0F);
        const auto  cb      = static_cast<std::uint8_t>(
                                  std::clamp(light.color[2], 0.0F, 1.0F) * 255.0F);
        batcher.quad(bounds.x + kPad, ry + 4.0F,
                     kSwatchW, kRowH - 8.0F,
                     cd::ui::renderer::Color { cr, cg, cb, 255U });

        // Strips start after swatch.
        const float sx = bounds.x + kPad + kSwatchW + kSwatchGap;

        // Helper: draw a background + filled portion for a normalised value [0,1].
        auto draw_strip = [&](float offset_x, float norm_fill,
                              cd::ui::renderer::Color fill_color)
        {
            batcher.quad(sx + offset_x, ry + 3.0F,
                         kStripW, kRowH - 6.0F,
                         cd::ui::renderer::Color {
                             theme.surface_hover.r,
                             theme.surface_hover.g,
                             theme.surface_hover.b,
                             theme.surface_hover.a });
            const float fw = std::clamp(norm_fill, 0.0F, 1.0F) * kStripW;
            if (fw > 0.0F)
            {
                batcher.quad(sx + offset_x, ry + 3.0F,
                             fw, kRowH - 6.0F,
                             fill_color);
            }
        };

        // World-position strips (X=red, Y=green, Z=blue; range ±200 m → [0,1]).
        constexpr float kPosRange = 200.0F;
        draw_strip(0.0F,
                   (light.position[0] + kPosRange) / (2.0F * kPosRange),
                   cd::ui::renderer::Color { 200U, 80U, 80U, 200U });

        draw_strip(kStripW + kStripGap,
                   (light.position[1] + kPosRange) / (2.0F * kPosRange),
                   cd::ui::renderer::Color { 80U, 200U, 80U, 200U });

        draw_strip((kStripW + kStripGap) * 2.0F,
                   (light.position[2] + kPosRange) / (2.0F * kPosRange),
                   cd::ui::renderer::Color { 80U, 80U, 200U, 200U });

        // Radius strip (range 0..50 m).
        constexpr float kRadiusMax = 50.0F;
        draw_strip((kStripW + kStripGap) * 3.0F,
                   light.radius / kRadiusMax,
                   cd::ui::renderer::Color { 180U, 140U, 60U, 200U });

        // Intensity strip (range 0..10000 cd — luminous intensity).
        constexpr float kIntensityMax = 10000.0F;
        draw_strip((kStripW + kStripGap) * 4.0F,
                   light.intensity / kIntensityMax,
                   cd::ui::renderer::Color { 220U, 220U, 100U, 200U });
    }

    // ---- 4. Cluster grid stats section --------------------------------------
    // Drawn below the light list with a separator + three cell-count bars
    // (X, Y, Z tiles) and near/far depth indicators.
    const float stats_y    = bounds.y + row_top(lights_.size()) + kPad * 2.0F;
    const float stats_avail = bounds.y + bounds.h - stats_y - kPad;

    if (stats_avail <= 0.0F)
        return;  // Not enough space for stats.

    // Thin separator before stats.
    batcher.quad(bounds.x + kPad, stats_y,
                 row_w, 2.0F,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     80U });

    // Three cell-count bars (X / Y / Z tiles; each normalised to [0..64] max).
    constexpr float kStatRowH  = 10.0F;
    constexpr float kStatGap   = 3.0F;
    constexpr float kMaxTiles  = 64.0F;
    const float     stat_strip = (row_w - 2.0F * kStripGap) / 3.0F;
    const float     sy0        = stats_y + 6.0F;

    auto draw_stat_bar = [&](float ox, float norm,
                             cd::ui::renderer::Color color)
    {
        batcher.quad(bounds.x + kPad + ox, sy0,
                     stat_strip, kStatRowH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r,
                         theme.surface_hover.g,
                         theme.surface_hover.b,
                         theme.surface_hover.a });
        const float fw = std::clamp(norm, 0.0F, 1.0F) * stat_strip;
        if (fw > 0.0F)
            batcher.quad(bounds.x + kPad + ox, sy0, fw, kStatRowH, color);
    };

    draw_stat_bar(0.0F,
                  static_cast<float>(grid_.x_tiles) / kMaxTiles,
                  cd::ui::renderer::Color { 200U, 80U, 80U, 180U });

    draw_stat_bar(stat_strip + kStripGap,
                  static_cast<float>(grid_.y_tiles) / kMaxTiles,
                  cd::ui::renderer::Color { 80U, 200U, 80U, 180U });

    draw_stat_bar((stat_strip + kStripGap) * 2.0F,
                  static_cast<float>(grid_.z_slices) / kMaxTiles,
                  cd::ui::renderer::Color { 80U, 80U, 200U, 180U });

    // Near/far depth indicators: two thin horizontal bars at the bottom of the
    // stats area.  Near normalised to [0..10 m], far to [0..10000 m].
    const float nf_y0 = sy0 + kStatRowH + kStatGap;
    if (nf_y0 + kStatRowH <= bounds.y + bounds.h - kPad)
    {
        // Near-plane bar (narrow range 0..10 m, accent colour).
        constexpr float kNearMax = 10.0F;
        const float near_norm = std::clamp(grid_.near_plane / kNearMax, 0.0F, 1.0F);
        batcher.quad(bounds.x + kPad, nf_y0,
                     near_norm * row_w, kStatRowH,
                     cd::ui::renderer::Color {
                         theme.accent.r,
                         theme.accent.g,
                         theme.accent.b,
                         120U });

        // Far-plane bar (wide range 0..10000 m, dim accent).
        const float nf_y1 = nf_y0 + kStatRowH + kStatGap;
        if (nf_y1 + kStatRowH <= bounds.y + bounds.h - kPad)
        {
            constexpr float kFarMax = 10000.0F;
            const float far_norm = std::clamp(grid_.far_plane / kFarMax, 0.0F, 1.0F);
            batcher.quad(bounds.x + kPad, nf_y1,
                         far_norm * row_w, kStatRowH,
                         cd::ui::renderer::Color {
                             theme.accent.r,
                             theme.accent.g,
                             theme.accent.b,
                             60U });
        }
    }
}

}  // namespace cd::editor::panel::light_editor
