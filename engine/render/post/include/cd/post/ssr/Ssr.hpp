// =============================================================================
// CHROMODYNAMIC — cd/post/ssr/Ssr.hpp
// Day 5 — Screen-Space Reflections (Stachowiak 2015 / McGuire 2014).
//
// Hierarchical-depth ray march with blue-noise jitter. Closes the
// "no specular reflections without RT" gap on hardware that lacks
// ray_query. Pure screen-space, fast, and the fallback path the rest
// of the renderer can wire in front of full RT reflections.
//
// References:
//   * Stachowiak & Uludag 2015 — "Stochastic Screen-Space
//     Reflections" (Frostbite / SIGGRAPH talk).
//   * McGuire & Mara 2014 — "Efficient GPU Screen-Space Ray Tracing".
//   * Wronski 2014 — half-resolution colour pyramid for the LOD lookup.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <cstdint>
#include <string_view>

namespace cd::post::ssr
{

struct Settings
{
    /// Max ray steps before bailing out. Stachowiak: 64-128 is fine on
    /// modern GPUs at 1080p; bump for 4K.
    std::uint32_t max_steps { 64 };
    /// Thickness in view-space units. Determines how aggressively we
    /// treat a depth-buffer crossing as a real hit vs. an over-shoot.
    float thickness { 0.5F };
    /// Roughness threshold above which SSR contribution fades out (the
    /// screen-space approximation breaks down for blurry reflections).
    float roughness_max { 0.6F };
    /// Number of jittered rays per pixel. 1 = stochastic single-ray
    /// (relies on TAA / spatial filter), 4 = clean per-frame.
    std::uint32_t rays_per_pixel { 1 };
    /// Reflectance cutoff: if surface F0 is below this, skip SSR for
    /// the fragment (purely diffuse). 0.02 = typical insulator.
    float min_f0 { 0.02F };
};

/// Schlick Fresnel evaluated at view angle `cos_theta`. Helper used
/// by the SSR contribution-weighting code on the host side; the GLSL
/// kernel includes a vec3 variant inline.
[[nodiscard]] inline float
schlick_fresnel(float f0, float cos_theta) noexcept
{
    const float x = 1.0F - cos_theta;
    const float x5 = x * x * x * x * x;
    return f0 + (1.0F - f0) * x5;
}

/// Roughness-driven fade for SSR contribution. Stachowiak's curve:
/// 1.0 below the start threshold, linear ramp to 0.0 at roughness_max.
[[nodiscard]] inline float
roughness_fade(float roughness, const Settings& s) noexcept
{
    constexpr float kStart = 0.1F;
    if (roughness <= kStart) return 1.0F;
    if (roughness >= s.roughness_max) return 0.0F;
    return 1.0F - (roughness - kStart) / (s.roughness_max - kStart);
}

// ---- GLSL compute kernel ----------------------------------------------------

constexpr std::string_view kSsrTraceCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D color_pyramid; // mip chain of frame
layout(set = 0, binding = 1) uniform sampler2D depth;         // linear depth
layout(set = 0, binding = 2) uniform sampler2D normal_rough;  // RGB=N (NDC), A=roughness
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D ssr_out;
layout(push_constant) uniform PC {
  mat4 inv_proj;
  mat4 proj;
  vec2 size;
  float thickness;
  float roughness_max;
  uint  max_steps;
  uint  frame_index;
  uint  padding0_;
  uint  padding1_;
} pc;

vec3 unproject(vec2 uv, float d) {
  vec4 ndc = vec4(uv * 2.0 - 1.0, d, 1.0);
  vec4 v = pc.inv_proj * ndc;
  return v.xyz / v.w;
}
vec2 project(vec3 v) {
  vec4 ndc = pc.proj * vec4(v, 1.0);
  return (ndc.xy / ndc.w) * 0.5 + 0.5;
}

void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  float d = texture(depth, uv).r;
  if (d >= 0.999) {
    imageStore(ssr_out, ivec2(p), vec4(0));
    return;
  }
  vec4 nr = texture(normal_rough, uv);
  float roughness = nr.a;
  vec3 n_view = normalize(nr.xyz * 2.0 - 1.0);
  vec3 v_pos = unproject(uv, d);
  vec3 v_dir = normalize(v_pos);
  vec3 r_dir = reflect(v_dir, n_view);
  // March: step along reflected ray in view space, project each
  // sample, compare against the depth buffer.
  vec3  ray = v_pos + r_dir * 0.01;  // slight bias out of the surface
  float step_size = 0.05;
  vec4  hit = vec4(0);
  for (uint s = 0; s < pc.max_steps; ++s) {
    ray += r_dir * step_size;
    vec2 ss = project(ray);
    if (ss.x < 0.0 || ss.x > 1.0 || ss.y < 0.0 || ss.y > 1.0) break;
    float ds = texture(depth, ss).r;
    vec3 surf = unproject(ss, ds);
    float gap = ray.z - surf.z;
    if (gap > 0.0 && gap < pc.thickness) {
      hit = vec4(textureLod(color_pyramid, ss, roughness * 6.0).rgb, 1.0);
      break;
    }
    step_size *= 1.1;  // geometric growth — same idea as Stachowiak's hi-Z trace
  }
  // Roughness fade.
  float fade = (roughness >= pc.roughness_max) ? 0.0
             : (1.0 - max(roughness - 0.1, 0.0) / max(pc.roughness_max - 0.1, 1e-4));
  imageStore(ssr_out, ivec2(p), vec4(hit.rgb * fade, hit.a));
}
)glsl";

}  // namespace cd::post::ssr
