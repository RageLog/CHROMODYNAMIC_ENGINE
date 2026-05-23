// =============================================================================
// CHROMODYNAMIC — cd/physics/Capsule.hpp
// Phase 26.C / Wave 194 — capsule (swept-sphere along a segment).
//
// Capsule defined by two endpoints + radius. `contains(point)`
// returns true when the point's distance to the segment is <= radius.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace cd::physics
{

struct Capsule
{
    cd::math::Vec3f p0 {};
    cd::math::Vec3f p1 {};
    float radius { 0.5F };
};

[[nodiscard]] inline float distance_point_segment(const cd::math::Vec3f& p,
                                                  const cd::math::Vec3f& a,
                                                  const cd::math::Vec3f& b) noexcept
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float len2 = dx * dx + dy * dy + dz * dz;
    if (len2 <= 1e-12F)
    {
        const float ex = p.x - a.x;
        const float ey = p.y - a.y;
        const float ez = p.z - a.z;
        return std::sqrt(ex * ex + ey * ey + ez * ez);
    }
    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy + (p.z - a.z) * dz) / len2;
    t = std::clamp(t, 0.0F, 1.0F);
    const float qx = a.x + t * dx;
    const float qy = a.y + t * dy;
    const float qz = a.z + t * dz;
    const float ex = p.x - qx;
    const float ey = p.y - qy;
    const float ez = p.z - qz;
    return std::sqrt(ex * ex + ey * ey + ez * ez);
}

[[nodiscard]] inline bool contains(const Capsule& c, const cd::math::Vec3f& p) noexcept
{
    return distance_point_segment(p, c.p0, c.p1) <= c.radius;
}

}  // namespace cd::physics
