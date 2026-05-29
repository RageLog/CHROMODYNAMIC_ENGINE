// =============================================================================
// CHROMODYNAMIC — cd/post/tonemap/Tonemap.hpp
// Day 13 — Tonemap stack.
//
// State-of-the-art tonemap operators for HDR -> display-mapped colour.
// CPU functions + matching GLSL strings so render code can pick the
// operator at material-creation time without duplicating the math.
//
// References:
//   * Narkowicz 2015 — "ACES Filmic Tone Mapping Curve"
//     https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
//   * Hable 2010 — "Uncharted 2: HDR Lighting" (Filmic/Hable).
//   * Hill 2017 — "A Closer Look at the ACES Tonemap"
//     (the production "Hill ACES" fit used in Filament + UE).
//   * Sobotka 2022 — AGX. Inverse-log mapping + per-channel S-curve.
//     Reference: https://github.com/sobotka/AgX
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::post::tonemap
{

/// Tonemap-operator tag — passed to `tonemap()` for runtime selection.
enum class Operator : std::uint8_t
{
    kNarkowicz = 0,  ///< Narkowicz 2015 ACES fit (cheapest, deepest blacks)
    kHill      = 1,  ///< Hill 2017 ACES fit (production quality)
    kHable     = 2,  ///< Hable / Uncharted 2 filmic (warmer highlights)
    kAgx       = 3,  ///< Sobotka 2022 AGX (saturation-preserving)
};

// ---- Narkowicz ACES ---------------------------------------------------------
[[nodiscard]] inline cd::math::Vec3f narkowicz(cd::math::Vec3f c) noexcept
{
    // Standard 5-term rational fit; produces deep blacks + soft highlights.
    constexpr float a = 2.51F;
    constexpr float b = 0.03F;
    constexpr float cc = 2.43F;
    constexpr float d = 0.59F;
    constexpr float e = 0.14F;
    auto apply = [&](float x) {
        return std::clamp((x * (a * x + b)) / (x * (cc * x + d) + e),
                          0.0F, 1.0F);
    };
    return { apply(c.x), apply(c.y), apply(c.z) };
}

// ---- Hill ACES (production fit) ---------------------------------------------
[[nodiscard]] inline cd::math::Vec3f hill_aces(cd::math::Vec3f c) noexcept
{
    // RRT_to_ODT scaled approximation. Filament uses the same coefficients.
    auto apply = [](float v) {
        const float num = v * (v + 0.0245786F) - 0.000090537F;
        const float den = v * (0.983729F * v + 0.4329510F) + 0.238081F;
        return std::clamp(num / den, 0.0F, 1.0F);
    };
    return { apply(c.x), apply(c.y), apply(c.z) };
}

// ---- Hable / Uncharted 2 filmic ---------------------------------------------
[[nodiscard]] inline cd::math::Vec3f hable(cd::math::Vec3f c) noexcept
{
    // Hable's S-curve parameters (Uncharted 2 talk, GDC 2010).
    constexpr float A = 0.15F;
    constexpr float B = 0.50F;
    constexpr float C = 0.10F;
    constexpr float D = 0.20F;
    constexpr float E = 0.02F;
    constexpr float F = 0.30F;
    constexpr float W = 11.2F;
    auto curve = [&](float x) {
        return ((x * (A * x + C * B) + D * E) /
                (x * (A * x + B) + D * F)) - E / F;
    };
    const float white_scale = 1.0F / curve(W);
    auto apply = [&](float v) {
        return std::clamp(curve(v) * white_scale, 0.0F, 1.0F);
    };
    return { apply(c.x), apply(c.y), apply(c.z) };
}

// ---- AGX (Sobotka 2022) -----------------------------------------------------
[[nodiscard]] inline cd::math::Vec3f agx(cd::math::Vec3f c) noexcept
{
    // 1) Input -> AGX "log" space via a stop-clipped log2 mapping.
    // 2) Apply per-channel 6th-order polynomial S-curve.
    // 3) Clamp to display gamut.
    constexpr float kMinEv = -12.47393F;
    constexpr float kMaxEv =   4.026069F;
    constexpr float kRange =  kMaxEv - kMinEv;
    auto to_log = [&](float v) {
        const float lv = std::log2(std::max(v, 1e-10F));
        return std::clamp((lv - kMinEv) / kRange, 0.0F, 1.0F);
    };
    auto s_curve = [](float x) {
        const float x2 = x  * x;
        const float x4 = x2 * x2;
        return  15.5F      * x4 * x2
              - 40.14F     * x4 * x
              + 31.96F     * x4
              -  6.868F    * x2 * x
              +  0.4298F   * x2
              +  0.1191F   * x
              -  0.00232F;
    };
    auto apply = [&](float v) {
        return std::clamp(s_curve(to_log(v)), 0.0F, 1.0F);
    };
    return { apply(c.x), apply(c.y), apply(c.z) };
}

// ---- Dispatcher -------------------------------------------------------------
[[nodiscard]] inline cd::math::Vec3f tonemap(cd::math::Vec3f c, Operator op) noexcept
{
    switch (op)
    {
        case Operator::kNarkowicz: return narkowicz(c);
        case Operator::kHill:      return hill_aces(c);
        case Operator::kHable:     return hable(c);
        case Operator::kAgx:       return agx(c);
    }
    return c;
}

// ---- GLSL source strings ----------------------------------------------------
// Drop-in fragment-shader helpers. Each defines a `vec3 cd_tonemap(vec3 c)`
// implementing the same math as the CPU functions above.

constexpr std::string_view kNarkowiczGlsl = R"glsl(
vec3 cd_tonemap(vec3 c) {
  const float a = 2.51, b = 0.03, cc = 2.43, d = 0.59, e = 0.14;
  return clamp((c * (a*c + b)) / (c * (cc*c + d) + e), vec3(0.0), vec3(1.0));
}
)glsl";

constexpr std::string_view kHillAcesGlsl = R"glsl(
vec3 cd_tonemap(vec3 x) {
  vec3 a = x * (x + 0.0245786) - 0.000090537;
  vec3 b = x * (0.983729 * x + 0.4329510) + 0.238081;
  return clamp(a / b, vec3(0.0), vec3(1.0));
}
)glsl";

constexpr std::string_view kHableGlsl = R"glsl(
vec3 cd_tonemap_curve(vec3 x) {
  const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
  return ((x * (A*x + C*B) + D*E) / (x * (A*x + B) + D*F)) - E/F;
}
vec3 cd_tonemap(vec3 c) {
  const float W = 11.2;
  vec3 ws = vec3(1.0) / cd_tonemap_curve(vec3(W));
  return clamp(cd_tonemap_curve(c) * ws, vec3(0.0), vec3(1.0));
}
)glsl";

constexpr std::string_view kAgxGlsl = R"glsl(
float cd_agx_log(float v) {
  const float kMinEv = -12.47393, kMaxEv = 4.026069;
  return clamp((log2(max(v, 1e-10)) - kMinEv) / (kMaxEv - kMinEv), 0.0, 1.0);
}
float cd_agx_curve(float x) {
  float x2 = x*x, x4 = x2*x2;
  return  15.5*x4*x2 - 40.14*x4*x + 31.96*x4
        -  6.868*x2*x + 0.4298*x2 + 0.1191*x - 0.00232;
}
vec3 cd_tonemap(vec3 c) {
  return clamp(vec3(cd_agx_curve(cd_agx_log(c.r)),
                    cd_agx_curve(cd_agx_log(c.g)),
                    cd_agx_curve(cd_agx_log(c.b))),
               vec3(0.0), vec3(1.0));
}
)glsl";

[[nodiscard]] constexpr std::string_view glsl_for(Operator op) noexcept
{
    switch (op)
    {
        case Operator::kNarkowicz: return kNarkowiczGlsl;
        case Operator::kHill:      return kHillAcesGlsl;
        case Operator::kHable:     return kHableGlsl;
        case Operator::kAgx:       return kAgxGlsl;
    }
    return kNarkowiczGlsl;
}

}  // namespace cd::post::tonemap
