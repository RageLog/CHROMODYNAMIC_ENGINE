// =============================================================================
// CHROMODYNAMIC — cd/render/Tonemap.hpp
// Phase 63.A / Wave 231 — tonemap operators (Reinhard / ACES fitted).
//
// CPU-side reference implementations of the standard HDR → LDR
// tonemap operators. Shader-side versions live in GLSL/HLSL files
// and produce identical (within float precision) results.
//
// Operators:
//   * `tonemap_reinhard(x)`   = x / (1 + x), per-channel.
//   * `tonemap_aces_fitted(x)` = Krzysztof Narkowicz's fitted ACES
//                                approximation, single curve good
//                                enough for editor preview.
//   * `tonemap_uncharted2(x)` = Hable filmic curve.
//
// Per-channel scalar operators. Caller maps over RGB.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>

namespace cd::render
{

[[nodiscard]] inline float tonemap_reinhard(float x) noexcept
{
    if (x <= 0.0F) return 0.0F;
    return x / (1.0F + x);
}

/// Krzysztof Narkowicz's ACES fitted curve. Single-line; produces
/// the look of ACES-tonemapped HDR at ~5% the cost. Reference:
/// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
[[nodiscard]] inline float tonemap_aces_fitted(float x) noexcept
{
    constexpr float kA = 2.51F;
    constexpr float kB = 0.03F;
    constexpr float kC = 2.43F;
    constexpr float kD = 0.59F;
    constexpr float kE = 0.14F;
    const float t = (x * (kA * x + kB)) / (x * (kC * x + kD) + kE);
    return std::clamp(t, 0.0F, 1.0F);
}

/// Hable / Uncharted 2 filmic curve.
[[nodiscard]] inline float tonemap_uncharted2(float x) noexcept
{
    constexpr float kA = 0.15F;
    constexpr float kB = 0.50F;
    constexpr float kC = 0.10F;
    constexpr float kD = 0.20F;
    constexpr float kE = 0.02F;
    constexpr float kF = 0.30F;
    return ((x * (kA * x + kC * kB) + kD * kE) / (x * (kA * x + kB) + kD * kF)) - kE / kF;
}

}  // namespace cd::render
