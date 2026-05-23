// =============================================================================
// CHROMODYNAMIC — cd/physics/Aabb.hpp
// Phase 20.B / Wave 182 — header-only axis-aligned bounding-box primitive.
//
// AABB = min/max corner pair. `overlaps(a, b)` returns true when the
// two boxes share interior or touch at a face/edge/vertex. `contains`
// helpers for point + AABB. Foundation broad-phase primitive that
// every physics layer (Jolt integration in a future wave) builds on.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>

namespace cd::physics
{

struct Aabb
{
    cd::math::Vec3f min {};
    cd::math::Vec3f max {};
};

[[nodiscard]] inline bool overlaps(const Aabb& a, const Aabb& b) noexcept
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

[[nodiscard]] inline bool contains(const Aabb& a, const cd::math::Vec3f& p) noexcept
{
    return p.x >= a.min.x && p.x <= a.max.x &&
           p.y >= a.min.y && p.y <= a.max.y &&
           p.z >= a.min.z && p.z <= a.max.z;
}

[[nodiscard]] inline Aabb merge(const Aabb& a, const Aabb& b) noexcept
{
    Aabb out;
    out.min.x = std::min(a.min.x, b.min.x);
    out.min.y = std::min(a.min.y, b.min.y);
    out.min.z = std::min(a.min.z, b.min.z);
    out.max.x = std::max(a.max.x, b.max.x);
    out.max.y = std::max(a.max.y, b.max.y);
    out.max.z = std::max(a.max.z, b.max.z);
    return out;
}

[[nodiscard]] inline cd::math::Vec3f center(const Aabb& a) noexcept
{
    return cd::math::Vec3f {
        0.5F * (a.min.x + a.max.x),
        0.5F * (a.min.y + a.max.y),
        0.5F * (a.min.z + a.max.z),
    };
}

[[nodiscard]] inline cd::math::Vec3f extent(const Aabb& a) noexcept
{
    return cd::math::Vec3f {
        a.max.x - a.min.x,
        a.max.y - a.min.y,
        a.max.z - a.min.z,
    };
}

}  // namespace cd::physics
