// =============================================================================
// CHROMODYNAMIC — cd/math/Plane.hpp
// Phase 23.A / Wave 188 — header-only plane primitive.
//
// Plane in Hessian normal form: n·p + d = 0. Used for frustum
// classification and clip-space tests.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::math
{

struct Plane
{
    Vec3f normal { 0.0F, 1.0F, 0.0F };  ///< Must be unit length.
    float d { 0.0F };                    ///< Signed distance from origin.
};

[[nodiscard]] inline float signed_distance(const Plane& p, const Vec3f& point) noexcept
{
    return p.normal.x * point.x + p.normal.y * point.y + p.normal.z * point.z + p.d;
}

/// Returns -1 (point is on the negative side of the plane), 0 (on
/// the plane within `eps`), or +1 (positive side).
[[nodiscard]] inline int classify_point(const Plane& p, const Vec3f& point,
                                         float eps = 1e-4F) noexcept
{
    const float s = signed_distance(p, point);
    if (s > eps)  return 1;
    if (s < -eps) return -1;
    return 0;
}

}  // namespace cd::math
