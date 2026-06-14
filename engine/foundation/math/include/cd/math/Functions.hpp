// =============================================================================
// CHROMODYNAMIC — cd/math/Functions.hpp
// ADR-017 P4 (Sprint S2.7) — scalar utility functions.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Constants.hpp>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace cd::math
{

template <class T>
[[nodiscard]] constexpr T deg_to_rad(T deg) noexcept
{
    return deg * (pi_v<T> / T { 180 });
}

template <class T>
[[nodiscard]] constexpr T rad_to_deg(T rad) noexcept
{
    return rad * (T { 180 } / pi_v<T>);
}

template <class T>
[[nodiscard]] constexpr T clamp(T v, T lo, T hi) noexcept
{
    return std::clamp(v, lo, hi);
}

template <class T>
[[nodiscard]] constexpr T saturate(T v) noexcept
{
    return clamp(v, T { 0 }, T { 1 });
}

template <class T>
[[nodiscard]] constexpr T lerp(T a, T b, T t) noexcept
{
    return a + (b - a) * t;
}

template <class T>
[[nodiscard]] constexpr T inverse_lerp(T a, T b, T v) noexcept
{
    return (v - a) / (b - a);
}

template <class T>
[[nodiscard]] constexpr T remap(T from_lo, T from_hi, T to_lo, T to_hi, T v) noexcept
{
    return lerp(to_lo, to_hi, inverse_lerp(from_lo, from_hi, v));
}

template <class T>
[[nodiscard]] constexpr T smoothstep(T edge0, T edge1, T x) noexcept
{
    const T t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (T { 3 } - T { 2 } * t);
}

/// Equality with an absolute tolerance. Use for floats only.
template <class T>
[[nodiscard]] constexpr bool approx_equal(T a, T b, T tol = epsilon_v<T> * T { 16 }) noexcept
{
    static_assert(std::is_floating_point_v<T>);
    const T diff = a - b;
    const T abs_diff = diff < T { 0 } ? -diff : diff;
    return abs_diff <= tol;
}

template <class T>
[[nodiscard]] constexpr int sign(T v) noexcept
{
    return (T { 0 } < v) - (v < T { 0 });
}

/// Henyey-Greenstein phase function (Henyey & Greenstein 1941,
/// "Diffuse Radiation in the Galaxy").
///
/// @param cos_theta  Dot product of the incident and scattered directions
///                   (both unit vectors, pointing away from the scattering
///                   point), i.e. cos(angle between light and view rays).
/// @param g          Asymmetry parameter g ∈ (-1, +1).
///                   g > 0 → forward-scatter (water droplets, Mie aerosols),
///                   g = 0 → isotropic,  g < 0 → back-scatter.
/// @return           Phase value in sr⁻¹.
///
/// The guard `max(denom, 1e-6)` prevents a singularity at g → 1 ∧ cos_theta → 1.
/// For typical Mie g ≤ 0.95 and any cos_theta the guard never fires
/// (minimum denom ≈ 0.0025 at g=0.95, cos_theta=1), so golden / render
/// output is unaffected.
[[nodiscard]] inline float henyey_greenstein(float cos_theta, float g) noexcept
{
    const float g2    = g * g;
    const float denom = 1.0F + g2 - 2.0F * g * cos_theta;
    return (1.0F - g2) / (4.0F * pi_v<float> * std::pow(std::max(denom, 1.0e-6F), 1.5F));
}

}  // namespace cd::math
