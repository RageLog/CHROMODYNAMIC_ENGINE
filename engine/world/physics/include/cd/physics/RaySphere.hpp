// =============================================================================
// CHROMODYNAMIC — cd/physics/RaySphere.hpp
// Phase 38.A / Wave 206 — ray/sphere analytic intersection.
//
// Solves |O + tD - C|² = r² for t, where (O, D) are the ray and
// (C, r) the sphere. Quadratic discriminant:
//
//   a = D·D     (== 1 if direction is unit)
//   b = 2·(O - C)·D
//   c = (O - C)·(O - C) - r²
//   Δ = b² - 4ac
//
// Returns the smaller non-negative t (entry hit) when Δ ≥ 0; if both
// roots are negative the sphere is behind the origin and we return
// std::nullopt. Inside-the-sphere rays return t = 0.
//
// Direction is NOT auto-normalized — caller controls "t in world units"
// vs "t in direction lengths". Match the existing `intersect_ray_aabb`
// convention.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Ray.hpp>
#include <cd/physics/Sphere.hpp>

#include <cmath>
#include <optional>

namespace cd::physics
{

[[nodiscard]] inline std::optional<float>
intersect_ray_sphere(const Ray& r, const Sphere& s) noexcept
{
    const cd::math::Vec3f oc { r.origin.x - s.center.x,
                               r.origin.y - s.center.y,
                               r.origin.z - s.center.z };
    const float a = r.direction.x * r.direction.x
                  + r.direction.y * r.direction.y
                  + r.direction.z * r.direction.z;
    if (a < 1e-12F) return std::nullopt;   // zero-length direction
    const float b = 2.0F * (oc.x * r.direction.x + oc.y * r.direction.y + oc.z * r.direction.z);
    const float c = oc.x * oc.x + oc.y * oc.y + oc.z * oc.z - s.radius * s.radius;
    const float disc = b * b - 4.0F * a * c;
    if (disc < 0.0F) return std::nullopt;

    const float sq = std::sqrt(disc);
    const float inv2a = 0.5F / a;
    const float t0 = (-b - sq) * inv2a;
    const float t1 = (-b + sq) * inv2a;

    if (t0 >= 0.0F) return t0;
    if (t1 >= 0.0F) return 0.0F;          // origin inside the sphere
    return std::nullopt;                  // sphere fully behind
}

}  // namespace cd::physics
