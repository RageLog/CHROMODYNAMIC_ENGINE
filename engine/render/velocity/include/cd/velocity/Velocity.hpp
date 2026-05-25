// =============================================================================
// CHROMODYNAMIC — cd/velocity/Velocity.hpp
// Day 24 — Velocity / motion-vector buffer.
//
// Per-pixel screen-space motion vector (curr_uv -> prev_uv). Required
// upstream for TAA (Day 11), per-object motion blur (Day 9), ReSTIR
// temporal reuse (Day 6/7), and most SOTA denoisers.
//
// Pipeline contract:
//   * Each draw pushes its prev_model + curr_model matrices.
//   * VS computes prev_clip = prev_vp * prev_model * pos and
//     curr_clip = curr_vp * curr_model * pos.
//   * FS writes (curr.uv - prev.uv) into an RG16F image.
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::velocity
{

/// World-space point projected through both prev and curr VP, returns
/// the screen-space motion vector (curr_uv - prev_uv) in [-1, 1].
/// `prev_vp_pos` / `curr_vp_pos` are the clip-space projections of
/// the same world point — caller does the projection. Returns zero
/// vector when either projection is behind the near plane.
[[nodiscard]] inline cd::math::Vec2f
motion_vector_uv(cd::math::Vec4f prev_clip, cd::math::Vec4f curr_clip) noexcept
{
    if (prev_clip.w <= 0.0F || curr_clip.w <= 0.0F) return { 0.0F, 0.0F };
    const float prev_u = (prev_clip.x / prev_clip.w) * 0.5F + 0.5F;
    const float prev_v = (prev_clip.y / prev_clip.w) * 0.5F + 0.5F;
    const float curr_u = (curr_clip.x / curr_clip.w) * 0.5F + 0.5F;
    const float curr_v = (curr_clip.y / curr_clip.w) * 0.5F + 0.5F;
    return { curr_u - prev_u, curr_v - prev_v };
}

/// Pixel-space motion magnitude (useful for adaptive sampling
/// decisions in TAA / denoisers).
[[nodiscard]] inline float
motion_pixels(cd::math::Vec2f uv_delta, std::uint32_t width, std::uint32_t height) noexcept
{
    const float dx = uv_delta.x * static_cast<float>(width);
    const float dy = uv_delta.y * static_cast<float>(height);
    return std::sqrt(dx * dx + dy * dy);
}

// ---- GLSL kernels -----------------------------------------------------------

constexpr std::string_view kVelocityVS = R"glsl(
#version 460
layout(push_constant) uniform PC {
  mat4 prev_vp_model;   // prev_vp * prev_model
  mat4 curr_vp_model;   // curr_vp * curr_model
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 0) out vec4 v_prev_clip;
layout(location = 1) out vec4 v_curr_clip;
void main() {
  v_prev_clip = pc.prev_vp_model * vec4(in_pos, 1.0);
  v_curr_clip = pc.curr_vp_model * vec4(in_pos, 1.0);
  gl_Position = v_curr_clip;
  gl_Position.y = -gl_Position.y;
}
)glsl";

constexpr std::string_view kVelocityFS = R"glsl(
#version 460
layout(location = 0) in vec4 v_prev_clip;
layout(location = 1) in vec4 v_curr_clip;
layout(location = 0) out vec2 out_motion;
void main() {
  vec2 prev_uv = (v_prev_clip.xy / v_prev_clip.w) * 0.5 + 0.5;
  vec2 curr_uv = (v_curr_clip.xy / v_curr_clip.w) * 0.5 + 0.5;
  out_motion = curr_uv - prev_uv;
}
)glsl";

}  // namespace cd::velocity
