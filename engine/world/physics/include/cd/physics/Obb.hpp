// =============================================================================
// CHROMODYNAMIC — cd/physics/Obb.hpp
// Phase 25.C / Wave 192 — oriented bounding box (header-only).
//
// OBB = center + three orthonormal axes + per-axis half-extents.
// Contains-point uses the dot-product project test. Full OBB/OBB
// intersection (SAT) lands when the first user code needs it; this
// wave ships the shape + the cheap query.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::physics
{

struct Obb
{
    cd::math::Vec3f center {};
    cd::math::Vec3f axis_x { 1, 0, 0 };
    cd::math::Vec3f axis_y { 0, 1, 0 };
    cd::math::Vec3f axis_z { 0, 0, 1 };
    cd::math::Vec3f half_extents { 0.5F, 0.5F, 0.5F };
};

[[nodiscard]] inline bool contains(const Obb& b, const cd::math::Vec3f& p) noexcept
{
    const cd::math::Vec3f d {
        p.x - b.center.x,
        p.y - b.center.y,
        p.z - b.center.z,
    };
    const float dx = d.x * b.axis_x.x + d.y * b.axis_x.y + d.z * b.axis_x.z;
    if (std::abs(dx) > b.half_extents.x) return false;
    const float dy = d.x * b.axis_y.x + d.y * b.axis_y.y + d.z * b.axis_y.z;
    if (std::abs(dy) > b.half_extents.y) return false;
    const float dz = d.x * b.axis_z.x + d.y * b.axis_z.y + d.z * b.axis_z.z;
    if (std::abs(dz) > b.half_extents.z) return false;
    return true;
}

}  // namespace cd::physics
