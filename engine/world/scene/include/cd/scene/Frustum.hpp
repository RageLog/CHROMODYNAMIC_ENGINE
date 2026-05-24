// =============================================================================
// CHROMODYNAMIC — cd/scene/Frustum.hpp
// Phase 29.C / Wave 198 — view-frustum + AABB cull primitive.
//
// A Frustum is six oriented half-spaces (left, right, bottom, top,
// near, far). Each plane stores the inward-facing normal and the
// signed distance from the origin to the plane along that normal —
// the same convention as Akenine-Möller §16.10:
//
//   plane(x) = dot(n, x) + d
//
// A point is *inside* the half-space iff plane(x) ≥ 0. A frustum
// contains a point iff plane(x) ≥ 0 for ALL six planes.
//
// For AABB culling we use the standard "n/p-vertex" trick: pick the
// AABB corner that is farthest along the plane normal (the *p-vertex*);
// if that corner is on the outside of any plane, the entire AABB is
// outside the frustum (early-reject). Implementation is branch-free per
// plane using std::clamp-style component selection.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>

namespace cd::scene
{

struct Plane
{
    cd::math::Vec3f n {};  ///< unit-length normal (caller must normalize)
    float           d { 0.0F };
};

[[nodiscard]] inline float signed_distance(const Plane& p,
                                           const cd::math::Vec3f& v) noexcept
{
    return p.n.x * v.x + p.n.y * v.y + p.n.z * v.z + p.d;
}

struct Frustum
{
    Plane left {};
    Plane right {};
    Plane bottom {};
    Plane top {};
    Plane near_ {};   // `near` is a Windows macro
    Plane far_ {};
};

/// Returns true iff `aabb` overlaps the frustum (inclusive — touching
/// counts as inside). Uses the p-vertex test against each plane;
/// returns false the moment a plane fully rejects the AABB.
[[nodiscard]] inline bool intersects(const Frustum& f,
                                     const cd::physics::Aabb& aabb) noexcept
{
    const Plane planes[6] = { f.left, f.right, f.bottom, f.top, f.near_, f.far_ };
    for (const auto& p : planes)
    {
        // p-vertex: corner farthest along plane normal.
        const cd::math::Vec3f pv {
            (p.n.x >= 0.0F) ? aabb.max.x : aabb.min.x,
            (p.n.y >= 0.0F) ? aabb.max.y : aabb.min.y,
            (p.n.z >= 0.0F) ? aabb.max.z : aabb.min.z,
        };
        if (signed_distance(p, pv) < 0.0F)
            return false;
    }
    return true;
}

/// Returns true iff the sphere `(center, radius)` is at least
/// partially inside the frustum. Classic bounding-sphere cull: a
/// sphere is outside iff its center is on the negative side of any
/// plane by MORE than its radius. This is the cheapest cull test
/// engines apply before the AABB-level intersects() above.
///
/// Inclusive — a sphere just touching a plane (signed_distance == -r)
/// is considered inside. Required by Phase 109 marathon item; same
/// convention as `intersects(Frustum, Aabb)`.
[[nodiscard]] inline bool contains_sphere(const Frustum& f,
                                          const cd::math::Vec3f& center,
                                          float radius) noexcept
{
    const Plane planes[6] = { f.left, f.right, f.bottom, f.top, f.near_, f.far_ };
    const float neg_r = -radius;
    for (const auto& p : planes)
    {
        if (signed_distance(p, center) < neg_r) return false;
    }
    return true;
}

/// Strict containment: returns true iff the sphere is FULLY inside
/// every half-space (no plane crosses the sphere). Useful for picking
/// "trivially-accept" shadow-frustum candidates.
[[nodiscard]] inline bool fully_contains_sphere(const Frustum& f,
                                                const cd::math::Vec3f& center,
                                                float radius) noexcept
{
    const Plane planes[6] = { f.left, f.right, f.bottom, f.top, f.near_, f.far_ };
    for (const auto& p : planes)
    {
        if (signed_distance(p, center) < radius) return false;
    }
    return true;
}

}  // namespace cd::scene
