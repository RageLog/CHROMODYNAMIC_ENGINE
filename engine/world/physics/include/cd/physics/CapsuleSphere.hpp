// =============================================================================
// CHROMODYNAMIC — cd/physics/CapsuleSphere.hpp
// Phase 45.A / Wave 213 — capsule ↔ sphere intersection.
//
// A capsule overlaps a sphere iff the sphere's centre lies within
// (capsule.radius + sphere.radius) of the capsule's segment. This is
// O(1) via `distance_point_segment(sphere.center, capsule.p0, capsule.p1)`.
//
// Common use cases:
//   * Character body capsule vs. trigger sphere ("inside damage zone").
//   * Projectile (sphere) vs. enemy hitbox (capsule).
//   * AI line-of-sight cone simplified to a capsule.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/physics/Capsule.hpp>
#include <cd/physics/Sphere.hpp>

namespace cd::physics
{

[[nodiscard]] inline bool intersects(const Capsule& c, const Sphere& s) noexcept
{
    const float d = distance_point_segment(s.center, c.p0, c.p1);
    return d <= (c.radius + s.radius);
}

[[nodiscard]] inline bool intersects(const Sphere& s, const Capsule& c) noexcept
{
    return intersects(c, s);
}

}  // namespace cd::physics
