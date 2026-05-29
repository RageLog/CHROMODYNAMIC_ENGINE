// =============================================================================
// CHROMODYNAMIC — cd/render/volumetric/Fog.hpp
// Phase 8 / Sprint 11 / Wave 91 — volumetric fog CPU baseline.
//
// Single-scattering volumetric fog along a ray, with:
//   * Beer-Lambert extinction:  T(t) = exp(-σ_t · t)  for homogeneous σ_t
//   * Henyey-Greenstein phase:  p(cos θ) = (1 - g²) / (4π · (1 + g² - 2g·cos θ)^1.5)
//   * Numerical ray-march integration with N segments (caller-tuneable).
//
// All math in linear RGB. Density (σ_t) and in-scattering coefficient
// (σ_s) are scalar for the homogeneous case; heterogeneous volumes can
// sample a callback per segment in the `integrate_along` overload.
//
// Header-only. Depends on cd::core + cd::math + standard library.
//
// References:
//   * Henyey, Greenstein 1941 — "Diffuse Radiation in the Galaxy".
//   * Hillaire 2015 — "Towards Unified and Physically-Based Volumetric
//     Lighting in Frostbite" (HPG, slide 30 — homogeneous fog).
//   * Hoffman/Preetham 2002 — "Rendering Outdoor Light Scattering in
//     Real Time" (canonical reference for the integral form).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace cd::render::volumetric
{

inline constexpr float kPi { 3.14159265F };

struct FogParams
{
    /// Extinction coefficient σ_t in 1/m. Beer-Lambert: T = exp(-σ_t · d).
    float extinction { 0.05F };
    /// Scattering coefficient σ_s in 1/m. Energy-conserving: σ_s ≤ σ_t.
    float scattering { 0.04F };
    /// In-scatter colour (linear RGB), pre-multiplied with light intensity.
    cd::math::Vec3f albedo { 0.8F, 0.8F, 0.85F };
    /// Henyey-Greenstein asymmetry parameter g ∈ (-1, +1).
    /// g > 0 → forward scatter (water droplets), g < 0 → back scatter,
    /// g = 0 → isotropic.
    float anisotropy { 0.4F };
};

/// Henyey-Greenstein phase function. `cos_theta` is the dot product of
/// the light direction with the view direction (both unit). Returns
/// the phase value (1/sr).
[[nodiscard]] inline float henyey_greenstein(float cos_theta, float g) noexcept
{
    const float g2 = g * g;
    const float denom = 1.0F + g2 - 2.0F * g * cos_theta;
    return (1.0F - g2) / (4.0F * kPi * std::pow(std::max(denom, 1.0e-6F), 1.5F));
}

/// Beer-Lambert transmittance along a homogeneous segment of length
/// `distance` with extinction `sigma_t`. Returns T ∈ (0, 1].
[[nodiscard]] inline float transmittance(float sigma_t, float distance) noexcept
{
    return std::exp(-std::max(0.0F, sigma_t * distance));
}

/// In-scattered radiance integrated along a ray of length `distance`
/// through homogeneous fog. `light_dir` is the unit direction TOWARD
/// the light (in the same space as `view_dir`); `light_color` is the
/// directional light's pre-attenuated radiance.
///
/// Closed-form for homogeneous Beer-Lambert + constant in-scatter:
///     L_in = σ_s · phase(cos θ) · L_light · (1 - T) / σ_t
/// where T = exp(-σ_t · distance). Derived by integrating
///     ∫₀ᵈ σ_s · phase · L · exp(-σ_t · t) dt = ...
[[nodiscard]] inline cd::math::Vec3f in_scattering(
    const FogParams& params,
    const cd::math::Vec3f& view_dir,
    const cd::math::Vec3f& light_dir,
    const cd::math::Vec3f& light_color,
    float distance) noexcept
{
    const float cos_theta = cd::math::dot(view_dir, light_dir);
    const float phase = henyey_greenstein(cos_theta, params.anisotropy);
    const float T = transmittance(params.extinction, distance);
    const float denom = std::max(params.extinction, 1.0e-4F);
    const float scale = params.scattering * phase * (1.0F - T) / denom;
    return cd::math::Vec3f { params.albedo.x * light_color.x * scale,
                             params.albedo.y * light_color.y * scale,
                             params.albedo.z * light_color.z * scale };
}

/// Numerical ray-march for heterogeneous volumes. `sigma_t_at(t)` is
/// called per segment to return the local extinction at distance `t`
/// from the ray origin. `light_dir` and `light_color` apply uniformly
/// (sun / directional light). Returns the integrated in-scattered
/// radiance. `steps` = number of segments (caller balances cost vs.
/// banding).
template <class SigmaFn>
[[nodiscard]] inline cd::math::Vec3f integrate_along(
    const FogParams& params,
    const cd::math::Vec3f& view_dir,
    const cd::math::Vec3f& light_dir,
    const cd::math::Vec3f& light_color,
    float distance,
    SigmaFn sigma_t_at,
    std::uint32_t steps = 16)
{
    if (distance <= 0.0F || steps == 0)
        return { 0.0F, 0.0F, 0.0F };
    const float dt = distance / static_cast<float>(steps);
    const float cos_theta = cd::math::dot(view_dir, light_dir);
    const float phase = henyey_greenstein(cos_theta, params.anisotropy);

    cd::math::Vec3f L { 0.0F, 0.0F, 0.0F };
    float T = 1.0F;
    for (std::uint32_t i = 0; i < steps; ++i)
    {
        const float t = (static_cast<float>(i) + 0.5F) * dt;
        const float sigma_t = std::max(0.0F, sigma_t_at(t));
        // Use the params.scattering / extinction ratio as the local
        // single-scatter albedo (homogeneous fog approximation; full
        // heterogeneous would also vary σ_s independently).
        const float ratio = sigma_t > 0.0F ? params.scattering / std::max(params.extinction, 1.0e-4F)
                                            : 0.0F;
        const float sigma_s_local = ratio * sigma_t;

        const float segment_T = std::exp(-sigma_t * dt);
        const float seg_in = sigma_s_local * phase * (1.0F - segment_T) / std::max(sigma_t, 1.0e-4F);
        L.x += params.albedo.x * light_color.x * seg_in * T;
        L.y += params.albedo.y * light_color.y * seg_in * T;
        L.z += params.albedo.z * light_color.z * seg_in * T;
        T *= segment_T;
    }
    return L;
}

}  // namespace cd::render::volumetric
