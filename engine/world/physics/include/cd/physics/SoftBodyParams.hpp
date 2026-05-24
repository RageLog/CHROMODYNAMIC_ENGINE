// =============================================================================
// CHROMODYNAMIC — cd/physics/SoftBodyParams.hpp
// Phase 73.A / Wave 241 — cloth / spring solver knob struct.
//
// Mass-spring soft-body solver tuning parameters. Single-struct
// surface so the editor inspector can display cloth tuning sliders
// without hard-coding every default value.
//
//   * stiffness         — Hookean spring constant (0..k), N/m.
//   * damping           — viscous damping coefficient, 1/s.
//   * gravity           — acceleration vector, m/s².
//   * iterations        — XPBD position-based solver iterations.
//   * rest_length_scale — multiplier on initial spring rest length
//                         (1.0 = natural, < 1 pre-stretched cloth).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>

namespace cd::physics
{

struct SoftBodyParams
{
    float            stiffness          { 100.0F };
    float            damping            { 0.5F };
    cd::math::Vec3f  gravity            { 0.0F, -9.81F, 0.0F };
    std::uint32_t    iterations         { 8 };
    float            rest_length_scale  { 1.0F };
};

[[nodiscard]] constexpr SoftBodyParams soft_body_cloth_default() noexcept
{
    return SoftBodyParams { 80.0F, 0.3F, { 0, -9.81F, 0 }, 12, 1.0F };
}

[[nodiscard]] constexpr SoftBodyParams soft_body_rope_default() noexcept
{
    return SoftBodyParams { 300.0F, 0.8F, { 0, -9.81F, 0 }, 16, 1.0F };
}

}  // namespace cd::physics
