// =============================================================================
// CHROMODYNAMIC — cd/math/BarycentricInterp.hpp
// Phase 57.B / Wave 225 — barycentric attribute interpolation.
//
// Given barycentric weights (u, v, w) with u + v + w == 1 and per-
// vertex attribute samples a, b, c, the interpolated value is
// `u*a + v*b + w*c`. Template on T so callers interpolate float,
// Vec3f color, Vec3f normal, Vec2f UV, etc.
//
// Pairs with `cd::physics::barycentric` (Triangle.hpp) which produces
// the weights from a position; together they pick the color/normal/UV
// at any point inside a triangle.
//
// Requires `T` to support `T * float` and `T + T`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

namespace cd::math
{

template <class T>
[[nodiscard]] constexpr T barycentric_interp(
    const T& a, const T& b, const T& c,
    float u, float v, float w) noexcept
{
    return a * u + b * v + c * w;
}

/// Convenience overload that takes the three weights as a single vec3-
/// like with .x/.y/.z fields.
template <class T, class W>
[[nodiscard]] constexpr T barycentric_interp(
    const T& a, const T& b, const T& c, const W& uvw) noexcept
{
    return a * uvw.x + b * uvw.y + c * uvw.z;
}

}  // namespace cd::math
