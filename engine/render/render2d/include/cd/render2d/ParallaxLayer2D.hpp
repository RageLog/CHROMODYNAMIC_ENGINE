// =============================================================================
// CHROMODYNAMIC — cd/render2d/ParallaxLayer2D.hpp
// Milestone 3 — Composable Multi-Layer 2D Parallax Data Structure with
// Independent Scroll Factors, Auto-Scroll Velocities, Depth Sorting,
// and Procedural / Textured Canvas Emission.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>
#include <cd/render2d/Camera2D.hpp>
#include <cd/render2d/SortKey2D.hpp>
#include <cd/render2d/SpriteBatch2D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::render2d
{

enum class ParallaxProceduralStyle : std::uint8_t
{
    kNone              = 0,
    kCitySkyline       = 1,
    kMountainPinnacle  = 2,
    kForestSilhouettes = 3,
    kCloudClusters     = 4,
};

struct ParallaxLayer2D
{
    std::string             name { "Layer" };
    cd::math::Vec2f         scroll_factor { 0.5F, 0.5F };
    cd::math::Vec2f         auto_scroll_velocity { 0.0F, 0.0F };
    cd::math::Vec2f         current_offset { 0.0F, 0.0F };
    cd::math::Vec2f         tile_size { 16.0F, 9.0F };
    std::uint32_t           texture_slot { 0xFFFFFFFFU };
    UvRect2D                uv_rect { 0.0F, 0.0F, 1.0F, 1.0F };
    ColorRgba8              tint { 255U, 255U, 255U, 255U };
    std::uint8_t            layer { 10U };
    std::int16_t            z_order { 0 };
    std::int16_t            z_index { 0 };
    bool                    visible { true };
    bool                    repeat_x { true };
    bool                    repeat_y { false };
    ParallaxProceduralStyle procedural_style { ParallaxProceduralStyle::kNone };
};

class ParallaxManager2D
{
public:
    void add_layer(ParallaxLayer2D layer)
    {
        layers_.push_back(std::move(layer));
    }

    void clear() noexcept { layers_.clear(); }
    [[nodiscard]] std::span<const ParallaxLayer2D> layers() const noexcept { return layers_; }
    [[nodiscard]] std::span<ParallaxLayer2D> mutable_layers() noexcept { return layers_; }

    [[nodiscard]] ParallaxLayer2D* find_layer(std::string_view name) noexcept
    {
        for (auto& l : layers_)
        {
            if (l.name == name)
            {
                return &l;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const ParallaxLayer2D* find_layer(std::string_view name) const noexcept
    {
        for (const auto& l : layers_)
        {
            if (l.name == name)
            {
                return &l;
            }
        }
        return nullptr;
    }

    void sort_layers() noexcept
    {
        std::stable_sort(layers_.begin(), layers_.end(), [](const ParallaxLayer2D& a, const ParallaxLayer2D& b) noexcept {
            const auto za = a.z_order != 0 ? a.z_order : a.z_index;
            const auto zb = b.z_order != 0 ? b.z_order : b.z_index;
            return za < zb;
        });
    }

    [[nodiscard]] cd::math::Vec2f compute_effective_offset(
        const ParallaxLayer2D& layer,
        cd::math::Vec2f        cam_pos) const noexcept
    {
        return {
            cam_pos.x * layer.scroll_factor.x + layer.current_offset.x,
            cam_pos.y * layer.scroll_factor.y + layer.current_offset.y,
        };
    }

    void update(float dt) noexcept
    {
        for (auto& l : layers_)
        {
            l.current_offset.x += l.auto_scroll_velocity.x * dt;
            l.current_offset.y += l.auto_scroll_velocity.y * dt;
        }
    }

    void step(float dt) noexcept { update(dt); }

    void emit_to_canvas(
        Canvas2D&             canvas,
        const Camera2D&       camera,
        const ViewportRect2D& viewport) const
    {
        if (viewport.width <= 1.0F || viewport.height <= 1.0F)
        {
            return;
        }

        const cd::math::Vec2f cam_pos = camera.render_eye_world();

        for (const auto& l : layers_)
        {
            if (!l.visible)
            {
                continue;
            }

            const cd::math::Vec2f eff = compute_effective_offset(l, cam_pos);
            const std::int16_t eff_z = l.z_order != 0 ? l.z_order : l.z_index;
            const std::uint64_t key = make_sort_key_2d(
                l.layer,
                eff_z,
                0.0F,
                false,
                static_cast<std::uint16_t>(l.texture_slot & 0xFFFFU));

            if (l.texture_slot != 0xFFFFFFFFU)
            {
                // Tiled texture parallax
                const float tile_w = std::max(16.0F, l.tile_size.x * camera.effective_scale_px_per_unit());
                const float tile_h = std::max(16.0F, l.tile_size.y * camera.effective_scale_px_per_unit());
                const float shift_x = l.repeat_x ? std::fmod(eff.x * 24.0F, tile_w) : eff.x;
                const float shift_y = l.repeat_y ? std::fmod(eff.y * 24.0F, tile_h) : eff.y;

                const int count_x = l.repeat_x ? static_cast<int>(std::ceil(viewport.width / tile_w)) + 2 : 1;
                const int count_y = l.repeat_y ? static_cast<int>(std::ceil(viewport.height / tile_h)) + 2 : 1;

                for (int iy = -1; iy < count_y; ++iy)
                {
                    for (int ix = -1; ix < count_x; ++ix)
                    {
                        const float px = viewport.x + static_cast<float>(ix) * tile_w - shift_x;
                        const float py = viewport.y + static_cast<float>(iy) * tile_h - shift_y;
                        canvas.add_textured_rect(px, py, tile_w, tile_h, l.texture_slot, l.uv_rect, l.tint, key);
                    }
                }
            }
            else if (l.procedural_style == ParallaxProceduralStyle::kCitySkyline)
            {
                const float step = 140.0F;
                const float par_shift = std::fmod(eff.x * 12.0F, step);
                const int cols = static_cast<int>(std::ceil(viewport.width / step)) + 3;
                for (int i = -2; i < cols; ++i)
                {
                    const float bx = viewport.x + static_cast<float>(i) * step - par_shift;
                    const float bh = 55.0F + static_cast<float>(((i * 37) & 7) * 16);
                    canvas.add_solid_rect(
                        bx, viewport.y + viewport.height - bh, 96.0F, bh,
                        l.tint, key);
                }
            }
            else if (l.procedural_style == ParallaxProceduralStyle::kMountainPinnacle)
            {
                const float step = 200.0F;
                const float par_shift = std::fmod(eff.x * 8.0F, step);
                const int cols = static_cast<int>(std::ceil(viewport.width / step)) + 3;
                for (int i = -2; i < cols; ++i)
                {
                    const float bx = viewport.x + static_cast<float>(i) * step - par_shift;
                    const float peak_h = 120.0F + static_cast<float>(((i * 53) & 7) * 20);
                    canvas.add_solid_rect(
                        bx, viewport.y + viewport.height - peak_h, 160.0F, peak_h,
                        l.tint, key);
                }
            }
        }
    }

private:
    std::vector<ParallaxLayer2D> layers_ {};
};

using ParallaxStack2D = ParallaxManager2D;

}  // namespace cd::render2d
