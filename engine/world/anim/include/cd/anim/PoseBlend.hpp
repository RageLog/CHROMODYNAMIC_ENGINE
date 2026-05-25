// =============================================================================
// CHROMODYNAMIC — cd/anim/PoseBlend.hpp
// Phase 156 / v0.99.82 — pose blending + retargeting helpers.
//
// Single-track animation already lands per joint (Phase 5/S4.2.b
// SkinnedClip::sample). The missing piece for production-quality
// character animation is BLENDING between two poses — what every
// character controller does to transition between idle ↔ walk ↔ run
// or layer an upper-body action onto a locomotion clip.
//
// API surface (header-only):
//
//   blend_pose(a, b, weight, out)
//       Linear blend two poses joint-by-joint. weight=0 → a;
//       weight=1 → b; intermediate values lerp position/scale and
//       slerp rotation. Output Pose must already be sized to match
//       a + b.
//
//   blend_pose_into(target, b, weight, joint_filter)
//       In-place blend: target = lerp(target, b, weight). Optional
//       `joint_filter` callback returns true for joints that should
//       blend (false = leave the target's value alone). Useful for
//       layered upper/lower-body composition.
//
//   additive_apply(base, additive, weight, out)
//       Apply an additive offset clip on top of a base. additive's
//       joint locals are treated as DELTAS from bind pose. Common
//       for "head tilt" or "breathing" overlays that should ride on
//       any locomotion clip.
//
// All three functions are pure — no allocations beyond the caller's
// output Pose. They preserve the rest-of-pose untouched when the
// input poses are mismatched in size (only the common prefix joints
// get blended).
// =============================================================================
#pragma once

#include <cd/anim/Skeleton.hpp>
#include <cd/core/Defines.hpp>
#include <cd/math/QuatSlerp.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Transform.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>

namespace cd::anim
{

/// Linear-blend two transforms: position/scale lerp + rotation slerp.
[[nodiscard]] inline cd::math::Transformf
lerp_transform(const cd::math::Transformf& a,
               const cd::math::Transformf& b,
               float t) noexcept
{
    cd::math::Transformf r;
    const float inv = 1.0F - t;
    r.position.x = a.position.x * inv + b.position.x * t;
    r.position.y = a.position.y * inv + b.position.y * t;
    r.position.z = a.position.z * inv + b.position.z * t;
    r.scale.x    = a.scale.x    * inv + b.scale.x    * t;
    r.scale.y    = a.scale.y    * inv + b.scale.y    * t;
    r.scale.z    = a.scale.z    * inv + b.scale.z    * t;
    r.rotation   = cd::math::slerp(a.rotation, b.rotation, t);
    return r;
}

/// Blend two poses joint-by-joint into `out`.
/// `weight` is clamped to [0, 1]. Out size := min(a, b).
inline void blend_pose(const Pose& a, const Pose& b, float weight, Pose& out)
{
    weight = std::clamp(weight, 0.0F, 1.0F);
    const auto n = std::min(a.joint_locals.size(), b.joint_locals.size());
    out.joint_locals.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        out.joint_locals[i] = lerp_transform(a.joint_locals[i], b.joint_locals[i], weight);
}

/// In-place blend: target = lerp(target, b, weight). `joint_filter`
/// is optional — return true to include the joint in the blend, false
/// to leave the target's value alone. nullptr means blend every joint.
inline void blend_pose_into(Pose& target, const Pose& b, float weight,
                            const std::function<bool(std::size_t)>& joint_filter = {})
{
    weight = std::clamp(weight, 0.0F, 1.0F);
    const auto n = std::min(target.joint_locals.size(), b.joint_locals.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        if (joint_filter && !joint_filter(i)) continue;
        target.joint_locals[i] = lerp_transform(target.joint_locals[i], b.joint_locals[i], weight);
    }
}

/// Additive layer: `additive`'s joint locals are treated as DELTAS
/// from `base` (position += delta.position, scale *= delta.scale,
/// rotation = delta.rotation * base.rotation). `weight` scales the
/// additive contribution; weight=0 means base wins entirely.
inline void additive_apply(const Pose& base, const Pose& additive, float weight, Pose& out)
{
    weight = std::clamp(weight, 0.0F, 1.0F);
    const auto n = std::min(base.joint_locals.size(), additive.joint_locals.size());
    out.joint_locals.resize(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& bp = base.joint_locals[i];
        const auto& ap = additive.joint_locals[i];
        cd::math::Transformf r = bp;
        // Scale the additive delta by weight before applying.
        r.position.x += ap.position.x * weight;
        r.position.y += ap.position.y * weight;
        r.position.z += ap.position.z * weight;
        // Scale lerps from base toward (base*additive).
        r.scale.x = bp.scale.x * (1.0F - weight) + bp.scale.x * ap.scale.x * weight;
        r.scale.y = bp.scale.y * (1.0F - weight) + bp.scale.y * ap.scale.y * weight;
        r.scale.z = bp.scale.z * (1.0F - weight) + bp.scale.z * ap.scale.z * weight;
        // Rotation: slerp from base toward (additive * base).
        const auto combined = ap.rotation * bp.rotation;
        r.rotation = cd::math::slerp(bp.rotation, combined, weight);
        out.joint_locals[i] = r;
    }
}

}  // namespace cd::anim
