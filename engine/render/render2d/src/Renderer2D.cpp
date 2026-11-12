// =============================================================================
// CHROMODYNAMIC — cd/render2d/Renderer2D.cpp
// FAZ 34 (ADR-20261111) — Complete 2D Viewport/Game Scene Renderer.
// =============================================================================
#include <cd/render2d/Renderer2D.hpp>

#include <algorithm>
#include <cmath>

namespace cd::render2d
{

void Renderer2D::render_scene(
    Canvas2D&                        out_canvas,
    const ViewportRect2D&            viewport,
    const Camera2D&                  camera,
    const Tilemap2D&                 tilemap,
    const LightingEnvironment2D&     lighting,
    std::span<const Sprite2D>        sprites,
    std::span<const CollisionRect2D> debug_colliders,
    const Render2DDebugOptions&      options,
    std::span<const ParallaxLayer2D> parallax_layers) const
{
    out_canvas.clear(viewport);
    if (viewport.width <= 2.0F || viewport.height <= 2.0F)
    {
        return;
    }

    // 1. Atmospheric Sky Gradient + Distant Parallax Silhouette Layer (Layer 0..10)
    const std::uint64_t sky_key = make_sort_key_2d(0U, -100, 0.0F, false, 0U);
    out_canvas.add_gradient_rect(
        viewport.x, viewport.y, viewport.width, viewport.height,
        options.sky_top_color, options.sky_bottom_color, false, sky_key);

    if (!parallax_layers.empty())
    {
        ParallaxManager2D mgr {};
        for (const auto& l : parallax_layers)
        {
            mgr.add_layer(l);
        }
        mgr.emit_to_canvas(out_canvas, camera, viewport);
    }
    else
    {
        // Subtle parallax skyline towers so camera movement feels deep & kinetic
        const cd::math::Vec2f eye         = camera.render_eye_world();
        const std::uint64_t   parallax_k1 = make_sort_key_2d(5U, 0, 0.0F, false, 0U);
        const float           par_shift   = std::fmod(eye.x * 12.0F, 140.0F);
        for (int i = -2; i < 16; ++i)
        {
            const float bx = viewport.x + static_cast<float>(i) * 140.0F - par_shift;
            const float bh = 55.0F + static_cast<float>(((i * 37) & 7) * 16);
            out_canvas.add_solid_rect(
                bx, viewport.y + viewport.height - bh, 96.0F, bh,
                ColorRgba8 { 20U, 30U, 52U, 155U }, parallax_k1);
        }
    }

    // 2. World Coordinate Grid (Layer 15 — Editor Mode)
    if (options.show_grid)
    {
        const Aabb2D        wb      = camera.visible_world_bounds(viewport, 0.0F);
        const float         step    = std::max(0.5F, tilemap.tile_size());
        const float         scale   = camera.effective_scale_px_per_unit();
        const std::uint64_t grid_k  = make_sort_key_2d(15U, 0, 0.0F, false, 0U);
        const std::uint64_t axis_k  = make_sort_key_2d(16U, 0, 0.0F, false, 0U);

        if (scale * step >= 8.0F)
        {
            const int gx0 = static_cast<int>(std::floor(wb.min.x / step));
            const int gx1 = static_cast<int>(std::ceil(wb.max.x / step));
            const int gy0 = static_cast<int>(std::floor(wb.min.y / step));
            const int gy1 = static_cast<int>(std::ceil(wb.max.y / step));

            for (int gx = gx0; gx <= gx1; ++gx)
            {
                const float wx    = static_cast<float>(gx) * step;
                const float sx    = std::round(camera.world_to_screen({ wx, 0.0F }, viewport).x);
                const bool  major = (gx % 5 == 0);
                const ColorRgba8 c = (gx == 0)
                    ? ColorRgba8 { 85U, 210U, 130U, 145U }
                    : (major ? ColorRgba8 { 95U, 120U, 165U, 58U } : ColorRgba8 { 80U, 100U, 140U, 28U });
                out_canvas.add_solid_rect(sx, viewport.y, (gx == 0) ? 2.0F : 1.0F, viewport.height, c, (gx == 0) ? axis_k : grid_k);
            }
            for (int gy = gy0; gy <= gy1; ++gy)
            {
                const float wy    = static_cast<float>(gy) * step;
                const float sy    = std::round(camera.world_to_screen({ 0.0F, wy }, viewport).y);
                const bool  major = (gy % 5 == 0);
                const ColorRgba8 c = (gy == 0)
                    ? ColorRgba8 { 235U, 95U, 105U, 155U }
                    : (major ? ColorRgba8 { 95U, 120U, 165U, 58U } : ColorRgba8 { 80U, 100U, 140U, 28U });
                out_canvas.add_solid_rect(viewport.x, sy, viewport.width, (gy == 0) ? 2.0F : 1.0F, c, (gy == 0) ? axis_k : grid_k);
            }
        }
    }

    // 3. Soft Volumetric 2D Light Halos (Layer 35 — behind tiles & sprites)
    if (lighting.enable_lighting)
    {
        const Aabb2D        wb      = camera.visible_world_bounds(viewport, 2.0F);
        const std::uint64_t halo_k  = make_sort_key_2d(35U, 0, 0.0F, false, 0U);
        const float         px_unit = camera.effective_scale_px_per_unit();
        for (const auto& pl : lighting.point_lights)
        {
            if (!pl.enabled || pl.intensity <= 0.0F || pl.radius <= 0.05F)
            {
                continue;
            }
            const Aabb2D lb {
                .min = { pl.position.x - pl.radius, pl.position.y - pl.radius },
                .max = { pl.position.x + pl.radius, pl.position.y + pl.radius },
            };
            if (!lb.overlaps(wb))
            {
                continue;
            }
            const cd::math::Vec2f sp = camera.world_to_screen(pl.position, viewport);
            const float alpha = std::clamp(0.24F * pl.intensity, 0.04F, 0.42F);
            out_canvas.add_radial_glow(
                sp.x, sp.y, pl.radius * px_unit,
                ColorRgba8::from_floats(pl.color.x, pl.color.y, pl.color.z, alpha),
                20, halo_k);
        }
    }

    // 4. Chunked Wang Auto-Tiled Tilemap (Layer 60)
    tilemap.emit_to_canvas(out_canvas, camera, lighting, 60U);

    // 5. Sprites, Flipbook Characters, Collectibles & Enemies (Layer 100+ with 64-Bit Z/Y-Sort)
    for (const auto& sp : sprites)
    {
        emit_sprite_2d(out_canvas, camera, lighting, sp);
    }

    // 6. Debug Overlays: Greedy Merged Physics Colliders, Light Gizmos & Camera Safe-Area Frame (Layer 230)
    if (options.show_greedy_colliders)
    {
        const std::uint64_t col_k = make_sort_key_2d(230U, 0, 0.0F, false, 0U);
        for (const auto& rc : debug_colliders)
        {
            const cd::math::Vec2f tl = camera.world_to_screen(
                { rc.center.x - rc.half_extents.x, rc.center.y + rc.half_extents.y }, viewport);
            const cd::math::Vec2f br = camera.world_to_screen(
                { rc.center.x + rc.half_extents.x, rc.center.y - rc.half_extents.y }, viewport);
            const ColorRgba8 wire = rc.hazard
                ? ColorRgba8 { 255U, 80U, 95U, 225U }
                : (rc.one_way ? ColorRgba8 { 255U, 210U, 75U, 225U } : ColorRgba8 { 65U, 245U, 130U, 220U });
            out_canvas.add_rect_outline(tl.x, tl.y, std::max(2.0F, br.x - tl.x), std::max(2.0F, br.y - tl.y), 1.8F, wire, col_k);
        }
    }

    if (options.show_light_gizmos && lighting.enable_lighting)
    {
        const std::uint64_t lg_k = make_sort_key_2d(232U, 0, 0.0F, false, 0U);
        for (const auto& pl : lighting.point_lights)
        {
            if (!pl.enabled) continue;
            const cd::math::Vec2f sp  = camera.world_to_screen(pl.position, viewport);
            const ColorRgba8      col = ColorRgba8::from_floats(pl.color.x, pl.color.y, pl.color.z, 0.95F);
            out_canvas.add_solid_rect(sp.x - 4.0F, sp.y - 4.0F, 8.0F, 8.0F, col, lg_k);
            out_canvas.add_rect_outline(sp.x - 7.0F, sp.y - 7.0F, 14.0F, 14.0F, 1.4F,
                                        ColorRgba8 { 255U, 255U, 255U, 200U }, lg_k + 1ULL);
        }
    }

    if (options.show_camera_frame)
    {
        // 16:9 Gameplay Safe-Frame Guide in Editor Mode
        const std::uint64_t cf_k = make_sort_key_2d(235U, 0, 0.0F, false, 0U);
        const float         fw   = viewport.width * 0.86F;
        const float         fh   = std::min(viewport.height * 0.86F, fw * (9.0F / 16.0F));
        const float         fx   = viewport.x + (viewport.width - fw) * 0.5F;
        const float         fy   = viewport.y + (viewport.height - fh) * 0.5F;
        out_canvas.add_rect_outline(fx, fy, fw, fh, 1.0F, ColorRgba8 { 95U, 210U, 255U, 65U }, cf_k);
    }

    out_canvas.sort_commands();
}

}  // namespace cd::render2d
