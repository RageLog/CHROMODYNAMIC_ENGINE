// =============================================================================
// CHROMODYNAMIC — cd/volumetric_clouds/Clouds.hpp
// Day 17 — Volumetric clouds (Schneider 2017).
//
// Ray-march of a procedural Perlin-Worley density field. CPU
// reference + GLSL kernel.
//
// Reference: Andrew Schneider — "Real-Time Volumetric Cloudscapes"
// (Horizon Zero Dawn / SIGGRAPH 2017).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::volumetric_clouds
{

struct Settings
{
    float layer_bottom_km { 1.5F };
    float layer_top_km    { 5.0F };
    float coverage        { 0.5F };
    float density_scale   { 0.05F };
    std::uint32_t march_steps { 64 };
};

/// Compute the cloud layer height fraction for a position. Returns
/// 0 outside [layer_bottom, layer_top]; 1 at the middle of the layer.
[[nodiscard]] inline float
height_fraction(float altitude_km, const Settings& s) noexcept
{
    if (altitude_km < s.layer_bottom_km || altitude_km > s.layer_top_km) return 0.0F;
    const float t = (altitude_km - s.layer_bottom_km) /
                    std::max(s.layer_top_km - s.layer_bottom_km, 1e-3F);
    return std::sin(t * 3.14159265F);  // bell-shape
}

/// Remap helper from Schneider's talk. Maps input range [a, b] to
/// output range [c, d]; clamped.
[[nodiscard]] inline float
remap(float x, float a, float b, float c, float d) noexcept
{
    const float t = std::clamp((x - a) / std::max(b - a, 1e-5F), 0.0F, 1.0F);
    return c + t * (d - c);
}

/// Sample the density field at altitude with a noise value in [0, 1].
/// `coverage` ∈ [0, 1] thins or thickens the cloud cover.
[[nodiscard]] inline float
density(float altitude_km, float noise_01, const Settings& s) noexcept
{
    const float hf = height_fraction(altitude_km, s);
    if (hf <= 0.0F) return 0.0F;
    return std::max(0.0F, (noise_01 - (1.0F - s.coverage)) * hf * s.density_scale);
}

// ---- GLSL kernel ------------------------------------------------------------

constexpr std::string_view kCloudsMarchCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler3D noise_lo;
layout(set = 0, binding = 1) uniform sampler3D noise_hi;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC {
  vec2  size;
  vec3  cam_origin; float padding0;
  vec3  cam_forward; float padding1;
  vec3  cam_right;   float padding2;
  vec3  cam_up;      float padding3;
  vec4  sun_dir;
  vec4  layer_params;   // bottom, top, coverage, density
  uint  steps;
  uint  pad4_;
  uint  pad5_;
  uint  pad6_;
} pc;

float height_fraction(float alt) {
  float a = (alt - pc.layer_params.x) / max(pc.layer_params.y - pc.layer_params.x, 1e-3);
  return sin(a * 3.14159265);
}

void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 ndc = (vec2(p) + 0.5) / pc.size * 2.0 - 1.0;
  vec3 dir = normalize(pc.cam_forward + pc.cam_right * ndc.x + pc.cam_up * ndc.y);
  vec3 pos = pc.cam_origin;
  float trans = 1.0; vec3 lit = vec3(0);
  float step = (pc.layer_params.y - pc.layer_params.x) / float(pc.steps);
  for (uint i = 0; i < pc.steps; ++i) {
    pos += dir * step;
    float alt = pos.y;
    if (alt < pc.layer_params.x || alt > pc.layer_params.y) continue;
    vec3 uvw = pos * 0.01;
    float n_lo = texture(noise_lo, uvw).r;
    float n_hi = texture(noise_hi, uvw * 8.0).r;
    float n = n_lo * 0.5 + n_hi * 0.5;
    float hf = height_fraction(alt);
    float d = max(0.0, (n - (1.0 - pc.layer_params.z)) * hf * pc.layer_params.w);
    if (d > 0.0) {
      float t = exp(-d * step);
      vec3 sun = vec3(1.0, 0.95, 0.85);
      lit += sun * (1.0 - t) * trans;
      trans *= t;
      if (trans < 0.01) break;
    }
  }
  imageStore(dst, ivec2(p), vec4(lit, 1.0 - trans));
}
)glsl";

}  // namespace cd::volumetric_clouds
