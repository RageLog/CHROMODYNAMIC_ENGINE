// =============================================================================
// CHROMODYNAMIC — cd/math/Smoothstep.hpp
// Phase 77.B / Wave 245 — smootherstep + remap helpers.
//
// `cd::math::smoothstep(t)` lives in Easing.hpp (Phase 22, 3t² - 2t³).
// This header adds:
//
//   smootherstep:  6t⁵ - 15t⁴ + 10t³   — Perlin's C² continuity variant.
//   smoothstep_remap(edge0, edge1, x):  GLSL-style range remap.
//
// Both clamp to [0, 1].
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Easing.hpp>   // for smoothstep

#include <algorithm>

namespace cd::math
{

[[nodiscard]] constexpr float smootherstep(float t) noexcept
{
    t = std::clamp(t, 0.0F, 1.0F);
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] constexpr float smoothstep_remap(float edge0, float edge1, float x) noexcept
{
    if (edge1 <= edge0) return 0.0F;
    const float t = (x - edge0) / (edge1 - edge0);
    return smoothstep(t);
}

}  // namespace cd::math
