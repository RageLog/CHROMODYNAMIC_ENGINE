// =============================================================================
// CHROMODYNAMIC — cd/anim/BlendTree2.hpp
// Phase 19.F / Wave 180 — two-pose linear blend primitive.
//
// Given two source poses A and B (each a vector of joint Transforms)
// and a blend weight in [0, 1], produces an output pose where 0 = A,
// 1 = B, intermediate = LERP per joint. Quaternion rotations use
// NLERP (renormalize after componentwise lerp) — simpler than SLERP
// and visually indistinguishable for small blend angles, which is
// what walk/run blends produce.
//
// Pose dimensionality is implicit (must match input poses). Caller
// asserts/checks A.size() == B.size() externally; this primitive
// returns an empty span if they mismatch.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>

#include <span>
#include <vector>

namespace cd::anim
{

[[nodiscard]] inline cd::math::Vec3f lerp_vec3(const cd::math::Vec3f& a, const cd::math::Vec3f& b, float t) noexcept
{
    return cd::math::Vec3f {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

[[nodiscard]] inline cd::math::Quatf nlerp_quat(const cd::math::Quatf& a, const cd::math::Quatf& b, float t) noexcept
{
    // Pick the shorter arc.
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float sign = dot < 0.0F ? -1.0F : 1.0F;
    cd::math::Quatf out {
        a.x + (sign * b.x - a.x) * t,
        a.y + (sign * b.y - a.y) * t,
        a.z + (sign * b.z - a.z) * t,
        a.w + (sign * b.w - a.w) * t,
    };
    const float len2 = out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w;
    if (len2 > 0.0F)
    {
        const float inv = 1.0F / std::sqrt(len2);
        out.x *= inv; out.y *= inv; out.z *= inv; out.w *= inv;
    }
    return out;
}

/// Two-pose linear blend. `out` must already be sized to A.size().
/// Returns true on success; false when A.size() != B.size().
inline bool blend2(std::span<const cd::math::Transformf> a,
                   std::span<const cd::math::Transformf> b,
                   float t,
                   std::span<cd::math::Transformf> out) noexcept
{
    if (a.size() != b.size() || out.size() != a.size())
        return false;
    if (t < 0.0F) t = 0.0F;
    if (t > 1.0F) t = 1.0F;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        out[i].position = lerp_vec3(a[i].position, b[i].position, t);
        out[i].rotation = nlerp_quat(a[i].rotation, b[i].rotation, t);
        out[i].scale    = lerp_vec3(a[i].scale,    b[i].scale,    t);
    }
    return true;
}

}  // namespace cd::anim
