// =============================================================================
// CHROMODYNAMIC — cd/physics/Obb.hpp
// Phase 25.C / Wave 192 — oriented bounding box (header-only).
//
// OBB = center + three orthonormal axes + per-axis half-extents.
// Contains-point uses the dot-product project test. `intersects(a, b)`
// is the full 15-axis separating-axis test (SAT) for OBB/OBB overlap
// (Gottschalk OBBTree / Ericson "Real-Time Collision Detection" §4.4).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <array>
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
    return !(std::abs(dz) > b.half_extents.z);
}

/// OBB/OBB overlap via the separating-axis theorem (SAT). Tests the 3
/// face axes of A, the 3 face axes of B, and the 9 edge-edge cross
/// products (15 axes total). Returns true when the boxes overlap or
/// touch. An `epsilon` (default 1e-6) is added to the parallel-edge
/// cross-product magnitudes per Ericson §4.4.1 to avoid false
/// separations when two axes are (near-)parallel and the cross product
/// degenerates to (near-)zero length.
[[nodiscard]] inline bool intersects(const Obb& a, const Obb& b,
                                     float epsilon = 1e-6F) noexcept
{
    const std::array<cd::math::Vec3f, 3> au { a.axis_x, a.axis_y, a.axis_z };
    const std::array<cd::math::Vec3f, 3> bu { b.axis_x, b.axis_y, b.axis_z };
    const std::array<float, 3> ae { a.half_extents.x, a.half_extents.y, a.half_extents.z };
    const std::array<float, 3> be { b.half_extents.x, b.half_extents.y, b.half_extents.z };

    const auto dot = [](const cd::math::Vec3f& u, const cd::math::Vec3f& v) noexcept
    {
        return u.x * v.x + u.y * v.y + u.z * v.z;
    };

    // Rotation matrix expressing B's axes in A's frame, and its abs form.
    std::array<std::array<float, 3>, 3> rot {};
    std::array<std::array<float, 3>, 3> abs_rot {};
    for (std::size_t i = 0; i < 3; ++i)
    {
        for (std::size_t j = 0; j < 3; ++j)
        {
            rot.at(i).at(j) = dot(au.at(i), bu.at(j));
            abs_rot.at(i).at(j) = std::abs(rot.at(i).at(j)) + epsilon;
        }
    }

    // Translation, in A's frame.
    const cd::math::Vec3f t_world {
        b.center.x - a.center.x,
        b.center.y - a.center.y,
        b.center.z - a.center.z,
    };
    const std::array<float, 3> t {
        dot(t_world, au.at(0)),
        dot(t_world, au.at(1)),
        dot(t_world, au.at(2)),
    };

    // L = A's face axes.
    for (std::size_t i = 0; i < 3; ++i)
    {
        const float ra = ae.at(i);
        const float rb = be.at(0) * abs_rot.at(i).at(0)
                       + be.at(1) * abs_rot.at(i).at(1)
                       + be.at(2) * abs_rot.at(i).at(2);
        if (std::abs(t.at(i)) > ra + rb) return false;
    }

    // L = B's face axes.
    for (std::size_t j = 0; j < 3; ++j)
    {
        const float ra = ae.at(0) * abs_rot.at(0).at(j)
                       + ae.at(1) * abs_rot.at(1).at(j)
                       + ae.at(2) * abs_rot.at(2).at(j);
        const float rb = be.at(j);
        const float tj = t.at(0) * rot.at(0).at(j)
                       + t.at(1) * rot.at(1).at(j)
                       + t.at(2) * rot.at(2).at(j);
        if (std::abs(tj) > ra + rb) return false;
    }

    // L = A_i x B_j (9 edge-edge axes). Indices follow Ericson §4.4.1.
    // A0 x B0
    {
        const float ra = ae.at(1) * abs_rot.at(2).at(0) + ae.at(2) * abs_rot.at(1).at(0);
        const float rb = be.at(1) * abs_rot.at(0).at(2) + be.at(2) * abs_rot.at(0).at(1);
        if (std::abs(t.at(2) * rot.at(1).at(0) - t.at(1) * rot.at(2).at(0)) > ra + rb) return false;
    }
    // A0 x B1
    {
        const float ra = ae.at(1) * abs_rot.at(2).at(1) + ae.at(2) * abs_rot.at(1).at(1);
        const float rb = be.at(0) * abs_rot.at(0).at(2) + be.at(2) * abs_rot.at(0).at(0);
        if (std::abs(t.at(2) * rot.at(1).at(1) - t.at(1) * rot.at(2).at(1)) > ra + rb) return false;
    }
    // A0 x B2
    {
        const float ra = ae.at(1) * abs_rot.at(2).at(2) + ae.at(2) * abs_rot.at(1).at(2);
        const float rb = be.at(0) * abs_rot.at(0).at(1) + be.at(1) * abs_rot.at(0).at(0);
        if (std::abs(t.at(2) * rot.at(1).at(2) - t.at(1) * rot.at(2).at(2)) > ra + rb) return false;
    }
    // A1 x B0
    {
        const float ra = ae.at(0) * abs_rot.at(2).at(0) + ae.at(2) * abs_rot.at(0).at(0);
        const float rb = be.at(1) * abs_rot.at(1).at(2) + be.at(2) * abs_rot.at(1).at(1);
        if (std::abs(t.at(0) * rot.at(2).at(0) - t.at(2) * rot.at(0).at(0)) > ra + rb) return false;
    }
    // A1 x B1
    {
        const float ra = ae.at(0) * abs_rot.at(2).at(1) + ae.at(2) * abs_rot.at(0).at(1);
        const float rb = be.at(0) * abs_rot.at(1).at(2) + be.at(2) * abs_rot.at(1).at(0);
        if (std::abs(t.at(0) * rot.at(2).at(1) - t.at(2) * rot.at(0).at(1)) > ra + rb) return false;
    }
    // A1 x B2
    {
        const float ra = ae.at(0) * abs_rot.at(2).at(2) + ae.at(2) * abs_rot.at(0).at(2);
        const float rb = be.at(0) * abs_rot.at(1).at(1) + be.at(1) * abs_rot.at(1).at(0);
        if (std::abs(t.at(0) * rot.at(2).at(2) - t.at(2) * rot.at(0).at(2)) > ra + rb) return false;
    }
    // A2 x B0
    {
        const float ra = ae.at(0) * abs_rot.at(1).at(0) + ae.at(1) * abs_rot.at(0).at(0);
        const float rb = be.at(1) * abs_rot.at(2).at(2) + be.at(2) * abs_rot.at(2).at(1);
        if (std::abs(t.at(1) * rot.at(0).at(0) - t.at(0) * rot.at(1).at(0)) > ra + rb) return false;
    }
    // A2 x B1
    {
        const float ra = ae.at(0) * abs_rot.at(1).at(1) + ae.at(1) * abs_rot.at(0).at(1);
        const float rb = be.at(0) * abs_rot.at(2).at(2) + be.at(2) * abs_rot.at(2).at(0);
        if (std::abs(t.at(1) * rot.at(0).at(1) - t.at(0) * rot.at(1).at(1)) > ra + rb) return false;
    }
    // A2 x B2
    {
        const float ra = ae.at(0) * abs_rot.at(1).at(2) + ae.at(1) * abs_rot.at(0).at(2);
        const float rb = be.at(0) * abs_rot.at(2).at(1) + be.at(1) * abs_rot.at(2).at(0);
        if (std::abs(t.at(1) * rot.at(0).at(2) - t.at(0) * rot.at(1).at(2)) > ra + rb) return false;
    }

    return true;  // no separating axis found → overlap
}

}  // namespace cd::physics
