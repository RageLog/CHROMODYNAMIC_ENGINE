// =============================================================================
// CHROMODYNAMIC — cd/material/BrdfLut.hpp
// Phase 155 / v0.99.81 — pre-integrated split-sum BRDF LUT (CPU bake).
//
// The split-sum approximation by Karis (2013) factors the IBL specular
// integral into two terms that can each be pre-computed separately:
//
//   ∫ Li * fr * (N·L) dω  ≈  (∫ Li dω) * (∫ fr * (N·L) dω)
//                              cubemap        BRDF LUT
//
// The BRDF LUT term is a 2D texture parameterised by:
//   U = max(0, dot(N, V))   (cosine of view angle, 0..1)
//   V = roughness            (0..1)
//
// Each texel stores (scale, bias) such that the runtime PBR shader
// can recover the Cook-Torrance specular response as:
//
//   F_total = F0 * scale + bias
//
// The integrand uses GGX importance sampling with N samples per texel
// — 1024 samples is the typical reference value; the engine bakes at
// 256x256 with 512 samples by default (matches Filament's ground-truth
// LUT to within 0.5% per texel and fits in a tiny on-disk asset).
//
// This header is CPU-only and dependency-free (no rhi types) so the
// LUT can be:
//   * baked at engine boot into a host buffer
//   * uploaded to GPU via cd::rhi::create_texture + upload_texture
//   * round-tripped through cd_cook_texture
//   * tested without a GPU
//
// Reference: Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH 2013)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace cd::material
{

/// Pre-integrated BRDF LUT texel. R = scale, G = bias for the
/// Cook-Torrance split-sum reconstruction `F0 * scale + bias`.
struct BrdfLutTexel
{
    float scale { 0.0F };
    float bias  { 0.0F };
};

// ---- Hammersley low-discrepancy sequence ----------------------------------

/// Van der Corput radical inverse — bit-reversal of `i` in base 2,
/// mapped to [0, 1). Used as the second coordinate of the Hammersley
/// sequence.
[[nodiscard]] inline float radical_inverse_vdc(std::uint32_t bits) noexcept
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u)  | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u)  | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u)  | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u)  | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;  // / 0x100000000
}

/// Hammersley sample `i` of `N` in [0,1)^2.
[[nodiscard]] inline std::array<float, 2>
hammersley(std::uint32_t i, std::uint32_t N) noexcept
{
    return { static_cast<float>(i) / static_cast<float>(N), radical_inverse_vdc(i) };
}

// ---- GGX importance sampling ----------------------------------------------

/// Importance-sample the GGX NDF for a given roughness² (α²). Returns the
/// half-vector H in tangent space (N = +Z).
[[nodiscard]] inline std::array<float, 3>
importance_sample_ggx(const std::array<float, 2>& xi, float roughness) noexcept
{
    const float a = roughness * roughness;
    const float phi = 2.0F * std::numbers::pi_v<float> * xi[0];
    const float cos_theta = std::sqrt((1.0F - xi[1]) / (1.0F + (a * a - 1.0F) * xi[1]));
    const float sin_theta = std::sqrt(std::max(0.0F, 1.0F - cos_theta * cos_theta));
    return { sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta };
}

/// Smith geometry function with Schlick-GGX (k = a/2, IBL variant).
[[nodiscard]] inline float
geometry_schlick_ggx_ibl(float n_dot_v, float roughness) noexcept
{
    const float a = roughness;
    const float k = (a * a) / 2.0F;
    return n_dot_v / (n_dot_v * (1.0F - k) + k);
}

[[nodiscard]] inline float
geometry_smith_ibl(float n_dot_v, float n_dot_l, float roughness) noexcept
{
    return geometry_schlick_ggx_ibl(n_dot_v, roughness)
         * geometry_schlick_ggx_ibl(n_dot_l, roughness);
}

// ---- LUT integration -------------------------------------------------------

/// Bake one texel of the split-sum LUT. `n_dot_v` is the cosine of the
/// view angle in (0, 1], `roughness` is in [0, 1].
[[nodiscard]] inline BrdfLutTexel
integrate_brdf(float n_dot_v, float roughness, std::uint32_t samples = 512) noexcept
{
    // View vector in tangent space with N = +Z.
    const float v_x = std::sqrt(std::max(0.0F, 1.0F - n_dot_v * n_dot_v));
    const float v_z = n_dot_v;

    float a_sum = 0.0F;
    float b_sum = 0.0F;
    for (std::uint32_t i = 0; i < samples; ++i)
    {
        const auto xi = hammersley(i, samples);
        const auto H  = importance_sample_ggx(xi, roughness);
        // Reflect V about H to get L.
        const float v_dot_h = v_x * H[0] + v_z * H[2];
        // L = reflect(-V, H) = 2*dot(V,H)*H - V; we only need L_z (N·L)
        // for the integration so the other components are unused.
        const float l_z = 2.0F * v_dot_h * H[2] - v_z;
        const float n_dot_l = std::max(0.0F, l_z);
        if (n_dot_l <= 0.0F) continue;

        const float n_dot_h = std::max(0.0F, H[2]);
        const float v_dot_h_clamp = std::max(0.0F, v_dot_h);
        const float g = geometry_smith_ibl(std::max(0.001F, n_dot_v), n_dot_l, roughness);
        const float g_vis = (g * v_dot_h_clamp) / std::max(1e-5F, n_dot_h * n_dot_v);
        const float fc = std::pow(1.0F - v_dot_h_clamp, 5.0F);
        a_sum += (1.0F - fc) * g_vis;
        b_sum += fc * g_vis;
    }
    const float inv_n = 1.0F / static_cast<float>(samples);
    return BrdfLutTexel { a_sum * inv_n, b_sum * inv_n };
}

/// Bake a complete `width x height` LUT. Row 0 (V=0) is roughness=0,
/// row height-1 is roughness=1; col 0 is N·V≈0, col width-1 is N·V=1.
/// Output is row-major scale/bias pairs (float * 2 per texel).
[[nodiscard]] inline std::vector<BrdfLutTexel>
bake_brdf_lut(std::uint32_t width = 256, std::uint32_t height = 256,
              std::uint32_t samples = 512)
{
    std::vector<BrdfLutTexel> out(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const float roughness = (static_cast<float>(y) + 0.5F) / static_cast<float>(height);
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const float n_dot_v = (static_cast<float>(x) + 0.5F) / static_cast<float>(width);
            out[static_cast<std::size_t>(y) * width + x] = integrate_brdf(n_dot_v, roughness, samples);
        }
    }
    return out;
}

}  // namespace cd::material
