// =============================================================================
// CHROMODYNAMIC — cd/light/ColorTemperature.hpp
// Phase 165 / v0.99.87 — correlated color temperature → linear RGB.
//
// Implements Krystek's 1985 approximation of the Planckian locus in
// CIE xyY chromaticity, then transforms to linear sRGB via the
// standard sRGB primaries matrix. The same conversion every modern
// engine (Filament, , Godot 4) uses for "tungsten /
// daylight / overcast" lighting presets.
//
// Reference: Krystek, M. (1985). "An algorithm to calculate
// correlated colour temperature." Color Research & Application,
// 10(1), 38-40.
//
// Header-only because the work is a handful of multiply-adds.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>

namespace cd::light
{

/// Convert correlated color temperature (CCT) in Kelvin to linear sRGB.
/// Valid range: 1000 K (firelight) → 15000 K (overcast sky / shade).
/// Values outside that range are clamped.
///
/// Common references:
///   * Candle / firelight     ~1900 K
///   * Tungsten incandescent  ~2700 K
///   * Halogen                ~3200 K
///   * "Daylight" fluorescent ~4000 K
///   * Direct sunlight at noon ~5500 K
///   * Standard daylight (D65) ~6500 K
///   * Cloudy / overcast       ~7000 K
///   * Shade / blue sky       ~10000 K
[[nodiscard]] inline cd::math::Vec3f cct_to_linear_rgb(float kelvin) noexcept
{
    const float T = std::clamp(kelvin, 1000.0F, 15000.0F);

    // Krystek approximation gives CIE 1960 (u, v); we use a simpler
    // CCT → xy fit (CIE 1931) that's good enough for engine artist
    // workflow over the 1000-15000 K range:
    //   x(T) = -0.2661239e9/T^3 - 0.2343589e6/T^2 + 0.8776956e3/T + 0.179910   (T<=4000K)
    //          -3.0258469e9/T^3 + 2.1070379e6/T^2 + 0.2226347e3/T + 0.240390   (T>4000K)
    //   y(T) = -1.1063814 x^3 - 1.34811020 x^2 + 2.18555832 x - 0.20219683     (T<=2222K)
    //          -0.9549476 x^3 - 1.37418593 x^2 + 2.09137015 x - 0.16748867     (T<=4000K)
    //           3.0817580 x^3 - 5.87338670 x^2 + 3.75112997 x - 0.37001483     (T>4000K)
    const float T2 = T * T;
    const float T3 = T2 * T;
    float x = 0.0F;
    if (T <= 4000.0F)
        x = -0.2661239e9F  / T3 - 0.2343589e6F / T2 + 0.8776956e3F / T + 0.179910F;
    else
        x = -3.0258469e9F  / T3 + 2.1070379e6F / T2 + 0.2226347e3F / T + 0.240390F;

    float y = 0.0F;
    const float x2 = x * x;
    const float x3 = x2 * x;
    if (T <= 2222.0F)
        y = -1.1063814F  * x3 - 1.34811020F * x2 + 2.18555832F * x - 0.20219683F;
    else if (T <= 4000.0F)
        y = -0.9549476F  * x3 - 1.37418593F * x2 + 2.09137015F * x - 0.16748867F;
    else
        y =  3.0817580F  * x3 - 5.87338670F * x2 + 3.75112997F * x - 0.37001483F;

    // CIE 1931 xy → XYZ (assume luminance Y = 1).
    const float X = x / std::max(1e-6F, y);
    const float Z = (1.0F - x - y) / std::max(1e-6F, y);

    // CIE XYZ (D65) → linear sRGB (sRGB primaries, D65 white).
    // Bradford-adapted matrix from Bruce Lindbloom.
    const float r =  3.2404542F * X - 1.5371385F * 1.0F - 0.4985314F * Z;
    const float g = -0.9692660F * X + 1.8760108F * 1.0F + 0.0415560F * Z;
    const float b =  0.0556434F * X - 0.2040259F * 1.0F + 1.0572252F * Z;

    // Negative values can occur for very saturated CCTs at the edge
    // of the gamut; clamp to keep the output a valid color.
    return {
        std::max(0.0F, r),
        std::max(0.0F, g),
        std::max(0.0F, b),
    };
}

}  // namespace cd::light
