// =============================================================================
// CHROMODYNAMIC — cd/anim/AdditiveBlend.hpp
// Phase 30.A / Wave 199 — additive layer over a base pose.
//
// Additive blending is what every game engine uses for upper-body
// overlays (aim, lean, breathe) on top of a locomotion base: the
// "additive pose" is a *delta* relative to a reference rest pose, and
// it is added back to the base pose with a per-frame weight.
//
//   out_pos[i] = base_pos[i] + weight * additive_delta_pos[i]
//   out_rot[i] = base_rot[i] * pow(additive_delta_rot[i], weight)
//   out_scale[i] = base_scale[i] * lerp(1, additive_delta_scale[i], weight)
//
// For the rotation we use the cheaper *quaternion nlerp identity* form
// — interpolate between identity and the delta, then multiply into the
// base. This matches Unreal's UE5 layered-blend formula (Animation
// Kernel docs).
//
// Pose dimensionality must match (base.size() == delta.size() == out.size()).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>
#include <span>

namespace cd::anim
{

namespace detail
{

[[nodiscard]] inline cd::math::Quatf nlerp_to_identity(const cd::math::Quatf& q,
                                                      float t) noexcept
{
    // Interpolate between identity (0,0,0,1) and q, normalized.
    const float sign = q.w < 0.0F ? -1.0F : 1.0F;
    cd::math::Quatf out {
        sign * q.x * t,
        sign * q.y * t,
        sign * q.z * t,
        1.0F + (sign * q.w - 1.0F) * t,
    };
    const float len2 = out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w;
    if (len2 > 0.0F)
    {
        const float inv = 1.0F / std::sqrt(len2);
        out.x *= inv; out.y *= inv; out.z *= inv; out.w *= inv;
    }
    return out;
}

[[nodiscard]] inline cd::math::Quatf qmul(const cd::math::Quatf& a,
                                         const cd::math::Quatf& b) noexcept
{
    return cd::math::Quatf {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

}  // namespace detail

/// Apply `additive_delta` to `base` with `weight`. weight=0 returns
/// base unchanged; weight=1 applies the full delta.
/// Returns true on success; false on size mismatch.
inline bool additive_blend(std::span<const cd::math::Transformf> base,
                           std::span<const cd::math::Transformf> additive_delta,
                           float weight,
                           std::span<cd::math::Transformf> out) noexcept
{
    if (base.size() != additive_delta.size() || out.size() != base.size())
        return false;
    if (weight < 0.0F) weight = 0.0F;
    if (weight > 1.0F) weight = 1.0F;
    for (std::size_t i = 0; i < base.size(); ++i)
    {
        const auto& bp = base[i];
        const auto& dp = additive_delta[i];
        out[i].position = cd::math::Vec3f {
            bp.position.x + weight * dp.position.x,
            bp.position.y + weight * dp.position.y,
            bp.position.z + weight * dp.position.z,
        };
        const auto scaled_q = detail::nlerp_to_identity(dp.rotation, weight);
        out[i].rotation = detail::qmul(bp.rotation, scaled_q);
        out[i].scale = cd::math::Vec3f {
            bp.scale.x * (1.0F + (dp.scale.x - 1.0F) * weight),
            bp.scale.y * (1.0F + (dp.scale.y - 1.0F) * weight),
            bp.scale.z * (1.0F + (dp.scale.z - 1.0F) * weight),
        };
    }
    return true;
}

}  // namespace cd::anim
