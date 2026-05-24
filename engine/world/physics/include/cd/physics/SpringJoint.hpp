// =============================================================================
// CHROMODYNAMIC — cd/physics/SpringJoint.hpp
// Phase 93.A / Wave 261 — distance / spring joint parameter struct.
//
// Two-body constraint pulling them toward a `rest_length` distance.
// Solver applies `force = -stiffness * (current_length - rest_length)
// - damping * relative_velocity` along the axis between attach points.
//
// Used for cloth strands (Phase 73 SoftBodyParams works on whole
// meshes; SpringJoint is the per-pair scalar variant), rope physics,
// suspension, soft-body anchor points.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>

namespace cd::physics
{

struct SpringJoint
{
    std::uint32_t   body_a       { 0 };
    std::uint32_t   body_b       { 0 };
    cd::math::Vec3f attach_a     {};   // local-space attach on body A
    cd::math::Vec3f attach_b     {};
    float           rest_length  { 1.0F };
    float           stiffness    { 100.0F };
    float           damping      { 1.0F };
};

[[nodiscard]] constexpr SpringJoint make_rope_link(std::uint32_t a, std::uint32_t b,
                                                   float rest_length) noexcept
{
    return SpringJoint { a, b, {}, {}, rest_length, 500.0F, 5.0F };
}

[[nodiscard]] constexpr SpringJoint make_cloth_link(std::uint32_t a, std::uint32_t b,
                                                    float rest_length) noexcept
{
    return SpringJoint { a, b, {}, {}, rest_length, 80.0F, 0.5F };
}

}  // namespace cd::physics
