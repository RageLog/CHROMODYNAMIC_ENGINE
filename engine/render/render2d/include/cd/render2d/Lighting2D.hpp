// =============================================================================
// CHROMODYNAMIC — cd/render2d/Lighting2D.hpp
// FAZ 34 (ADR-20261111) — Dynamic 2D Point/Directional Lighting, 2D Normal-Map
// Lambertian Shading & Analytical Soft-Penumbra Occluder Shadowing.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>
#include <cd/render2d/Camera2D.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cd::render2d
{

struct ColorRgba8
{
    std::uint8_t r { 255U };
    std::uint8_t g { 255U };
    std::uint8_t b { 255U };
    std::uint8_t a { 255U };

    [[nodiscard]] static constexpr ColorRgba8 from_floats(float rf, float gf, float bf, float af = 1.0F) noexcept
    {
        const auto to_u8 = [](float v) constexpr noexcept -> std::uint8_t {
            const float c = v < 0.0F ? 0.0F : (v > 1.0F ? 1.0F : v);
            return static_cast<std::uint8_t>(c * 255.0F + 0.5F);
        };
        return { to_u8(rf), to_u8(gf), to_u8(bf), to_u8(af) };
    }
};

struct PointLight2D
{
    cd::math::Vec2f position { 0.0F, 0.0F };
    cd::math::Vec3f color { 1.0F, 0.85F, 0.55F };
    float           intensity { 1.4F };
    float           radius { 7.5F };
    float           falloff_exp { 1.6F };
    float           z_height { 1.25F };
    bool            cast_shadows { true };
    bool            enabled { true };
};

struct ShadowOccluder2D
{
    cd::math::Vec2f center { 0.0F, 0.0F };
    cd::math::Vec2f half_extents { 0.5F, 0.5F };
    float           opacity { 0.88F };
    bool            enabled { true };
};

struct LightingEnvironment2D
{
    cd::math::Vec3f               ambient_color { 0.24F, 0.28F, 0.38F };
    float                         ambient_intensity { 1.0F };
    cd::math::Vec3f               directional_dir { -0.35F, -0.75F, 0.55F };
    cd::math::Vec3f               directional_color { 0.85F, 0.90F, 1.0F };
    float                         directional_intensity { 0.35F };
    bool                          enable_lighting { true };
    std::vector<PointLight2D>     point_lights {};
    std::vector<ShadowOccluder2D> occluders {};
};

/// Slab ray-AABB intersection in 2D for segment P0 -> P1, returning soft shadow visibility [0..1].
[[nodiscard]] inline float compute_soft_shadow_visibility_2d(
    cd::math::Vec2f                light_pos,
    cd::math::Vec2f                sample_pos,
    const std::vector<ShadowOccluder2D>& occluders,
    float                          self_ignore_radius = 0.08F) noexcept
{
    const float dx   = sample_pos.x - light_pos.x;
    const float dy   = sample_pos.y - light_pos.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist <= 1.0e-4F)
    {
        return 1.0F;
    }

    float visibility = 1.0F;
    for (const auto& occ : occluders)
    {
        if (!occ.enabled || occ.opacity <= 0.0F)
        {
            continue;
        }
        // Skip occluder if the sample point itself lies inside or on the boundary of this occluder
        // (so lit wall tiles still receive front-face illumination instead of self-occluding).
        if (std::fabs(sample_pos.x - occ.center.x) <= occ.half_extents.x + self_ignore_radius &&
            std::fabs(sample_pos.y - occ.center.y) <= occ.half_extents.y + self_ignore_radius)
        {
            continue;
        }

        const cd::math::Vec2f bmin { occ.center.x - occ.half_extents.x, occ.center.y - occ.half_extents.y };
        const cd::math::Vec2f bmax { occ.center.x + occ.half_extents.x, occ.center.y + occ.half_extents.y };

        float tmin = 0.0F;
        float tmax = 1.0F;
        bool  hit  = true;

        if (std::fabs(dx) < 1.0e-6F)
        {
            if (light_pos.x < bmin.x || light_pos.x > bmax.x)
            {
                hit = false;
            }
        }
        else
        {
            const float inv_dx = 1.0F / dx;
            float       t1     = (bmin.x - light_pos.x) * inv_dx;
            float       t2     = (bmax.x - light_pos.x) * inv_dx;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) hit = false;
        }

        if (hit)
        {
            if (std::fabs(dy) < 1.0e-6F)
            {
                if (light_pos.y < bmin.y || light_pos.y > bmax.y)
                {
                    hit = false;
                }
            }
            else
            {
                const float inv_dy = 1.0F / dy;
                float       t1     = (bmin.y - light_pos.y) * inv_dy;
                float       t2     = (bmax.y - light_pos.y) * inv_dy;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) hit = false;
            }
        }

        if (hit && tmax > 0.02F && tmin < 0.96F)
        {
            // Soft penumbra: grazing rays (small chord thickness tmax - tmin) attenuate softly.
            const float chord_len = (tmax - tmin) * dist;
            const float penumbra  = std::clamp(chord_len * 2.4F, 0.25F, 1.0F);
            visibility *= (1.0F - std::clamp(occ.opacity * penumbra, 0.0F, 0.95F));
        }
    }
    return std::clamp(visibility, 0.0F, 1.0F);
}

/// Evaluates combined 2D ambient + directional + dynamic point lights + soft shadows at `world_pos`.
[[nodiscard]] inline ColorRgba8 evaluate_lighting_2d(
    const LightingEnvironment2D& env,
    cd::math::Vec2f              world_pos,
    cd::math::Vec3f              surface_normal,
    ColorRgba8                   albedo) noexcept
{
    if (!env.enable_lighting)
    {
        return albedo;
    }

    const float n_len = std::sqrt(
        surface_normal.x * surface_normal.x +
        surface_normal.y * surface_normal.y +
        surface_normal.z * surface_normal.z);
    const cd::math::Vec3f n = (n_len > 1.0e-5F)
        ? cd::math::Vec3f { surface_normal.x / n_len, surface_normal.y / n_len, surface_normal.z / n_len }
        : cd::math::Vec3f { 0.0F, 0.0F, 1.0F };

    cd::math::Vec3f total_light {
        env.ambient_color.x * env.ambient_intensity,
        env.ambient_color.y * env.ambient_intensity,
        env.ambient_color.z * env.ambient_intensity,
    };

    if (env.directional_intensity > 0.0F)
    {
        const float dl_len = std::sqrt(
            env.directional_dir.x * env.directional_dir.x +
            env.directional_dir.y * env.directional_dir.y +
            env.directional_dir.z * env.directional_dir.z);
        if (dl_len > 1.0e-5F)
        {
            const cd::math::Vec3f l_to_sun {
                -env.directional_dir.x / dl_len,
                -env.directional_dir.y / dl_len,
                env.directional_dir.z / dl_len,
            };
            const float ndotl = std::max(0.15F, n.x * l_to_sun.x + n.y * l_to_sun.y + n.z * l_to_sun.z);
            total_light.x += env.directional_color.x * env.directional_intensity * ndotl;
            total_light.y += env.directional_color.y * env.directional_intensity * ndotl;
            total_light.z += env.directional_color.z * env.directional_intensity * ndotl;
        }
    }

    for (const auto& pl : env.point_lights)
    {
        if (!pl.enabled || pl.intensity <= 0.0F || pl.radius <= 0.01F)
        {
            continue;
        }
        const float dx      = pl.position.x - world_pos.x;
        const float dy      = pl.position.y - world_pos.y;
        const float dist_2d = std::sqrt(dx * dx + dy * dy);
        if (dist_2d >= pl.radius)
        {
            continue;
        }

        const float norm_d = dist_2d / pl.radius;
        const float atten  = std::pow(std::max(0.0F, 1.0F - norm_d), std::max(0.5F, pl.falloff_exp));

        const float dz      = std::max(0.15F, pl.z_height);
        const float inv_3d  = 1.0F / std::sqrt(dx * dx + dy * dy + dz * dz);
        const float ndotl   = std::max(0.0F, (n.x * dx + n.y * dy + n.z * dz) * inv_3d);

        float vis = 1.0F;
        if (pl.cast_shadows && !env.occluders.empty())
        {
            vis = compute_soft_shadow_visibility_2d(pl.position, world_pos, env.occluders);
        }

        const float contrib = pl.intensity * atten * ndotl * vis;
        total_light.x += pl.color.x * contrib;
        total_light.y += pl.color.y * contrib;
        total_light.z += pl.color.z * contrib;
    }

    // Soft shoulder tone-curve so bright overlapping neon lights saturate gracefully without harsh clipping
    const auto tone = [](float hdr) noexcept -> float {
        return hdr / (1.0F + 0.28F * std::max(0.0F, hdr - 0.85F));
    };

    const float base_r = static_cast<float>(albedo.r) / 255.0F;
    const float base_g = static_cast<float>(albedo.g) / 255.0F;
    const float base_b = static_cast<float>(albedo.b) / 255.0F;
    const float base_a = static_cast<float>(albedo.a) / 255.0F;

    return ColorRgba8::from_floats(
        tone(base_r * total_light.x),
        tone(base_g * total_light.y),
        tone(base_b * total_light.z),
        base_a);
}

}  // namespace cd::render2d
