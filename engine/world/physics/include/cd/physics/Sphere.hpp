// =============================================================================
// CHROMODYNAMIC — cd/physics/Sphere.hpp
// Phase 21.A / Wave 184 — header-only sphere primitive + intersection.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>

#include <algorithm>
#include <cmath>

namespace cd::physics
{

struct Sphere
{
    cd::math::Vec3f center {};
    float radius { 0.0F };
};

[[nodiscard]] inline bool intersects(const Sphere& a, const Sphere& b) noexcept
{
    const float dx = a.center.x - b.center.x;
    const float dy = a.center.y - b.center.y;
    const float dz = a.center.z - b.center.z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    const float r = a.radius + b.radius;
    return d2 <= r * r;
}

[[nodiscard]] inline bool contains(const Sphere& s, const cd::math::Vec3f& p) noexcept
{
    const float dx = s.center.x - p.x;
    const float dy = s.center.y - p.y;
    const float dz = s.center.z - p.z;
    return (dx * dx + dy * dy + dz * dz) <= s.radius * s.radius;
}

[[nodiscard]] inline bool intersects(const Sphere& s, const Aabb& a) noexcept
{
    // Distance from sphere center to closest point on AABB.
    const float cx = std::clamp(s.center.x, a.min.x, a.max.x);
    const float cy = std::clamp(s.center.y, a.min.y, a.max.y);
    const float cz = std::clamp(s.center.z, a.min.z, a.max.z);
    const float dx = s.center.x - cx;
    const float dy = s.center.y - cy;
    const float dz = s.center.z - cz;
    return (dx * dx + dy * dy + dz * dz) <= s.radius * s.radius;
}

}  // namespace cd::physics
