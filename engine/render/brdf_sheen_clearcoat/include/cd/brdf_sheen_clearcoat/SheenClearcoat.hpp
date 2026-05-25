// =============================================================================
// CHROMODYNAMIC — cd/brdf_sheen_clearcoat/SheenClearcoat.hpp
// Day 21 — Charlie sheen + Filament clearcoat BRDF layers.
//
// API:
//   * charlie_d(roughness, n_dot_h) — Charlie distribution (Estevez
//     2017) for the sheen layer.
//   * v_neubelt(n_dot_v, n_dot_l) — Neubelt visibility (low-cost
//     fit for sheen, sums of inverse-distance squared).
//   * clearcoat_d_v(roughness, n_dot_h, n_dot_v, n_dot_l) — Filament
//     clearcoat D*V (uses GGX 0.25-min-roughness).
//   * Drop-in GLSL helpers.
//
// References:
//   * Estevez & Kulla 2017 — "Production Friendly Microfacet
//     Sheen BRDF" (Imageworks).
//   * Filament docs — clearcoat model.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::brdf_sheen_clearcoat
{

/// Charlie sheen distribution. Roughness in [0, 1]; n_dot_h is the
/// half-vector cosine.
[[nodiscard]] inline float
charlie_d(float roughness, float n_dot_h) noexcept
{
    const float alpha = std::max(roughness, 0.05F);
    const float inv_a = 1.0F / alpha;
    const float sin2  = std::max(0.0F, 1.0F - n_dot_h * n_dot_h);
    return (2.0F + inv_a) * std::pow(sin2, 0.5F * inv_a) /
           (2.0F * 3.14159265F);
}

/// Neubelt visibility for the sheen layer.
[[nodiscard]] inline float
v_neubelt(float n_dot_v, float n_dot_l) noexcept
{
    return 1.0F /
           (4.0F * (n_dot_l + n_dot_v - n_dot_l * n_dot_v) + 1e-4F);
}

/// Filament clearcoat D * V product. Uses GGX with a hard min
/// roughness floor of 0.045 to keep the layer from collapsing to a
/// mirror.
[[nodiscard]] inline float
clearcoat_d_v(float roughness,
              float n_dot_h,
              float n_dot_v,
              float n_dot_l) noexcept
{
    const float a   = std::max(roughness * roughness, 0.045F * 0.045F);
    const float a2  = a * a;
    const float den = (n_dot_h * n_dot_h) * (a2 - 1.0F) + 1.0F;
    const float D   = a2 / (3.14159265F * den * den);
    const float V   = 1.0F / (4.0F * n_dot_v * n_dot_l + 1e-4F);
    return D * V;
}

// ---- GLSL ------------------------------------------------------------------

constexpr std::string_view kSheenClearcoatGlsl = R"glsl(
float charlie_d(float r, float nh) {
  float a = max(r, 0.05);
  float i = 1.0 / a;
  float s2 = max(0.0, 1.0 - nh * nh);
  return (2.0 + i) * pow(s2, 0.5 * i) / 6.28318530;
}
float v_neubelt(float nv, float nl) {
  return 1.0 / (4.0 * (nl + nv - nl * nv) + 1e-4);
}
float clearcoat_dv(float r, float nh, float nv, float nl) {
  float a  = max(r * r, 0.045 * 0.045);
  float a2 = a * a;
  float d  = (nh * nh) * (a2 - 1.0) + 1.0;
  float D  = a2 / (3.14159265 * d * d);
  float V  = 1.0 / (4.0 * nv * nl + 1e-4);
  return D * V;
}
)glsl";

}  // namespace cd::brdf_sheen_clearcoat
