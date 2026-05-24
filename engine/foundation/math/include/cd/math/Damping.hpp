// =============================================================================
// CHROMODYNAMIC — cd/math/Damping.hpp
// Phase 81.B / Wave 249 — critically-damped spring response.
//
// "Spring-damper" interpolation for smooth camera follow, UI animation,
// hot-reload value transitions. Game Programming Gems formulation:
//
//   x[n+1] = x[n] + v[n] * dt
//   v[n+1] = v[n] + ((target - x[n]) * stiffness - v[n] * damping) * dt
//
// `critical_damping(stiffness)` returns the damping coefficient that
// gives critical (no-overshoot) response.
//
// Usage:
//   float v = 0;
//   float current = 0;
//   step_critically_damped(current, v, target, stiffness, dt);
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>

namespace cd::math
{

[[nodiscard]] constexpr float critical_damping(float stiffness) noexcept
{
    return 2.0F * stiffness;   // mass = 1, c_crit = 2 sqrt(km) but with k = stiffness²
}

inline void step_critically_damped(float& current, float& velocity,
                                   float target, float stiffness,
                                   float dt) noexcept
{
    // Stable explicit Euler (works as long as dt * stiffness < ~1).
    const float damping = critical_damping(stiffness);
    const float accel = (target - current) * stiffness * stiffness - velocity * damping;
    velocity += accel * dt;
    current  += velocity * dt;
}

inline void step_critically_damped_vec3(float current[3], float velocity[3],
                                        const float target[3],
                                        float stiffness, float dt) noexcept
{
    for (int i = 0; i < 3; ++i)
    {
        const float damping = 2.0F * stiffness;
        const float accel = (target[i] - current[i]) * stiffness * stiffness - velocity[i] * damping;
        velocity[i] += accel * dt;
        current[i]  += velocity[i] * dt;
    }
}

}  // namespace cd::math
