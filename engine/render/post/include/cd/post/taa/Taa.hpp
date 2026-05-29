// =============================================================================
// CHROMODYNAMIC — cd/post/taa/Taa.hpp
// Day 11 — Temporal Anti-Aliasing (Karis 2014).
//
// Reprojected-history blend with neighborhood AABB clipping. The
// fragment shader (kTaaResolveCS) samples:
//   * current frame colour (post-tonemap, pre-display)
//   * previous accumulator (history)
//   * velocity buffer (per-pixel motion vectors)
//   * 3x3 neighborhood of the current frame
// and computes a clamped temporal blend.
//
// References:
//   * Karis 2014 — "High Quality Temporal Supersampling" (UE4 talk).
//   * Salvi 2016 — "An Excursion in Temporal Supersampling" (clip
//     vs. clamp study).
//   * Pedersen 2016 — "Temporal Reprojection AA in INSIDE" (CG-clip
//     box variance).
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace cd::post::taa
{

struct Settings
{
    /// Min/max history blend weight. Karis recommends 0.04 (94% history,
    /// 6% current) when stable; pushed to 0.5 when motion is detected.
    float min_blend_factor { 0.04F };
    float max_blend_factor { 0.50F };
    /// Variance multiplier for the neighborhood clip box. Larger = more
    /// tolerant of disocclusion artifacts; smaller = sharper.
    float variance_clip_gamma { 1.0F };
    /// Velocity-magnitude scale that ramps blend factor from min -> max.
    /// 0.01 pixels of motion already counts as "disoccluding".
    float motion_blend_scale { 100.0F };
    /// Halton 2,3 jitter pattern length. UE4 uses 8.
    std::uint32_t jitter_phase_count { 8 };
};

/// Halton(b) sequence sample. Returns the `index`-th value of the
/// low-discrepancy Halton sequence in base `b`. Used to jitter the
/// projection matrix sub-pixel for TAA.
[[nodiscard]] inline float halton(std::uint32_t index, std::uint32_t b) noexcept
{
    float f = 1.0F;
    float r = 0.0F;
    while (index > 0)
    {
        f /= static_cast<float>(b);
        r += f * static_cast<float>(index % b);
        index /= b;
    }
    return r;
}

/// Per-frame sub-pixel jitter offset in [-0.5, 0.5] x [-0.5, 0.5].
/// Frame index advances; the host adds `(jx, jy) / extent` to the
/// projection matrix's [2][0] / [2][1] columns each frame, then
/// resolves with TAA which removes the resulting per-frame wiggle.
[[nodiscard]] inline cd::math::Vec2f
jitter_offset(std::uint32_t frame_index, std::uint32_t phase_count) noexcept
{
    const std::uint32_t i = (frame_index % phase_count) + 1;
    return { halton(i, 2) - 0.5F, halton(i, 3) - 0.5F };
}

/// AABB clip a history sample to the colour space spanned by `lo..hi`.
/// Karis line-clip variant: line from `centre` to `history`, find
/// the t in [0,1] where the line first exits the AABB; clamp `t`.
[[nodiscard]] inline cd::math::Vec3f
clip_aabb(cd::math::Vec3f lo,
          cd::math::Vec3f hi,
          cd::math::Vec3f centre,
          cd::math::Vec3f history) noexcept
{
    const cd::math::Vec3f dir { history.x - centre.x,
                                history.y - centre.y,
                                history.z - centre.z };
    auto t_for = [](float c, float d, float min, float max) {
        if (std::abs(d) < 1e-5F) return 1.0F;
        const float t1 = (min - c) / d;
        const float t2 = (max - c) / d;
        const float t_far = std::max(t1, t2);
        return t_far;
    };
    const float tx = t_for(centre.x, dir.x, lo.x, hi.x);
    const float ty = t_for(centre.y, dir.y, lo.y, hi.y);
    const float tz = t_for(centre.z, dir.z, lo.z, hi.z);
    const float t  = std::clamp(std::min({ tx, ty, tz }), 0.0F, 1.0F);
    return { centre.x + dir.x * t,
             centre.y + dir.y * t,
             centre.z + dir.z * t };
}

/// Compute the AABB extents from a 9-tap neighborhood: mean ± gamma * stddev.
struct Box
{
    cd::math::Vec3f lo;
    cd::math::Vec3f hi;
};
[[nodiscard]] inline Box
neighborhood_box(std::array<cd::math::Vec3f, 9> n, float gamma) noexcept
{
    cd::math::Vec3f mean { 0, 0, 0 };
    for (const auto& s : n)
    {
        mean.x += s.x; mean.y += s.y; mean.z += s.z;
    }
    constexpr float inv_n = 1.0F / 9.0F;
    mean.x *= inv_n; mean.y *= inv_n; mean.z *= inv_n;
    cd::math::Vec3f var { 0, 0, 0 };
    for (const auto& s : n)
    {
        const float dx = s.x - mean.x;
        const float dy = s.y - mean.y;
        const float dz = s.z - mean.z;
        var.x += dx * dx; var.y += dy * dy; var.z += dz * dz;
    }
    var.x *= inv_n; var.y *= inv_n; var.z *= inv_n;
    const cd::math::Vec3f stddev {
        std::sqrt(var.x) * gamma,
        std::sqrt(var.y) * gamma,
        std::sqrt(var.z) * gamma };
    return Box {
        { mean.x - stddev.x, mean.y - stddev.y, mean.z - stddev.z },
        { mean.x + stddev.x, mean.y + stddev.y, mean.z + stddev.z } };
}

/// CPU TAA resolve — given the current frame pixel, the reprojected
/// history pixel, the 3x3 neighborhood, and the motion-vector
/// magnitude, return the temporally-blended output. Same math as
/// kTaaResolveCS below.
[[nodiscard]] inline cd::math::Vec3f
resolve(cd::math::Vec3f curr,
        cd::math::Vec3f history,
        std::array<cd::math::Vec3f, 9> neighborhood,
        float motion_px,
        const Settings& s) noexcept
{
    const auto box = neighborhood_box(neighborhood, s.variance_clip_gamma);
    const auto clipped = clip_aabb(box.lo, box.hi, curr, history);
    const float t = std::clamp(motion_px * s.motion_blend_scale, 0.0F, 1.0F);
    const float blend = s.min_blend_factor + (s.max_blend_factor - s.min_blend_factor) * t;
    return { curr.x * blend + clipped.x * (1 - blend),
             curr.y * blend + clipped.y * (1 - blend),
             curr.z * blend + clipped.z * (1 - blend) };
}

// ---- GLSL compute kernel ----------------------------------------------------

constexpr std::string_view kTaaResolveCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D curr;
layout(set = 0, binding = 1) uniform sampler2D history;
layout(set = 0, binding = 2) uniform sampler2D velocity;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC {
  vec2  size;
  float min_blend;
  float max_blend;
  float variance_gamma;
  float motion_scale;
} pc;

vec3 clip_aabb(vec3 lo, vec3 hi, vec3 centre, vec3 hist) {
  vec3 dir = hist - centre;
  vec3 inv = 1.0 / max(abs(dir), vec3(1e-5));
  vec3 t1 = (lo - centre) * inv * sign(dir);
  vec3 t2 = (hi - centre) * inv * sign(dir);
  float t_far = max(max(max(t1.x, t1.y), t1.z),
                    max(max(t2.x, t2.y), t2.z));
  float t = clamp(t_far, 0.0, 1.0);
  return centre + dir * t;
}

void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  vec2 px = 1.0 / pc.size;
  // 3x3 neighborhood mean + variance.
  vec3 m1 = vec3(0); vec3 m2 = vec3(0);
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      vec3 s = texture(curr, uv + vec2(float(dx), float(dy)) * px).rgb;
      m1 += s; m2 += s * s;
    }
  }
  m1 /= 9.0; m2 /= 9.0;
  vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0))) * pc.variance_gamma;
  vec3 lo = m1 - sigma, hi = m1 + sigma;
  // Reproject history via velocity buffer.
  vec2 vel = texture(velocity, uv).xy;
  vec3 hist = texture(history, uv - vel).rgb;
  hist = clip_aabb(lo, hi, texture(curr, uv).rgb, hist);
  // Motion-aware blend.
  float motion_px = length(vel * pc.size);
  float t = clamp(motion_px * pc.motion_scale, 0.0, 1.0);
  float blend = pc.min_blend + (pc.max_blend - pc.min_blend) * t;
  vec3 out_rgb = mix(hist, texture(curr, uv).rgb, blend);
  imageStore(dst, ivec2(p), vec4(out_rgb, 1.0));
}
)glsl";

}  // namespace cd::post::taa
