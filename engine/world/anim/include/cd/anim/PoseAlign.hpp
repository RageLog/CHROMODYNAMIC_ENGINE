// =============================================================================
// CHROMODYNAMIC — cd/anim/PoseAlign.hpp
// Phase 86.A / Wave 254 — root-motion alignment helpers.
//
// In retargeting / blending, two source animations may have different
// root positions; we want the *delta* per frame, not the absolute
// position. `align_root_position(...)` subtracts the rest-pose root
// from each frame's root; `compose_root_motion(...)` re-adds an
// arbitrary world position.
//
// Operates on `cd::math::Transformf::position` only; rotation /
// scale stay untouched. Locomotion plumbing layer below `BlendTree2`
// (Phase 19).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Transform.hpp>
#include <cd/math/Vector.hpp>

#include <span>

namespace cd::anim
{

inline void align_root_position(std::span<cd::math::Transformf> frames,
                                const cd::math::Vec3f& rest_root) noexcept
{
    for (auto& f : frames)
    {
        f.position.x -= rest_root.x;
        f.position.y -= rest_root.y;
        f.position.z -= rest_root.z;
    }
}

inline void compose_root_motion(std::span<cd::math::Transformf> frames,
                                const cd::math::Vec3f& world_root) noexcept
{
    for (auto& f : frames)
    {
        f.position.x += world_root.x;
        f.position.y += world_root.y;
        f.position.z += world_root.z;
    }
}

[[nodiscard]] inline cd::math::Vec3f
root_velocity(const cd::math::Transformf& prev,
              const cd::math::Transformf& current,
              float dt) noexcept
{
    if (dt <= 0.0F) return cd::math::Vec3f {};
    const float inv = 1.0F / dt;
    return cd::math::Vec3f {
        (current.position.x - prev.position.x) * inv,
        (current.position.y - prev.position.y) * inv,
        (current.position.z - prev.position.z) * inv,
    };
}

}  // namespace cd::anim
