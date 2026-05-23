// =============================================================================
// CHROMODYNAMIC — cd/ibl/BrdfLut.hpp
// Phase 5 / S4.6.b — image-based lighting building blocks (CPU-side bake).
//
// Provides two functions for "split-sum" IBL:
//
//   bake_brdf_lut(width, height) → RG-Float texture (caller-side upload).
//   Stores (scale, bias) for the F0 contribution as a function of
//   (NdotV, roughness). Used in the fragment shader:
//
//       vec2 env_brdf = texture(brdf_lut, vec2(NdotV, roughness)).rg;
//       vec3 specular = (F0 * env_brdf.x + env_brdf.y) * prefiltered_env_color;
//
//   integrate_irradiance(env_color) → Vec3f (ambient indirect for one
//   normal direction). The full irradiance cube map is the convolution
//   of this for every normal direction; a sample can call this per face
//   for a small N×N cube map at startup.
//
// Compute-shader implementations of the same convolutions (real-time
// IBL) are the Phase 6 follow-up. The CPU bake is enough for shipped
// content (do it once, embed in the .pak, never recompute).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace cd::ibl
{

/// 2-channel float texture: out[(y*w + x)*2 + 0] = scale, +1 = bias.
struct BrdfLut
{
    std::uint32_t width { 0 };
    std::uint32_t height { 0 };
    std::vector<float> rg;  // size = width * height * 2
};

namespace detail
{

constexpr float kPi = 3.14159265358979F;

/// Hammersley low-discrepancy sequence (van der Corput base 2).
[[nodiscard]] inline cd::math::Vec2f hammersley(std::uint32_t i, std::uint32_t n) noexcept
{
    std::uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    const float radical_inv = static_cast<float>(bits) * 2.3283064365386963e-10F;
    return cd::math::Vec2f { static_cast<float>(i) / static_cast<float>(n), radical_inv };
}

/// Importance-sampled half-vector around (0,0,1) for GGX with roughness α².
[[nodiscard]] inline cd::math::Vec3f
sample_ggx(cd::math::Vec2f xi, float roughness, const cd::math::Vec3f& normal) noexcept
{
    const float a = roughness * roughness;
    const float phi = 2.0F * kPi * xi.x;
    const float cos_theta = std::sqrt((1.0F - xi.y) / (1.0F + (a * a - 1.0F) * xi.y));
    const float sin_theta = std::sqrt(1.0F - cos_theta * cos_theta);

    // Spherical → cartesian
    const cd::math::Vec3f h_tangent { std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta };

    // Build orthonormal basis around `normal`.
    const cd::math::Vec3f up
        = std::abs(normal.z) < 0.999F ? cd::math::Vec3f { 0.0F, 0.0F, 1.0F } : cd::math::Vec3f { 1.0F, 0.0F, 0.0F };
    const cd::math::Vec3f tangent {
        up.y * normal.z - up.z * normal.y,
        up.z * normal.x - up.x * normal.z,
        up.x * normal.y - up.y * normal.x,
    };
    const float tlen = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
    const cd::math::Vec3f t { tangent.x / tlen, tangent.y / tlen, tangent.z / tlen };
    const cd::math::Vec3f bitangent {
        normal.y * t.z - normal.z * t.y,
        normal.z * t.x - normal.x * t.z,
        normal.x * t.y - normal.y * t.x,
    };
    // Mat3 * h_tangent
    return cd::math::Vec3f {
        t.x * h_tangent.x + bitangent.x * h_tangent.y + normal.x * h_tangent.z,
        t.y * h_tangent.x + bitangent.y * h_tangent.y + normal.y * h_tangent.z,
        t.z * h_tangent.x + bitangent.z * h_tangent.y + normal.z * h_tangent.z,
    };
}

[[nodiscard]] inline float g_schlick_ggx(float n_dot_v, float roughness) noexcept
{
    const float k = (roughness * roughness) / 2.0F;
    return n_dot_v / (n_dot_v * (1.0F - k) + k);
}

[[nodiscard]] inline float g_smith(float n_dot_v, float n_dot_l, float roughness) noexcept
{
    return g_schlick_ggx(n_dot_v, roughness) * g_schlick_ggx(n_dot_l, roughness);
}

}  // namespace detail

/// Bake the split-sum BRDF LUT. Recommended size: 256x256 with 1024
/// samples. Smaller LUTs trade quality for bake time + cache footprint.
/// 256x256 with 1024 samples bakes in ~50ms on a desktop CPU and is the
/// canonical Karis/Frostbite reference target.
[[nodiscard]] inline BrdfLut bake_brdf_lut(std::uint32_t width = 256,
                                           std::uint32_t height = 256,
                                           std::uint32_t samples = 1024)
{
    BrdfLut lut;
    lut.width = width;
    lut.height = height;
    lut.rg.resize(static_cast<std::size_t>(width) * height * 2);

    for (std::uint32_t y = 0; y < height; ++y)
    {
        const float roughness = (static_cast<float>(y) + 0.5F) / static_cast<float>(height);
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const float n_dot_v = (static_cast<float>(x) + 0.5F) / static_cast<float>(width);
            const cd::math::Vec3f v {
                std::sqrt(1.0F - n_dot_v * n_dot_v),
                0.0F,
                n_dot_v,
            };
            const cd::math::Vec3f n { 0.0F, 0.0F, 1.0F };

            float a = 0.0F;  // scale
            float b = 0.0F;  // bias
            for (std::uint32_t i = 0; i < samples; ++i)
            {
                const auto xi = detail::hammersley(i, samples);
                const auto h = detail::sample_ggx(xi, roughness, n);
                const float v_dot_h = std::max(v.x * h.x + v.y * h.y + v.z * h.z, 0.0F);
                const cd::math::Vec3f l {
                    2.0F * v_dot_h * h.x - v.x,
                    2.0F * v_dot_h * h.y - v.y,
                    2.0F * v_dot_h * h.z - v.z,
                };
                const float n_dot_l = std::max(l.z, 0.0F);
                if (n_dot_l <= 0.0F)
                    continue;
                const float n_dot_h = std::max(h.z, 0.0F);
                const float g = detail::g_smith(n_dot_v, n_dot_l, roughness);
                const float g_vis = (g * v_dot_h) / (n_dot_h * n_dot_v + 1e-7F);
                const float fc = std::pow(1.0F - v_dot_h, 5.0F);
                a += (1.0F - fc) * g_vis;
                b += fc * g_vis;
            }
            const float inv_s = 1.0F / static_cast<float>(samples);
            lut.rg[(static_cast<std::size_t>(y) * width + x) * 2 + 0] = a * inv_s;
            lut.rg[(static_cast<std::size_t>(y) * width + x) * 2 + 1] = b * inv_s;
        }
    }
    return lut;
}

/// Analytical irradiance for a single normal direction against a single
/// directional light. Real applications convolve every direction against
/// a full environment map; this single-direction variant is the building
/// block. Use case:
///   for each face/texel of a target irradiance cube:
///     env_color = sample_skybox(world_direction)
///     irradiance += integrate_irradiance_directional(world_direction,
///                                                    light_dir,
///                                                    env_color)
[[nodiscard]] inline cd::math::Vec3f
integrate_irradiance_directional(const cd::math::Vec3f& normal,
                                 const cd::math::Vec3f& light_dir,
                                 const cd::math::Vec3f& light_color) noexcept
{
    const float ndotl = std::max(normal.x * light_dir.x + normal.y * light_dir.y + normal.z * light_dir.z, 0.0F);
    return cd::math::Vec3f { light_color.x * ndotl, light_color.y * ndotl, light_color.z * ndotl };
}

}  // namespace cd::ibl
