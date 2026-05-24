// =============================================================================
// CHROMODYNAMIC — cd/audio/PanLaw.hpp
// Phase 81.A / Wave 249 — stereo pan-law helpers.
//
// Pan position `p ∈ [-1, 1]` where -1 = full left, 0 = center, +1 =
// full right. Two pan laws supported:
//
//   * Linear pan (3 dB center attenuation): `L = (1-p)/2`, `R = (1+p)/2`.
//     Center sums to 1.0 (loud); endpoints sum to 1.0 (same loudness),
//     but the *energy* drops by 3dB at the center compared to ends.
//
//   * Constant-power pan (-3 dB center attenuation, 0 dB endpoints):
//     `L = cos(angle)`, `R = sin(angle)`, where angle = (p + 1) * π/4.
//     Total power L² + R² = 1 for all p — perceptually loudness-flat.
//
// Constant power is the industry default for music; linear is fine
// for tightly-correlated mono sources.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cmath>

namespace cd::audio
{

struct PanGains
{
    float left  { 1.0F };
    float right { 1.0F };
};

[[nodiscard]] inline PanGains linear_pan(float p) noexcept
{
    p = std::clamp(p, -1.0F, 1.0F);
    return PanGains { (1.0F - p) * 0.5F, (1.0F + p) * 0.5F };
}

[[nodiscard]] inline PanGains constant_power_pan(float p) noexcept
{
    p = std::clamp(p, -1.0F, 1.0F);
    constexpr float kQuarterPi = 0.7853981633974483F;
    const float angle = (p + 1.0F) * kQuarterPi;   // [0, π/2]
    return PanGains { std::cos(angle), std::sin(angle) };
}

}  // namespace cd::audio
