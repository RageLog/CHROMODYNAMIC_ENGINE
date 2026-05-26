// =============================================================================
// CHROMODYNAMIC — cd/texture_synth/Noise.hpp
//
// Small bag of procedural-noise helpers used to bake reference textures
// at runtime when no external asset is available. Cheap, deterministic,
// good enough for engine showcases + unit tests.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>
#include <cstdint>

namespace cd::texture_synth
{

/// 2-input integer hash → uniform float in [0, 1].
[[nodiscard]] inline float hash21(std::uint32_t x, std::uint32_t y) noexcept
{
    std::uint32_t h = x * 374761393U + y * 668265263U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return static_cast<float>(h & 0xFFFFFFU) / 16777215.0F;
}

/// Smooth value noise (bilinear interpolation with Hermite smoothing)
/// sampled at (u, v) ∈ [0, 1] using a grid of `freq × freq` cells.
[[nodiscard]] inline float value_noise2(float u, float v, float freq) noexcept
{
    const float fx = u * freq;
    const float fy = v * freq;
    const auto  x0 = static_cast<std::uint32_t>(std::floor(fx));
    const auto  y0 = static_cast<std::uint32_t>(std::floor(fy));
    const float tx = fx - std::floor(fx);
    const float ty = fy - std::floor(fy);
    const float sx = tx * tx * (3.0F - 2.0F * tx);
    const float sy = ty * ty * (3.0F - 2.0F * ty);
    const float a = hash21(x0,     y0);
    const float b = hash21(x0 + 1, y0);
    const float c = hash21(x0,     y0 + 1);
    const float d = hash21(x0 + 1, y0 + 1);
    return (a * (1 - sx) + b * sx) * (1 - sy) +
           (c * (1 - sx) + d * sx) * sy;
}

/// 3-octave fBm with halving amplitude — a sane default for landmass
/// or terrain-height generation. `base_freq` is the largest scale.
[[nodiscard]] inline float fbm2(float u, float v, float base_freq) noexcept
{
    return value_noise2(u, v, base_freq)        * 0.50F
         + value_noise2(u, v, base_freq * 2.0F) * 0.30F
         + value_noise2(u, v, base_freq * 4.0F) * 0.20F;
}

}  // namespace cd::texture_synth
