// =============================================================================
// CHROMODYNAMIC — cd/math/QuatSlerp.hpp
// Phase 27.A / Wave 196 — proper spherical linear interpolation.
//
// nlerp (Phase 19.F's blend2) is fast and visually fine for small
// angles, but animation cuts that traverse > ~45° benefit from
// arc-length-correct SLERP. This header adds it without disturbing
// the existing nlerp path.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>

#include <cmath>

namespace cd::math
{

[[nodiscard]] inline Quatf slerp(const Quatf& a, const Quatf& b, float t) noexcept
{
    if (t <= 0.0F) return a;
    if (t >= 1.0F) return b;
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quatf bb = b;
    if (dot < 0.0F)
    {
        bb = Quatf { -b.x, -b.y, -b.z, -b.w };
        dot = -dot;
    }
    if (dot > 0.9995F)
    {
        // Linear fallback for very small angles to avoid numerical
        // issues near sin(0).
        Quatf r {
            a.x + (bb.x - a.x) * t,
            a.y + (bb.y - a.y) * t,
            a.z + (bb.z - a.z) * t,
            a.w + (bb.w - a.w) * t,
        };
        const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (len > 0.0F) { r.x /= len; r.y /= len; r.z /= len; r.w /= len; }
        return r;
    }
    const float theta_0 = std::acos(dot);
    const float theta = theta_0 * t;
    const float sin_theta = std::sin(theta);
    const float sin_theta_0 = std::sin(theta_0);
    const float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
    const float s1 = sin_theta / sin_theta_0;
    return Quatf {
        s0 * a.x + s1 * bb.x,
        s0 * a.y + s1 * bb.y,
        s0 * a.z + s1 * bb.z,
        s0 * a.w + s1 * bb.w,
    };
}

}  // namespace cd::math
