// =============================================================================
// CHROMODYNAMIC — cd/math/RangeMap.hpp
// Phase 79.B / Wave 247 — cross-range linear remap.
//
// `remap(x, in_min, in_max, out_min, out_max)` is the classic GLSL
// idiom: linearly project `x` from input range to output range, with
// optional clamping. Used by UI slider → property value, audio
// dBFS → linear gain, normalized [0,1] → screen-space pixel rect.
//
// `clamped_remap` clamps `x` to `[in_min, in_max]` first; `remap`
// extrapolates without clamping.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>

namespace cd::math
{

[[nodiscard]] constexpr float remap(float x,
                                    float in_min, float in_max,
                                    float out_min, float out_max) noexcept
{
    if (in_max == in_min) return out_min;
    const float t = (x - in_min) / (in_max - in_min);
    return out_min + t * (out_max - out_min);
}

[[nodiscard]] constexpr float clamped_remap(float x,
                                            float in_min, float in_max,
                                            float out_min, float out_max) noexcept
{
    if (in_max == in_min) return out_min;
    const float t = std::clamp((x - in_min) / (in_max - in_min), 0.0F, 1.0F);
    return out_min + t * (out_max - out_min);
}

}  // namespace cd::math
