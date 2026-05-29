// =============================================================================
// CHROMODYNAMIC — cd/post/dof/Dof.hpp
// Day 10 — Depth of Field with hexagonal bokeh (Sousa 2013).
//
// Two-stage filter:
//   1) Compute Circle of Confusion (CoC) per pixel from depth +
//      camera params (aperture, focus distance, focal length).
//   2) Hexagonal bokeh gather: 3 directional box blurs at 60-degree
//      offsets, MIN-combined to form the hexagon. Sousa's
//      "next-gen post-processing in Killzone Shadow Fall".
//
// API:
//   * coc_pixels(depth, settings) — per-pixel CoC radius in pixels.
//   * GLSL CoC + hexagonal-blur kernels.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace cd::post::dof
{

struct CameraSettings
{
    float focal_length_mm  { 35.0F };
    float aperture_f_stop  { 2.8F };
    float focus_distance_m { 5.0F };
    float sensor_height_mm { 24.0F };
    float resolution_h_px  { 1080.0F };
    float max_coc_px       { 30.0F };
};

/// Thin-lens CoC formula. Given world-space depth, return the
/// projected CoC radius in pixels. Negative when in front of focus,
/// positive when behind — abs() to get blur magnitude.
[[nodiscard]] inline float
coc_pixels(float depth_m, const CameraSettings& s) noexcept
{
    if (depth_m <= 0.0F) return 0.0F;
    const float f_mm = s.focal_length_mm;
    const float f_m  = f_mm * 0.001F;
    const float A    = f_m / s.aperture_f_stop;  // aperture diameter (m)
    const float d_focus = s.focus_distance_m;
    const float coc_mm = std::abs(A * f_m *
                                  (depth_m - d_focus) /
                                  (depth_m * (d_focus - f_m))) * 1000.0F;
    const float coc_px = coc_mm / s.sensor_height_mm * s.resolution_h_px;
    return std::min(coc_px, s.max_coc_px);
}

// ---- GLSL kernels -----------------------------------------------------------

constexpr std::string_view kCocCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D depth;
layout(set = 0, binding = 1, r16f) uniform writeonly image2D coc;
layout(push_constant) uniform PC {
  vec2  size;
  float focal_length_mm;
  float aperture_f_stop;
  float focus_distance_m;
  float sensor_height_mm;
  float max_coc_px;
  float depth_to_m;
} pc;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  float d = texture(depth, uv).r * pc.depth_to_m;
  float f_m = pc.focal_length_mm * 0.001;
  float A   = f_m / pc.aperture_f_stop;
  float c_mm = abs(A * f_m * (d - pc.focus_distance_m) /
                   max(d * (pc.focus_distance_m - f_m), 1e-3)) * 1000.0;
  float c_px = min(c_mm / pc.sensor_height_mm * pc.size.y, pc.max_coc_px);
  imageStore(coc, ivec2(p), vec4(c_px));
}
)glsl";

constexpr std::string_view kHexBlurCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D color_in;
layout(set = 0, binding = 1) uniform sampler2D coc_in;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC {
  vec2  size;
  vec2  dir;
  uint  tap_count;
  uint  padding_;
} pc;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  float r = texture(coc_in, uv).r;
  vec2 px = pc.dir * r / pc.size;
  vec3 sum = vec3(0); float wsum = 0;
  for (uint i = 0; i < pc.tap_count; ++i) {
    float t = float(i) / float(pc.tap_count - 1) - 0.5;
    sum  += texture(color_in, uv + px * t).rgb;
    wsum += 1.0;
  }
  imageStore(dst, ivec2(p), vec4(sum / wsum, 1.0));
}
)glsl";

}  // namespace cd::post::dof
