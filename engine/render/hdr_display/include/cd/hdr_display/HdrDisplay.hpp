// =============================================================================
// CHROMODYNAMIC — cd/hdr_display/HdrDisplay.hpp
// Day 14 — HDR display output (HDR10 PQ + scRGB).
//
// API:
//   * pq_encode(L_nits) — SMPTE ST 2084 PQ EOTF^-1, encodes linear
//     light (cd/m^2) into the [0, 1] perceptual code value.
//   * pq_decode(code) — inverse, returns nits.
//   * linear_srgb_to_rec2020(linear_srgb) — primary-space matrix.
//   * scrgb_pack(linear, max_nits) — scale linear sRGB so 1.0 = max_nits
//     for the scRGB FP16 surface.
//
// References:
//   * SMPTE ST 2084:2014 (PQ EOTF).
//   * ITU-R BT.2020 (Rec.2020 primaries).
//   * Microsoft DirectX HDR docs (scRGB convention).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::hdr_display
{

constexpr float kPqM1 = 0.1593017578125F;
constexpr float kPqM2 = 78.84375F;
constexpr float kPqC1 = 0.8359375F;
constexpr float kPqC2 = 18.8515625F;
constexpr float kPqC3 = 18.6875F;
constexpr float kPqMaxNits = 10000.0F;

/// PQ (SMPTE ST 2084) encode. `nits` = linear cd/m^2 ∈ [0, 10000].
/// Returns the perceptually-uniform code value ∈ [0, 1].
[[nodiscard]] inline float pq_encode(float nits) noexcept
{
    const float Y = std::clamp(nits / kPqMaxNits, 0.0F, 1.0F);
    const float Ym = std::pow(Y, kPqM1);
    return std::pow((kPqC1 + kPqC2 * Ym) / (1.0F + kPqC3 * Ym), kPqM2);
}

/// PQ decode: code ∈ [0, 1] → linear cd/m^2.
[[nodiscard]] inline float pq_decode(float code) noexcept
{
    const float E = std::pow(std::clamp(code, 0.0F, 1.0F), 1.0F / kPqM2);
    const float num = std::max(E - kPqC1, 0.0F);
    const float den = kPqC2 - kPqC3 * E;
    return kPqMaxNits * std::pow(num / std::max(den, 1e-6F), 1.0F / kPqM1);
}

/// Bradford-style 3x3 matrix from linear sRGB (Rec.709 primaries) to
/// Rec.2020 primaries. Used before PQ encode for HDR10 outputs.
[[nodiscard]] inline cd::math::Vec3f
linear_srgb_to_rec2020(cd::math::Vec3f c) noexcept
{
    return {
        0.6274F * c.x + 0.3293F * c.y + 0.0433F * c.z,
        0.0691F * c.x + 0.9195F * c.y + 0.0114F * c.z,
        0.0164F * c.x + 0.0880F * c.y + 0.8956F * c.z };
}

/// scRGB packing: scale linear sRGB so 1.0 maps to `display_max_nits`.
/// Output is the FP16 framebuffer value Windows / Vulkan expect.
[[nodiscard]] inline cd::math::Vec3f
scrgb_pack(cd::math::Vec3f linear, float display_max_nits) noexcept
{
    // scRGB convention: 1.0 = 80 nits (the SDR reference white).
    const float k = display_max_nits / 80.0F;
    return { linear.x * k, linear.y * k, linear.z * k };
}

// ---- GLSL helpers -----------------------------------------------------------

constexpr std::string_view kHdrGlsl = R"glsl(
const float kM1 = 0.1593017578125;
const float kM2 = 78.84375;
const float kC1 = 0.8359375;
const float kC2 = 18.8515625;
const float kC3 = 18.6875;
vec3 cd_pq_encode(vec3 nits) {
  vec3 Y  = clamp(nits / 10000.0, vec3(0.0), vec3(1.0));
  vec3 Ym = pow(Y, vec3(kM1));
  return pow((kC1 + kC2 * Ym) / (1.0 + kC3 * Ym), vec3(kM2));
}
vec3 cd_linear_srgb_to_rec2020(vec3 c) {
  return mat3(0.6274, 0.0691, 0.0164,
              0.3293, 0.9195, 0.0880,
              0.0433, 0.0114, 0.8956) * c;
}
vec3 cd_scrgb_pack(vec3 linear, float max_nits) {
  return linear * (max_nits / 80.0);
}
)glsl";

}  // namespace cd::hdr_display
