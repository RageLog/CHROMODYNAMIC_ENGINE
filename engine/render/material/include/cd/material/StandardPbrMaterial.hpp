// =============================================================================
// CHROMODYNAMIC — cd/material/StandardPbrMaterial.hpp
// Phase 104 / Wave 271 — engine-side standard PBR shader source.
//
// Holds the canonical Cook-Torrance + GGX + Schlick-Fresnel + Smith-G
// shader source for the metallic/roughness workflow, plus an analytical
// split-sum IBL ambient (Karis 2013) using the same 3-band atmospheric
// palette the AnalyticalSkyMaterial / hello_skybox sample share. ACES
// Narkowicz tone-mapping at the end so highlight tint survives.
//
// The shader is intentionally header-only GLSL strings — material
// construction code looks up `kStandardPbrVS` / `kStandardPbrFS` and
// hands them to cd::shader::ICompiler. Samples no longer need to
// reproduce the BRDF inline; they just bind the StandardPbr material.
//
// Push constant layout (128 bytes, matches Vulkan minimum guarantee):
//   0   mat4   mvp                     (64 B)
//   64  vec4   albedo                  (16 B; alpha is opacity / cutout)
//   80  vec4   mr_amb (metallic, roughness, _, _)
//   96  vec4   camera_pos              (xyz; w unused)
//   112 vec4   light_dir               (xyz=direction; w=key intensity)
//
// Caller is responsible for the three-light rig parameters baked into
// the fragment shader. If you need to drive fill/rim independently,
// extend the push block — but the defaults are calibrated for the
// "warm key (PushBlock) + cool fill + warm back-rim" lighting in
// hello_pbr v5, which is intentionally artist-pleasing without a
// separate lighting UBO.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

namespace cd::material
{

/// Vertex shader — pos + normal, model identity, Vulkan Y-flip applied.
constexpr const char* kStandardPbrVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 albedo;
  vec4 mr_amb;
  vec4 camera_pos;
  vec4 light_dir;
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
void main() {
  v_world_pos = in_pos;
  v_normal    = in_normal;
  vec4 clip   = pc.mvp * vec4(in_pos, 1.0);
  clip.y      = -clip.y;  // Vulkan NDC Y is down.
  gl_Position = clip;
}
)glsl";

/// Fragment shader — Cook-Torrance + GGX + Schlick + Smith G,
/// analytical-sky split-sum IBL ambient, ACES Narkowicz tonemap.
constexpr const char* kStandardPbrFS = R"glsl(
#version 450
// Optional multi-light UBO — descriptor binding 0 (set 0). When the
// material's caller doesn't bind a UBO, cd_lights.count stays 0 and
// the loop is a no-op. Matches hello_engine's PrimFS LightSlot
// layout so a single LightUboGpu instance can drive both shaders.
struct CdLightSlot {
  vec4 pos_range;
  vec4 dir_type;
  vec4 color_int;
  vec4 extras;
};
layout(set = 0, binding = 0) uniform CdLightArray {
  // std140 packing: 'uint pad[3]' would be stride-16 (48 B) and push
  // slots[] to offset 64, but the C++ LightUboGpu uses packed
  // std::uint32_t pad[3] (12 B contiguous) with slots starting at
  // offset 16. Using 3 separate scalar uints matches the packed C++
  // layout — fixes the entire multi-light contribution being read
  // from a wrong offset on the GPU side.
  uint count;
  uint pad_a;
  uint pad_b;
  uint pad_c;
  CdLightSlot slots[8];
} cd_lights;
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 albedo;
  vec4 mr_amb;
  vec4 camera_pos;
  vec4 light_dir;
} pc;
layout(location = 0) in  vec3 v_world_pos;
layout(location = 1) in  vec3 v_normal;
layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979;

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d  = (NoH * NoH) * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d + 1e-7);
}

float G_SchlickGGX(float NoV, float k) {
  return NoV / (NoV * (1.0 - k) + k + 1e-7);
}

float G_Smith(float NoV, float NoL, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return G_SchlickGGX(NoV, k) * G_SchlickGGX(NoL, k);
}

vec3 F_Schlick(float HoV, vec3 F0) {
  return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
}

vec3 F_Schlick_roughness(float cos_theta, vec3 F0, float roughness) {
  vec3 ceiling = max(vec3(1.0 - roughness), F0);
  return F0 + (ceiling - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 sample_env(vec3 dir) {
  // Warmer / less-saturated env palette so polished metallic
  // spheres reflecting the sky preserve their base F0 chroma
  // instead of looking uniformly blue-grey-cream. Saturated
  // zenith was the prior cause of all 5 material rows reading
  // identically.
  vec3 zenith  = vec3(0.50, 0.58, 0.72);
  vec3 horizon = vec3(0.88, 0.85, 0.78);
  vec3 ground  = vec3(0.18, 0.16, 0.14);
  float h = dir.y;
  if (h >= 0.0) return mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.6));
  return mix(horizon, ground, pow(clamp(-h, 0.0, 1.0), 0.5));
}

vec3 direct_lobe(vec3 N, vec3 V, vec3 L,
                 vec3 albedo, float metallic, float roughness,
                 vec3 F0, vec3 light_color)
{
  vec3 H = normalize(L + V);
  float NoL = max(dot(N, L), 0.0);
  float NoV = max(dot(N, V), 0.0);
  float NoH = max(dot(N, H), 0.0);
  float HoV = max(dot(H, V), 0.0);
  float D = D_GGX(NoH, roughness * roughness);
  float G = G_Smith(NoV, NoL, roughness);
  vec3  F = F_Schlick(HoV, F0);
  vec3 specular = (D * G) * F / (4.0 * NoV * NoL + 1e-7);
  vec3 kS = F;
  vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
  vec3 diffuse = kD * albedo / PI;
  return (diffuse + specular) * NoL * light_color;
}

void main() {
  vec3 albedo = pc.albedo.rgb;
  float metallic = clamp(pc.mr_amb.x, 0.0, 1.0);
  float roughness = clamp(pc.mr_amb.y, 0.04, 1.0);

  vec3 N = normalize(v_normal);
  vec3 V = normalize(pc.camera_pos.xyz - v_world_pos);
  float NoV = max(dot(N, V), 0.0);
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  // Direct lighting — warm key (push) + cool fill + warm back-rim.
  // Intensities calibrated so HDR sum stays under the Hable shoulder
  // and metallic spheres preserve F0 chroma before tonemap.
  vec3 L_key  = normalize(-pc.light_dir.xyz);
  vec3 L_fill = normalize(vec3( 0.6, 0.3,  0.7));
  vec3 L_rim  = normalize(vec3(-0.1, 0.2, -1.0));
  float sun_i = pc.light_dir.w;
  vec3 C_key  = vec3(1.00, 0.93, 0.82) * sun_i * 0.95;
  vec3 C_fill = vec3(0.55, 0.70, 0.95) * sun_i * 0.28;
  vec3 C_rim  = vec3(1.00, 0.88, 0.70) * sun_i * 0.45;

  vec3 direct  = direct_lobe(N, V, L_key,  albedo, metallic, roughness, F0, C_key);
       direct += direct_lobe(N, V, L_fill, albedo, metallic, roughness, F0, C_fill);
       direct += direct_lobe(N, V, L_rim,  albedo, metallic, roughness, F0, C_rim);

  // Multi-light UBO contribution (#22 fix). Loops every enabled
  // non-sun light from the shared LightSlot UBO and folds it into
  // the direct term using the same physical lobe as the key/fill/
  // rim. Without this, disabling the sun made the sphere grid go
  // pitch-black even when point/spot lights were live.
  for (uint li = 0; li < cd_lights.count; ++li) {
    vec3 lp = cd_lights.slots[li].pos_range.xyz;
    float rng = cd_lights.slots[li].pos_range.w;
    if (rng <= 0.0) continue;
    vec3 to_p = lp - v_world_pos;
    float d  = length(to_p);
    if (d < 1e-4) continue;
    vec3 Lp = to_p / d;
    // Frostbite windowed inverse-square attenuation.
    float ratio = d / rng;
    float w_ = clamp(1.0 - ratio*ratio*ratio*ratio, 0.0, 1.0);
    float atten = (w_ * w_) / (d * d + 0.01);
    int ltp = int(cd_lights.slots[li].dir_type.w);
    float cone = 1.0;
    if (ltp == 2) {  // Spot
      vec3 axis = normalize(cd_lights.slots[li].dir_type.xyz);
      float cos_b = dot(-Lp, axis);
      float cos_out = cd_lights.slots[li].extras.x;
      float cos_in  = clamp(cos_out + 0.05, cos_out, 0.9999);
      cone = smoothstep(cos_out, cos_in, cos_b);
    }
    vec3 col = cd_lights.slots[li].color_int.xyz *
               cd_lights.slots[li].color_int.w * atten * cone;
    direct += direct_lobe(N, V, Lp, albedo, metallic, roughness, F0, col);
  }

  // IBL ambient (split-sum without BRDF LUT). Gated by TOTAL scene
  // light energy (sun + every enabled UBO light) so:
  //   - sun off + other lights on  => IBL still contributes (was the
  //     'sun off => black scene' bug)
  //   - all lights off             => IBL ≈ 0 (was the 'spheres glow
  //     with no light source' bug)
  // The gate uses sun_i + sum(color_int.w) clamped to [0,1].
  float total_light_e = sun_i;
  for (uint li2 = 0; li2 < cd_lights.count; ++li2) {
    if (cd_lights.slots[li2].pos_range.w <= 0.0) continue;
    total_light_e += cd_lights.slots[li2].color_int.w;
  }
  float ibl_gate = clamp(total_light_e * 0.6, 0.0, 1.0);
  vec3 R = reflect(-V, N);
  vec3 env_diffuse  = sample_env(N);
  vec3 env_specular = mix(sample_env(R), env_diffuse, roughness);
  vec3 ibl_F  = F_Schlick_roughness(NoV, F0, roughness);
  vec3 ibl_kD = (vec3(1.0) - ibl_F) * (1.0 - metallic);
  vec3 ibl    = (ibl_kD * env_diffuse * albedo + env_specular * ibl_F) *
                ibl_gate * 0.25;

  vec3 color = direct + ibl;

  // Hable (Uncharted 2) tonemap — preserves colour saturation in LDR
  // range better than AGX/Narkowicz. Matches prim pipeline so PBR
  // spheres + ECS primitives read with the same chromaticity.
  // F(x) = ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F)) - E/F
  const float A_ = 0.15, B_ = 0.50, C_ = 0.10, D_ = 0.20, E_ = 0.02, F_ = 0.30;
  const float W_ = 11.2;
  vec3 num   = color * (A_*color + C_*B_) + D_*E_;
  vec3 den   = color * (A_*color + B_)    + D_*F_;
  color      = num / den - E_/F_;
  float wnum = W_ * (A_*W_ + C_*B_) + D_*E_;
  float wden = W_ * (A_*W_ + B_)    + D_*F_;
  float wsc  = wnum / wden - E_/F_;
  color      = clamp(color / wsc, vec3(0.0), vec3(1.0));
  color      = pow(color, vec3(1.0 / 2.2));
  out_color = vec4(color, pc.albedo.a);
}
)glsl";

/// Push-constant block layout for the standard PBR shader.
/// 128 bytes — fits Vulkan's minimum push-constant range guarantee.
struct StandardPbrPush
{
    float mvp[16];
    float albedo[4];
    float mr_amb[4];     ///< metallic, roughness, _, _
    float camera_pos[4]; ///< xyz; w unused
    float light_dir[4];  ///< xyz=direction (will be negated in shader to get L); w=intensity
};

static_assert(sizeof(StandardPbrPush) == 128,
              "StandardPbrPush must equal 128 (Vulkan minimum push-constant range)");

}  // namespace cd::material
