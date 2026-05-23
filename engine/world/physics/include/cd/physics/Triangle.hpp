// =============================================================================
// CHROMODYNAMIC — cd/physics/Triangle.hpp
// Phase 27.B / Wave 196 — triangle + barycentric helper.
//
// Barycentric coordinates of `point` relative to (a, b, c). Useful for
// projecting a point onto a mesh triangle for picking / hit tests.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::physics
{

struct Triangle
{
    cd::math::Vec3f a {};
    cd::math::Vec3f b {};
    cd::math::Vec3f c {};
};

/// Compute the barycentric coordinates (u, v, w) of `p` w.r.t. the
/// triangle (a, b, c). Returns them as Vec3 with u=x, v=y, w=z.
/// Point is inside the triangle iff all three coords ∈ [0, 1] and
/// sum to 1.
[[nodiscard]] inline cd::math::Vec3f barycentric(const Triangle& t,
                                                 const cd::math::Vec3f& p) noexcept
{
    const cd::math::Vec3f v0 { t.b.x - t.a.x, t.b.y - t.a.y, t.b.z - t.a.z };
    const cd::math::Vec3f v1 { t.c.x - t.a.x, t.c.y - t.a.y, t.c.z - t.a.z };
    const cd::math::Vec3f v2 { p.x   - t.a.x, p.y   - t.a.y, p.z   - t.a.z };
    const float d00 = v0.x * v0.x + v0.y * v0.y + v0.z * v0.z;
    const float d01 = v0.x * v1.x + v0.y * v1.y + v0.z * v1.z;
    const float d11 = v1.x * v1.x + v1.y * v1.y + v1.z * v1.z;
    const float d20 = v2.x * v0.x + v2.y * v0.y + v2.z * v0.z;
    const float d21 = v2.x * v1.x + v2.y * v1.y + v2.z * v1.z;
    const float denom = d00 * d11 - d01 * d01;
    if (std::abs(denom) < 1e-12F)
        return cd::math::Vec3f { 1.0F, 0.0F, 0.0F };  // degenerate
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    const float u = 1.0F - v - w;
    return cd::math::Vec3f { u, v, w };
}

[[nodiscard]] inline bool contains(const Triangle& t,
                                   const cd::math::Vec3f& p,
                                   float eps = 1e-4F) noexcept
{
    const auto bc = barycentric(t, p);
    return bc.x >= -eps && bc.y >= -eps && bc.z >= -eps;
}

}  // namespace cd::physics
