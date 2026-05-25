// =============================================================================
// CHROMODYNAMIC — cd/light/Attenuation.hpp
// Phase 165 / v0.99.87 — physically-motivated distance + cone attenuation.
//
// Implements the same falloff every modern PBR engine uses (Frostbite
// 2014, Filament 1.x):
//
//   distance:  f(d) = saturate( 1 - (d / range)^4 )^2 / (d^2 + epsilon)
//
//   spot cone: f(theta) = saturate( (cos(theta) - cos_outer) /
//                                   (cos_inner - cos_outer) )^2
//
// The distance term is the "windowed inverse-square" from Lagarde &
// de Rousiers (2014). The cone term is smoothstep-squared so the
// derivative is continuous across the inner/outer angle boundary —
// no banding on the soft edge.
//
// Header-only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace cd::light
{

/// Frostbite windowed inverse-square distance attenuation. d is the
/// distance from light to shaded point (world units); range is the
/// light's `range` field. epsilon avoids singularity at d == 0.
[[nodiscard]] inline float
distance_attenuation(float d, float range, float epsilon = 0.01F) noexcept
{
    if (range <= 0.0F) return 0.0F;
    const float ratio = d / range;
    const float w = std::clamp(1.0F - ratio * ratio * ratio * ratio, 0.0F, 1.0F);
    return (w * w) / (d * d + epsilon);
}

/// Spot cone attenuation. `cos_theta` = dot(light_dir, light→surface).
/// `cos_inner` / `cos_outer` from the Light struct (pre-computed).
/// Returns 1 inside the inner cone, 0 outside the outer cone, smooth
/// in between.
[[nodiscard]] inline float
cone_attenuation(float cos_theta, float cos_inner, float cos_outer) noexcept
{
    if (cos_theta >= cos_inner) return 1.0F;
    if (cos_theta <= cos_outer) return 0.0F;
    const float denom = cos_inner - cos_outer;
    if (denom <= 1e-5F) return cos_theta >= cos_outer ? 1.0F : 0.0F;
    const float t = (cos_theta - cos_outer) / denom;
    return t * t;  // smoothstep-squared (Frostbite §3.1)
}

/// Convert luminous power (lumens) to radiant intensity that the
/// renderer can multiply with the shader BRDF. Per Frostbite §6.2.
/// For an omnidirectional point light: I = Phi / (4π).
[[nodiscard]] inline float lumens_to_point_intensity(float lumens) noexcept
{
    constexpr float k_inv_four_pi = 0.07957747F;  // 1 / (4π)
    return lumens * k_inv_four_pi;
}

/// For a spot light: I = Phi / (2π · (1 - cos(outer/2))).
/// Falls back to the point formula if the cone is malformed.
[[nodiscard]] inline float
lumens_to_spot_intensity(float lumens, float cos_outer) noexcept
{
    const float arg = 2.0F * 3.14159265358979F * (1.0F - cos_outer);
    return arg > 1e-5F ? lumens / arg : lumens_to_point_intensity(lumens);
}

/// Directional light: illuminance (lux) directly drives the shading
/// — no inverse-square. Returns input unchanged for API symmetry.
[[nodiscard]] inline float lux_to_directional_intensity(float lux) noexcept
{
    return lux;
}

}  // namespace cd::light
