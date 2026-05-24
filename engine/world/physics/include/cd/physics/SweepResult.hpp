// =============================================================================
// CHROMODYNAMIC — cd/physics/SweepResult.hpp
// Phase 53.A / Wave 221 — continuous-collision query result.
//
// A "sweep" advances a moving primitive (sphere, capsule, …) along a
// linear motion vector and reports the first impact: time-of-impact
// (TOI) ∈ [0, 1], the hit position, and the surface normal.
//
//   SweepResult { hit, toi, point, normal }
//   * hit    — true if any contact within the sweep.
//   * toi    — fraction of the motion at impact (0 = start, 1 = end).
//   * point  — world-space contact point.
//   * normal — outward surface normal at the contact (unit).
//
// `sweep_sphere_aabb(start, dir, sphere, aabb)` is a sample sweep test
// (treats the AABB inflated by sphere.radius then ray-vs-AABB).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>
#include <cd/physics/Ray.hpp>
#include <cd/physics/Sphere.hpp>

#include <algorithm>
#include <optional>

namespace cd::physics
{

struct SweepResult
{
    bool             hit { false };
    float            toi { 1.0F };
    cd::math::Vec3f  point  {};
    cd::math::Vec3f  normal {};
};

/// Sphere of radius `r` starting at `start`, moving by `dir` (full
/// motion vector, NOT unit length), sweeps against `aabb`. Returns a
/// SweepResult with `hit=true` and `toi ∈ [0, 1]` on contact.
[[nodiscard]] inline SweepResult sweep_sphere_aabb(
    const cd::math::Vec3f& start,
    const cd::math::Vec3f& dir,
    float radius,
    const Aabb& aabb) noexcept
{
    SweepResult out;
    // Minkowski-sum trick: expand the AABB by the sphere's radius then
    // ray-cast from the sphere center against the expanded AABB.
    const Aabb expanded {
        cd::math::Vec3f { aabb.min.x - radius, aabb.min.y - radius, aabb.min.z - radius },
        cd::math::Vec3f { aabb.max.x + radius, aabb.max.y + radius, aabb.max.z + radius },
    };
    const Ray r { start, dir };
    auto t = intersect_ray_aabb(r, expanded);
    if (!t.has_value()) return out;
    if (*t > 1.0F) return out;   // contact past the end of motion
    out.hit = true;
    out.toi = std::max(0.0F, *t);
    out.point = cd::math::Vec3f {
        start.x + dir.x * out.toi,
        start.y + dir.y * out.toi,
        start.z + dir.z * out.toi,
    };
    // Normal estimate: which face of the expanded AABB did we exit?
    // Pick the axis whose entry plane is closest to `point`.
    const float dx_min = std::abs(out.point.x - expanded.min.x);
    const float dx_max = std::abs(out.point.x - expanded.max.x);
    const float dy_min = std::abs(out.point.y - expanded.min.y);
    const float dy_max = std::abs(out.point.y - expanded.max.y);
    const float dz_min = std::abs(out.point.z - expanded.min.z);
    const float dz_max = std::abs(out.point.z - expanded.max.z);
    const float min_d = std::min({ dx_min, dx_max, dy_min, dy_max, dz_min, dz_max });
    if (min_d == dx_min) out.normal = { -1, 0, 0 };
    else if (min_d == dx_max) out.normal = { 1, 0, 0 };
    else if (min_d == dy_min) out.normal = { 0, -1, 0 };
    else if (min_d == dy_max) out.normal = { 0, 1, 0 };
    else if (min_d == dz_min) out.normal = { 0, 0, -1 };
    else                       out.normal = { 0, 0, 1 };
    return out;
}

}  // namespace cd::physics
