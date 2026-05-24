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
  vec4 sun_dir;
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

vec3 sample_env(vec3 dir) {
  vec3 zenith  = vec3(0.18, 0.42, 0.85);
  vec3 horizon = vec3(0.78, 0.86, 0.96);
  vec3 ground  = vec3(0.10, 0.10, 0.14);
  float h = dir.y;
  if (h >= 0.0) return mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.6));
  return mix(horizon, ground, pow(clamp(-h, 0.0, 1.0), 0.5));
}

void main() {
  vec3 dir = ray_dir(v_ndc);
  vec3 sky = sample_env(dir);

  // Sun disk + soft glow aligned with the caller's directional light.
  vec3 L = normalize(-pc.sun_dir.xyz);
  float cos_a = clamp(dot(dir, L), 0.0, 1.0);
  float disk = smoothstep(0.9994, 0.9998, cos_a);
  float glow = pow(cos_a, 64.0);
  vec3 sun_color = vec3(1.0, 0.93, 0.82) * pc.sun_dir.w;
  vec3 result = sky + sun_color * (disk * 6.0 + glow * 0.5);

  // ACES Narkowicz tonemap — matches StandardPbrFS so sky and spheres
  // share a single tone-mapping curve. No mid-frame curve mismatch.
  const float a_ = 2.51;
  const float b_ = 0.03;
  const float c_ = 2.43;
  const float d_ = 0.59;
  const float e_ = 0.14;
  result = clamp((result * (a_ * result + b_)) /
                 (result * (c_ * result + d_) + e_),
                 vec3(0.0), vec3(1.0));
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
};

static_assert(sizeof(AnalyticalSkyPush) == 64,
              "AnalyticalSkyPush must equal 64 B");

}  // namespace cd::material
