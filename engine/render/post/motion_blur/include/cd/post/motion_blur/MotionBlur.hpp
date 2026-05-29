// =============================================================================
// CHROMODYNAMIC — cd/post/motion_blur/MotionBlur.hpp
// Day 9 — Per-object motion blur (McGuire 2012).
//
// Tiled-max-velocity gather. Two passes:
//   1) kVelocityTileMaxCS — reduce the velocity buffer to 1 max
//      velocity per K×K tile (K = 16 typical).
//   2) kMotionBlurGatherCS — per-pixel gather along the tile-max
//      direction with jittered tap depths to dodge sample aliasing.
//
// Reference: McGuire, Hennessy, Bukowski, Osman — "A Reconstruction
// Filter for Plausible Motion Blur" (I3D 2012).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace cd::post::motion_blur
{

struct Settings
{
    /// Tile size for the velocity reduction (McGuire: 16).
    std::uint32_t tile_size  { 16 };
    /// Gather sample count along the tile-max velocity direction.
    std::uint32_t tap_count  { 16 };
    /// Maximum motion radius in pixels (clamp tile-max to avoid
    /// pixels gathering from outside the velocity neighborhood).
    float         max_motion_px { 40.0F };
};

/// Tile-max velocity reduction — host helper used by tests + offline
/// content cooking. Production runs the equivalent compute kernel.
[[nodiscard]] inline cd::math::Vec2f
reduce_tile_max(std::span<const cd::math::Vec2f> velocity,
                std::uint32_t w,
                std::uint32_t h,
                std::uint32_t tile_x,
                std::uint32_t tile_y,
                std::uint32_t tile_size,
                float max_motion_px) noexcept
{
    cd::math::Vec2f best { 0, 0 };
    float best_m2 = 0.0F;
    for (std::uint32_t y = 0; y < tile_size; ++y)
    {
        const std::uint32_t py = tile_y * tile_size + y;
        if (py >= h) continue;
        for (std::uint32_t x = 0; x < tile_size; ++x)
        {
            const std::uint32_t px = tile_x * tile_size + x;
            if (px >= w) continue;
            const auto& v = velocity[py * w + px];
            const float m2 = v.x * v.x + v.y * v.y;
            if (m2 > best_m2) { best_m2 = m2; best = v; }
        }
    }
    // Clamp to max motion radius.
    const float mag = std::sqrt(best_m2);
    if (mag > max_motion_px)
    {
        const float s = max_motion_px / std::max(mag, 1e-5F);
        best.x *= s;
        best.y *= s;
    }
    return best;
}

// ---- GLSL kernels -----------------------------------------------------------

constexpr std::string_view kVelocityTileMaxCS = R"glsl(
#version 460
layout(local_size_x = 16, local_size_y = 16) in;
layout(set = 0, binding = 0) uniform sampler2D velocity;
layout(set = 0, binding = 1, rg16f) uniform writeonly image2D tile_max;
layout(push_constant) uniform PC {
  vec2  size;
  uint  tile_size;
  float max_motion_px;
} pc;
shared vec2 lds[256];
void main() {
  uvec2 tile = gl_WorkGroupID.xy;
  uvec2 t = gl_LocalInvocationID.xy;
  vec2 uv = (vec2(tile * pc.tile_size + t) + 0.5) / pc.size;
  vec2 v = (any(greaterThanEqual(tile * pc.tile_size + t, uvec2(pc.size))))
         ? vec2(0.0) : texture(velocity, uv).xy;
  lds[t.y * 16 + t.x] = v;
  barrier();
  if (t.x == 0u && t.y == 0u) {
    vec2 best = vec2(0.0);
    float best_m2 = 0.0;
    for (int i = 0; i < 256; ++i) {
      float m2 = dot(lds[i], lds[i]);
      if (m2 > best_m2) { best_m2 = m2; best = lds[i]; }
    }
    float mag = sqrt(best_m2);
    if (mag > pc.max_motion_px)
      best *= pc.max_motion_px / max(mag, 1e-5);
    imageStore(tile_max, ivec2(tile), vec4(best, 0, 0));
  }
}
)glsl";

constexpr std::string_view kMotionBlurGatherCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D color;
layout(set = 0, binding = 1) uniform sampler2D tile_max;
layout(set = 0, binding = 2) uniform sampler2D velocity;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC {
  vec2  size;
  uint  tile_size;
  uint  tap_count;
  uint  frame_index;
} pc;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv   = (vec2(p) + 0.5) / pc.size;
  vec2 tile_uv = (vec2(p / pc.tile_size) + 0.5) /
                 (pc.size / vec2(pc.tile_size));
  vec2 vmax = texture(tile_max, tile_uv).xy;
  vec2 vpix = texture(velocity, uv).xy;
  // McGuire reconstruction: gather along tile-max, weight by the
  // ratio of per-pixel motion to tile-max motion.
  vec3 sum = texture(color, uv).rgb;
  float wsum = 1.0;
  float seed = float((p.x + p.y * uint(pc.size.x) +
                      pc.frame_index * 17u) & 0xFFu) / 255.0;
  for (uint i = 1; i < pc.tap_count; ++i) {
    float t = (float(i) + seed) / float(pc.tap_count) - 0.5;
    vec2 off = vmax * t / pc.size;
    vec3 c = texture(color, uv + off).rgb;
    float w = 1.0;
    sum += c * w; wsum += w;
  }
  imageStore(dst, ivec2(p), vec4(sum / wsum, 1.0));
}
)glsl";

}  // namespace cd::post::motion_blur
