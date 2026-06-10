// =============================================================================
// CHROMODYNAMIC — cd/brdf/sss/Sss.hpp
// Day 20 — Burley pre-integrated SSS + Jiménez separable blur.
//
// API:
//   * burley_diffusion_profile(r, scale) — analytic SSS profile of
//     Burley 2015. r = world-space distance from shading point in mm.
//   * make_burley_kernel(taps, scale) — pre-integrated 1D kernel
//     suitable for the separable Jiménez 2010 horizontal/vertical
//     blur passes.
//   * GLSL helpers for the separable blur kernel.
//
// References:
//   * Burley 2015 — "Extending the Disney BRDF to a BSDF with
//     Integrated Subsurface Scattering" (SIGGRAPH 2015).
//   * Jiménez & Gutierrez 2010 — "Screen-Space Perceptual Rendering
//     of Human Skin" (Separable SSS).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cd::brdf::sss
{

/// Burley 2015 diffusion profile evaluated at radial distance `r`
/// (mm) for one wavelength. `s` is the channel-specific mean free
/// path. Returns the post-divide radial weight.
[[nodiscard]] inline float
burley_diffusion_profile(float r, float s) noexcept
{
    if (s < 1e-5F) return 0.0F;
    const float d = std::max(1e-5F, s);
    const float exp_a = std::exp(-r / d);
    const float exp_b = std::exp(-r / (3.0F * d));
    return (exp_a + exp_b) / (8.0F * 3.14159265F * d * r + 1e-5F);
}

struct Kernel1D
{
    /// Symmetric: tap 0 is the centre weight, taps 1..N-1 each
    /// represent +/- offset pairs.
    std::vector<float> weights;
    std::vector<float> offsets_mm;
};

/// Bake an N-tap 1D Jiménez separable kernel for the given Burley
/// scale. `radius_mm` is the half-width of the blur (typical 3 for
/// skin, 1 for low-density, 6+ for milky materials).
[[nodiscard]] inline Kernel1D
make_burley_kernel(std::uint32_t taps, float scale, float radius_mm)
{
    Kernel1D k {};
    if (taps == 0) return k;
    k.weights.assign(taps, 0.0F);
    k.offsets_mm.assign(taps, 0.0F);
    // Centre tap weight at r = 0.
    k.weights[0]   = burley_diffusion_profile(0.5F * radius_mm / static_cast<float>(taps),
                                              scale);
    k.offsets_mm[0] = 0.0F;
    // Symmetric outer taps — even spacing across [0, radius_mm].
    for (std::uint32_t i = 1; i < taps; ++i)
    {
        const float r = static_cast<float>(i) / static_cast<float>(taps - 1) * radius_mm;
        k.offsets_mm[i] = r;
        k.weights[i]    = burley_diffusion_profile(r, scale);
    }
    // Normalize so weights sum to 1 (centre + 2*outer).
    float sum = k.weights[0];
    for (std::uint32_t i = 1; i < taps; ++i) sum += 2.0F * k.weights[i];
    if (sum > 1e-5F) for (auto& w : k.weights) w /= sum;
    return k;
}

// ---- GLSL separable blur kernel ---------------------------------------------

constexpr std::string_view kSssSeparableBlurCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D src;
layout(set = 0, binding = 1) uniform sampler2D depth;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC {
  vec2  size;
  vec2  direction;     // (1,0) for horizontal pass, (0,1) for vertical
  float radius_mm;
  float depth_to_mm;
  uint  tap_count;
  uint  padding_;
} pc;

const int kMaxTaps = 16;
layout(set = 0, binding = 3) buffer Weights { float w[kMaxTaps]; vec2 off[kMaxTaps]; } K;

void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  float d0 = texture(depth, uv).r;
  vec3 col_sum = texture(src, uv).rgb * K.w[0];
  for (uint i = 1; i < pc.tap_count; ++i) {
    vec2 off = pc.direction * K.off[i].x / max(d0 * pc.depth_to_mm, 0.5);
    // Bilateral depth weight — skip taps that straddle a depth edge.
    float di = texture(depth, uv + off / pc.size).r;
    float dw = exp(-abs(di - d0) * 50.0);
    col_sum += texture(src, uv + off / pc.size).rgb * K.w[i] * dw;
    di = texture(depth, uv - off / pc.size).r;
    dw = exp(-abs(di - d0) * 50.0);
    col_sum += texture(src, uv - off / pc.size).rgb * K.w[i] * dw;
  }
  imageStore(dst, ivec2(p), vec4(col_sum, 1.0));
}
)glsl";

/// Cheap fragment-shader wrap-diffusion approximation used inline
/// when a full Burley separable-blur pass isn't available. Backlit
/// pixels receive a warm subsurface bleed.
constexpr std::string_view kInlineBurleyWrapGlsl = R"glsl(
vec3 sss_inline_wrap(vec3 N, vec3 L, float sun_intensity, float strength) {
  float backlit = clamp(dot(-N, L), 0.0, 1.0);
  vec3 tint = vec3(0.95, 0.55, 0.45);
  return tint * pow(backlit, 1.5) * strength * sun_intensity * 0.8;
}
)glsl";

}  // namespace cd::brdf::sss
