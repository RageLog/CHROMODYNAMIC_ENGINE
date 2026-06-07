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

// phase898-quintic-value-noise: Perlin 2002 "improved" Hermite
// smoothing function 6t^5 - 15t^4 + 10t^3. Unlike the cubic
// `3t^2 - 2t^3` used by value_noise2, the quintic has zero second
// derivative at t=0 and t=1, which eliminates the visible
// axis-aligned ridges cubic value noise leaves at cell boundaries
// (especially on diagonals / fine-scale composition).
//
// Drop-in for value_noise2 with the same (u, v, freq) call shape.
// Mirror of the GLSL `cd_value_noise` improvement that landed in
// phase853 (engine/render/post/composite/Composite.hpp). Useful
// when a CPU-baked detail map needs the same edge-free quality
// the runtime cloud shader uses.
[[nodiscard]] inline float value_noise2_quintic(float u, float v, float freq) noexcept
{
    const float fx = u * freq;
    const float fy = v * freq;
    const auto  x0 = static_cast<std::uint32_t>(std::floor(fx));
    const auto  y0 = static_cast<std::uint32_t>(std::floor(fy));
    const float tx = fx - std::floor(fx);
    const float ty = fy - std::floor(fy);
    // Quintic Hermite: 6t^5 - 15t^4 + 10t^3
    const float sx = tx * tx * tx * (tx * (tx * 6.0F - 15.0F) + 10.0F);
    const float sy = ty * ty * ty * (ty * (ty * 6.0F - 15.0F) + 10.0F);
    const float a = hash21(x0,     y0);
    const float b = hash21(x0 + 1, y0);
    const float c = hash21(x0,     y0 + 1);
    const float d = hash21(x0 + 1, y0 + 1);
    return (a * (1 - sx) + b * sx) * (1 - sy) +
           (c * (1 - sx) + d * sx) * sy;
}

/// 6-octave fBm with halving amplitude built on the quintic noise.
/// Mirror of the GLSL `cd_fbm4` that the phase853 cloud overlay
/// uses (called with 6 octaves in the world-anchored path). The
/// extra octaves carry sub-pixel detail; the quintic core
/// eliminates the cubic-axis ridge artefact.
[[nodiscard]] inline float fbm2_quintic_6oct(float u, float v, float base_freq) noexcept
{
    float s = 0.0F;
    float a = 0.5F;
    float f = base_freq;
    for (int i = 0; i < 6; ++i)
    {
        s += a * value_noise2_quintic(u, v, f);
        f *= 2.07F;
        a *= 0.5F;
    }
    return s;
}

}  // namespace cd::texture_synth
