// =============================================================================
// CHROMODYNAMIC — cd/material/AnalyticalSkyMaterial.hpp
// Phase 105 / Wave 272 — engine-side analytical-atmosphere skybox shader.
//
// Companion to StandardPbrMaterial. Renders a fullscreen-triangle sky
// without any cubemap upload: reconstructs the world-space view ray
// per fragment from camera basis vectors, evaluates the same 3-band
// atmospheric palette the StandardPbr IBL term samples, and draws a
// sun disk co-located with the caller-supplied direction.
//
// Same palette as `StandardPbrFS::sample_env` so spheres' IBL
// reflections agree with the background they sit on.
//
// Push constant layout (64 bytes — well under the 128 B Vulkan
// minimum so it can stack with another tiny push block if needed):
//   0   vec4 cam_right  (xyz=right basis, w=half_w = tan(fov/2)*aspect)
//   16  vec4 cam_up     (xyz=up    basis, w=half_h = tan(fov/2))
//   32  vec4 cam_fwd    (xyz=forward, w=_)
//   48  vec4 sun_dir    (xyz=direction (toward target), w=intensity)
//
// Recommended pipeline state for the sky material:
//   * vertex bindings = empty (fullscreen triangle via gl_VertexIndex)
//   * depth_test  = false
//   * depth_write = false
//   * cull_mode   = kNone
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>

namespace cd::material
{

constexpr const char* kAnalyticalSkyVS = R"glsl(
#version 450
// Vertex index 0,1,2 maps to (-1,-1), (3,-1), (-1,3) — covers full NDC
// after clip rejection of the offscreen triangle vertex. No VB bound.
layout(location = 0) out vec2 v_ndc;
void main() {
  vec2 p = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0,
                (gl_VertexIndex == 2) ? 3.0 : -1.0);
  v_ndc = p;
  gl_Position = vec4(p, 1.0, 1.0);  // z=1 — pin to back of depth range.
}
)glsl";

constexpr const char* kAnalyticalSkyFS = R"glsl(
#version 450
layout(push_constant) uniform SkyPC {
  vec4 cam_right;
  vec4 cam_up;
  vec4 cam_fwd;
  vec4 sun_dir;    // xyz = direction, w = intensity
  vec4 sun_color;  // xyz = linear RGB (CCT-converted), w = horizon tint blend [0,1]
} pc;
layout(location = 0) in  vec2 v_ndc;
layout(location = 0) out vec4 out_color;

vec3 ray_dir(vec2 ndc) {
  vec3 forward = pc.cam_fwd.xyz;
  vec3 right   = pc.cam_right.xyz;
  vec3 up      = pc.cam_up.xyz;
  return normalize(forward
                 + ndc.x * pc.cam_right.w * right
                 - ndc.y * pc.cam_up.w    * up);
}

vec3 sample_env(vec3 dir, vec3 sky_tint) {
  // Base palette + per-frame sky tint from the sun's color. The
  // horizon gets a small fraction of sky_tint so sunsets show warm
  // glow, daylight stays neutral, night under blue moon stays cool.
  vec3 zenith  = mix(vec3(0.18, 0.42, 0.85), sky_tint * 0.30, 0.20);
  vec3 horizon = mix(vec3(0.78, 0.86, 0.96), sky_tint, 0.35);
  vec3 ground  = vec3(0.10, 0.10, 0.14);
  float h = dir.y;
  if (h >= 0.0) return mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.6));
  return mix(horizon, ground, pow(clamp(-h, 0.0, 1.0), 0.5));
}

void main() {
  vec3 dir = ray_dir(v_ndc);
  // Sky gradient is gated by sun intensity so a fully-disabled sun
  // produces a near-black sky (no sky = no ambient = matches the
  // PBR + prim shader lights-off baseline). Without this the sky
  // stays bright blue regardless and the user reads it as 'lit'.
  float sky_gate = clamp(pc.sun_dir.w, 0.0, 1.0);
  vec3 sky = sample_env(dir, pc.sun_color.rgb) * sky_gate;

  // Sun disk + soft glow aligned with the caller's directional light.
  // The sun color (CCT-converted) drives both disk and glow.
  vec3 L = normalize(-pc.sun_dir.xyz);
  float cos_a = clamp(dot(dir, L), 0.0, 1.0);
  float disk = smoothstep(0.9994, 0.9998, cos_a);
  float glow = pow(cos_a, 64.0);
  vec3 sun_color = pc.sun_color.rgb * pc.sun_dir.w;
  vec3 result = sky + sun_color * (disk * 6.0 + glow * 0.5);

  // AGX tonemap (Sobotka 2022) — matches StandardPbrFS + prim FS so
  // sky / PBR spheres / ECS primitives share a single saturation-
  // preserving curve. Closes the "sky tint washes everything to
  // pastel" anomaly that surfaced under the prior Narkowicz path.
  const float kMinEv = -12.47393;
  const float kMaxEv =   4.026069;
  vec3 lg = clamp((log2(max(result, vec3(1e-10))) - vec3(kMinEv)) /
                  (kMaxEv - kMinEv), vec3(0.0), vec3(1.0));
  vec3 x2 = lg * lg;
  vec3 x4 = x2 * x2;
  result = clamp( 15.5  * x4 * x2
               - 40.14 * x4 * lg
               + 31.96 * x4
               -  6.868 * x2 * lg
               +  0.4298 * x2
               +  0.1191 * lg
               -  0.00232, vec3(0.0), vec3(1.0));
  result = pow(result, vec3(1.0 / 2.2));
  out_color = vec4(result, 1.0);
}
)glsl";

/// Push-constant block for the analytical sky material — 64 bytes.
/// xyz of every vec4 is a basis vector / direction; w holds either
/// the half-FOV scaling factor or (for sun_dir) the intensity scalar.
struct AnalyticalSkyPush
{
    float cam_right[4]; ///< xyz = right basis, w = half_w
    float cam_up[4];    ///< xyz = up basis,    w = half_h
    float cam_fwd[4];   ///< xyz = forward,     w unused
    float sun_dir[4];   ///< xyz = direction,   w = intensity
    float sun_color[4]; ///< xyz = linear RGB,  w = sky tint blend [0,1]
};

static_assert(sizeof(AnalyticalSkyPush) == 80,
              "AnalyticalSkyPush must equal 80 B");

// =============================================================================
// CPU mirror of the GLSL sample_env() so offline IBL bakes (cd::ibl)
// produce the same horizon/zenith/ground gradient the runtime sky
// fragment shader draws. Match the 3-band palette exactly.
// =============================================================================

[[nodiscard]] inline cd::math::Vec3f sample_sky_cpu(cd::math::Vec3f dir) noexcept
{
    const cd::math::Vec3f zenith  { 0.50F, 0.58F, 0.72F };
    const cd::math::Vec3f horizon { 0.88F, 0.85F, 0.78F };
    const cd::math::Vec3f ground  { 0.18F, 0.16F, 0.14F };
    auto mix3 = [](cd::math::Vec3f a, cd::math::Vec3f b, float t) {
        return cd::math::Vec3f { a.x + (b.x - a.x) * t,
                                  a.y + (b.y - a.y) * t,
                                  a.z + (b.z - a.z) * t };
    };
    const float h = dir.y;
    if (h >= 0.0F)
        return mix3(horizon, zenith,
                    std::pow(std::clamp(h, 0.0F, 1.0F), 0.6F));
    return mix3(horizon, ground,
                std::pow(std::clamp(-h, 0.0F, 1.0F), 0.5F));
}

}  // namespace cd::material
