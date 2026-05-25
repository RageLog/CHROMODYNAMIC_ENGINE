// =============================================================================
// CHROMODYNAMIC — cd/light_shafts/LightShafts.hpp
// Day 18 — Sun shafts / god rays.
//
// Two paths:
//   * Screen-space radial blur (Mitchell 2007 / Hoffman) — cheap,
//     occlusion-aware, runs on the back buffer after sky.
//   * Analytic single-scattering (Kim & Marsalek 2014 epipolar) — for
//     finely-detailed shafts at sunset.
//
// References:
//   * Mitchell 2007 — "Volumetric Light Scattering as a Post-Process".
//   * Kim & Marsalek 2014 — "Epipolar sampling for shadows".
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cd::light_shafts
{

struct Settings
{
    std::uint32_t samples { 64 };
    float density          { 0.95F };
    float decay            { 0.96F };
    float weight           { 0.5F };
    float exposure         { 0.15F };
};

/// Project a world-space sun direction to screen-space NDC.
/// `cam_forward / right / up` form the camera basis; returns
/// (u, v) ∈ [0, 1] where the shaft origin is.
[[nodiscard]] inline cd::math::Vec2f
sun_screen_pos(cd::math::Vec3f sun_world_dir,
               cd::math::Vec3f cam_forward,
               cd::math::Vec3f cam_right,
               cd::math::Vec3f cam_up,
               float aspect) noexcept
{
    // Project sun direction onto camera basis.
    const float fz = cam_forward.x * (-sun_world_dir.x) +
                     cam_forward.y * (-sun_world_dir.y) +
                     cam_forward.z * (-sun_world_dir.z);
    if (fz <= 0.0F) return { -1, -1 };
    const float fx = cam_right.x * (-sun_world_dir.x) +
                     cam_right.y * (-sun_world_dir.y) +
                     cam_right.z * (-sun_world_dir.z);
    const float fy = cam_up.x * (-sun_world_dir.x) +
                     cam_up.y * (-sun_world_dir.y) +
                     cam_up.z * (-sun_world_dir.z);
    return { (fx / fz / aspect) * 0.5F + 0.5F,
             (fy / fz) * 0.5F + 0.5F };
}

// ---- GLSL kernel ------------------------------------------------------------

constexpr std::string_view kRadialBlurCS = R"glsl(
#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) uniform sampler2D occlusion;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2D dst;
layout(push_constant) uniform PC {
  vec2  size;
  vec2  sun_uv;
  uint  samples;
  float density;
  float decay;
  float weight;
  float exposure;
  float padding_;
  float padding2_;
  float padding3_;
} pc;
void main() {
  uvec2 p = gl_GlobalInvocationID.xy;
  if (p.x >= uint(pc.size.x) || p.y >= uint(pc.size.y)) return;
  vec2 uv = (vec2(p) + 0.5) / pc.size;
  vec2 d = (uv - pc.sun_uv) / float(pc.samples) * pc.density;
  float il = 1.0; vec3 col = vec3(0);
  for (uint i = 0; i < pc.samples; ++i) {
    uv -= d;
    col += texture(occlusion, uv).rgb * il * pc.weight;
    il *= pc.decay;
  }
  imageStore(dst, ivec2(p), vec4(col * pc.exposure, 1.0));
}
)glsl";

}  // namespace cd::light_shafts
