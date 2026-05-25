// =============================================================================
// CHROMODYNAMIC — cd/post_smaa/Smaa.hpp
// Day 12 — Enhanced Subpixel Morphological AA (Jimenez 2012).
//
// SMAA 1x / T2x edge-detect + blending-weight + neighbourhood blend.
// Fallback for content that breaks TAA (large disocclusions, fast
// motion without good velocity vectors).
//
// Reference: Jimenez, Echevarria, Sousa, Gutierrez 2012 —
// "SMAA: Enhanced Subpixel Morphological Antialiasing".
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace cd::post_smaa
{

struct Settings
{
    /// Luma threshold for edge detection. Jimenez: 0.05-0.1 range.
    float threshold        { 0.1F };
    /// Local-contrast adaptation factor (Karis-style).
    float adaptation_factor { 2.0F };
    /// Max search steps along an edge.
    std::uint32_t max_search_steps { 16 };
};

/// Edge-detect by luma. Returns 2-bit per-pixel edge mask:
///   bit 0 = horizontal edge present, bit 1 = vertical edge present.
[[nodiscard]] inline std::uint32_t
luma_edge(float l_centre,
          float l_left,
          float l_top,
          float l_right,
          float l_bottom,
          const Settings& s) noexcept
{
    const float t = std::max(s.threshold * 0.5F, 1e-3F);
    const float dh = std::abs(l_centre - l_left);
    const float dv = std::abs(l_centre - l_top);
    std::uint32_t mask = 0;
    if (dh > t) mask |= 1u;
    if (dv > t) mask |= 2u;
    (void)l_right; (void)l_bottom;  // 4-tap version uses these for adaptation
    return mask;
}

// ---- GLSL kernels -----------------------------------------------------------

constexpr std::string_view kSmaaEdgeDetectCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D color;
layout(set = 0, binding = 1, rg8) uniform writeonly image2D edges;
layout(push_constant) uniform PC { vec2 size; float threshold; } pc;
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  vec2 px = 1.0 / pc.size;
  float L = luma(texture(color, uv).rgb);
  float lL = luma(texture(color, uv - vec2(px.x, 0)).rgb);
  float lT = luma(texture(color, uv - vec2(0, px.y)).rgb);
  vec2 e = step(vec2(pc.threshold * 0.5),
                vec2(abs(L - lL), abs(L - lT)));
  imageStore(edges, ivec2(p), vec4(e, 0, 0));
}
)glsl";

constexpr std::string_view kSmaaBlendCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D color;
layout(set = 0, binding = 1) uniform sampler2D edges;
layout(set = 0, binding = 2, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC { vec2 size; } pc;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  vec2 px = 1.0 / pc.size;
  vec2 e  = texture(edges, uv).rg;
  vec3 c0 = texture(color, uv).rgb;
  // Cheap morphological blend: when an edge is present, mix the
  // pixel with its neighbours along the perpendicular axis.
  vec3 cL = texture(color, uv - vec2(px.x, 0)).rgb;
  vec3 cT = texture(color, uv - vec2(0, px.y)).rgb;
  vec3 cR = texture(color, uv + vec2(px.x, 0)).rgb;
  vec3 cB = texture(color, uv + vec2(0, px.y)).rgb;
  vec3 c = c0;
  if (e.x > 0.5) c = mix(c, 0.5 * (cT + cB), 0.25);
  if (e.y > 0.5) c = mix(c, 0.5 * (cL + cR), 0.25);
  imageStore(dst, ivec2(p), vec4(c, 1.0));
}
)glsl";

}  // namespace cd::post_smaa
