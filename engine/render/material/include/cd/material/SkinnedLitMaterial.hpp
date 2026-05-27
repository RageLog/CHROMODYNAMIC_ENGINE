// =============================================================================
// CHROMODYNAMIC - cd/material/SkinnedLitMaterial.hpp
// Phase 228 / SK6 - skinned-mesh material (4-weight LBS + simple Lambert).
//
// Companion to StandardPbrMaterial for *animated* glTF assets. Uses the
// existing cd::anim::kSkinningVertexShaderGlsl as the VS so the matrix-
// palette layout, weight-renormalise, and bone-ID encoding all live in one
// place (cd::anim, not duplicated here).
//
// Why a separate material instead of extending StandardPbrMaterial:
//   * Vertex layout differs (SkinnedVertex = 64 B with bone_ids + weights
//     vs StandardPbrPush's 2-attr setup).
//   * Set 0 binding 0 holds the SkinningMatricesUbo (256 mat4 = 16 KB)
//     instead of the lights UBO.
//   * Skinned PBR + IBL + LTC area lights need a longer follow-up — this
//     header targets visible CesiumMan playback first; full PBR comes next.
//
// Push constant layout (128 B):
//   0   mat4 mvp        (view-projection * model)
//   64  mat4 model      (world-space model matrix for normal transform)
// =============================================================================
#pragma once

#include <cd/anim/GpuSkinning.hpp>
#include <cd/core/Defines.hpp>

namespace cd::material
{

/// VS = the canonical 4-weight LBS shader from cd::anim. We re-export by
/// name here so the material file stays self-contained without forcing
/// callers to track the upstream identifier.
constexpr const char* kSkinnedLitVS = cd::anim::kSkinningVertexShaderGlsl;

/// Simple Lambert + Schlick-Fresnel ambient + sun-direction fragment shader.
/// One directional light + hemisphere ambient. No textures (base colour
/// comes from push.albedo). Skinned glTF assets that need PBR will move to
/// a SkinnedPbrMaterial follow-up.
constexpr const char* kSkinnedLitFS = R"glsl(
#version 460

layout(push_constant) uniform Push {
  mat4 mvp;
  mat4 model;
} pc;

layout(location = 0) in  vec3 v_world_pos;
layout(location = 1) in  vec3 v_world_normal;
layout(location = 2) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
// G-Buffer MRT (locations 1..3 silently dropped when the pipeline has
// fewer attachments — Vulkan spec).
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_albedo;
layout(location = 3) out vec2 out_mr;

const vec3 kSunDirWS    = normalize(vec3(0.35, 0.65, 0.7));
const vec3 kSunColor    = vec3(1.00, 0.95, 0.85);
const vec3 kSkyAmbient  = vec3(0.42, 0.50, 0.65);
const vec3 kGndAmbient  = vec3(0.18, 0.16, 0.14);
const vec3 kAlbedo      = vec3(0.82, 0.78, 0.72);  // warm flesh-ish neutral

void main() {
  vec3 N = normalize(v_world_normal);
  // Hemisphere ambient + Lambert key.
  float up_t = N.y * 0.5 + 0.5;
  vec3 ambient = mix(kGndAmbient, kSkyAmbient, up_t);
  float ndl = max(dot(N, kSunDirWS), 0.0);
  vec3 lit  = kAlbedo * (ambient + kSunColor * ndl);
  out_color = vec4(lit, 1.0);
  out_normal = vec4(N, 1.0);
  out_albedo = vec4(kAlbedo, 1.0);
  out_mr     = vec2(0.0, 0.7);  // dielectric, slightly rough
}
)glsl";

/// 128-byte push block: 2 mat4 (view-projection × model | model). Matches
/// the GLSL push declaration above and the cd::anim VS.
struct SkinnedLitPush
{
    float mvp[16];
    float model[16];
};

static_assert(sizeof(SkinnedLitPush) == 128,
              "SkinnedLitPush must equal 128 B (Vulkan minimum push range)");

}  // namespace cd::material
