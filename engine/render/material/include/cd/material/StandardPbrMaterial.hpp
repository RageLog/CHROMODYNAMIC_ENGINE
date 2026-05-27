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
// R1: real IBL bindings — prefiltered specular cube (mip 0..N), diffuse
// irradiance cube, and split-sum BRDF LUT. Replace the prior
// analytical sample_env() with proper texture lookups so metallic F0
// chroma reads through to the reflection authentically.
layout(set = 0, binding = 1) uniform samplerCube cd_ibl_spec;
layout(set = 0, binding = 2) uniform samplerCube cd_ibl_diff;
layout(set = 0, binding = 3) uniform sampler2D   cd_brdf_lut;
const float kIblMaxMipLod = 5.0;  // spec cube has 6 mips (0..5)
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
// G-Buffer MRT (R3 foundation, phase 213+219).
// Pipelines with fewer color attachments silently drop the higher
// location writes (Vulkan spec). Pipelines with 4 attachments
// populate the full G-Buffer (normal/albedo/MR) for SSR, deferred
// shading, GI prep.
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_albedo;
layout(location = 3) out vec2 out_mr;

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

// ---- R6 BRDF additions (cd::brdf_sheen_clearcoat — Estevez/Filament) -------

// Charlie sheen distribution (Estevez 2017).
float charlie_d(float r, float nh) {
  float a = max(r, 0.05);
  float i = 1.0 / a;
  float s2 = max(0.0, 1.0 - nh * nh);
  return (2.0 + i) * pow(s2, 0.5 * i) / 6.28318530;
}
// Neubelt visibility for sheen.
float v_neubelt(float nv, float nl) {
  return 1.0 / (4.0 * (nl + nv - nl * nv) + 1e-4);
}
// Filament clearcoat D * V (GGX with 0.045 minimum roughness floor).
float clearcoat_dv(float r, float nh, float nv, float nl) {
  float a  = max(r * r, 0.045 * 0.045);
  float a2 = a * a;
  float d  = (nh * nh) * (a2 - 1.0) + 1.0;
  float D  = a2 / (3.14159265 * d * d);
  float V  = 1.0 / (4.0 * nv * nl + 1e-4);
  return D * V;
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
                 vec3 F0, vec3 light_color,
                 float sheen_strength, float clearcoat_strength)
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
  vec3 lobe = diffuse + specular;
  // Charlie sheen lobe (additive on top of base).
  if (sheen_strength > 0.001) {
    float Ds = charlie_d(roughness, NoH);
    float Vs = v_neubelt(NoV, NoL);
    // Match Filament's neutral sheen tint; user can scale further.
    vec3 sheen_col = vec3(0.95, 0.92, 0.88);
    lobe += sheen_col * Ds * Vs * sheen_strength;
  }
  // Filament clearcoat lobe — second specular layer with fixed F0
  // (4% dielectric), additive on the spec sum. Roughness reuses the
  // base material's, clamped by clearcoat_dv() to the 0.045 floor.
  if (clearcoat_strength > 0.001) {
    float DV_cc = clearcoat_dv(roughness, NoH, NoV, NoL);
    float F_cc  = 0.04 + 0.96 * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
    lobe += vec3(DV_cc * F_cc * clearcoat_strength);
  }
  return lobe * NoL * light_color;
}

void main() {
  vec3 albedo = pc.albedo.rgb;
  float metallic = clamp(pc.mr_amb.x, 0.0, 1.0);
  float roughness = clamp(pc.mr_amb.y, 0.04, 1.0);
  // R6 BRDF strengths packed into the reserved mr_amb slots:
  //   mr_amb.z = sheen strength    (Charlie + Neubelt)
  //   mr_amb.w = clearcoat strength (Filament 2-lobe)
  float sheen_s     = clamp(pc.mr_amb.z, 0.0, 1.0);
  float clearcoat_s = clamp(pc.mr_amb.w, 0.0, 1.0);

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
  // Rig magnitudes restored so metallic specular highlights have
  // enough energy to read as 'shiny'; chroma is preserved via the
  // post-tonemap 1.70× saturation pull-away at the end of main().
  float sun_i = pc.light_dir.w;
  vec3 C_key  = vec3(1.00, 0.93, 0.82) * sun_i * 0.90;
  vec3 C_fill = vec3(0.55, 0.70, 0.95) * sun_i * 0.25;
  vec3 C_rim  = vec3(1.00, 0.88, 0.70) * sun_i * 0.40;

  vec3 direct  = direct_lobe(N, V, L_key,  albedo, metallic, roughness, F0, C_key,  sheen_s, clearcoat_s);
       direct += direct_lobe(N, V, L_fill, albedo, metallic, roughness, F0, C_fill, sheen_s, clearcoat_s);
       direct += direct_lobe(N, V, L_rim,  albedo, metallic, roughness, F0, C_rim,  sheen_s, clearcoat_s);

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
    direct += direct_lobe(N, V, Lp, albedo, metallic, roughness, F0, col, sheen_s, clearcoat_s);
  }

  // R1: True split-sum IBL — uses bound cubemaps + LUT instead of the
  // analytical sample_env(). Karis 2013:
  //   IBL = kD * irradiance(N) * albedo +
  //         prefiltered(R, roughness * maxMip) * (F0 * brdf.x + brdf.y)
  // Gated by total scene-light energy so the IBL stays meaningfully
  // tied to the lit state (sun off + other lights on => IBL still
  // contributes; all lights off => IBL ≈ 0).
  // IBL gate: SUN ONLY. Non-sun lights are direct sources; they
  // illuminate via their own contribution and shouldn't synthesise a
  // global ambient lift. Metallic surfaces under non-sun-only lighting
  // will read black (no diffuse, no LTC-GGX yet); the R4 GI ship
  // restores indirect bounce.
  float ibl_gate = clamp(sun_i * 0.6, 0.0, 1.0);
  vec3  R           = reflect(-V, N);
  float spec_lod    = roughness * kIblMaxMipLod;
  vec3  prefiltered = textureLod(cd_ibl_spec, R, spec_lod).rgb;
  vec3  irradiance  = texture(cd_ibl_diff, N).rgb;
  vec2  brdf        = texture(cd_brdf_lut, vec2(clamp(NoV, 0.0, 1.0),
                                                clamp(roughness, 0.0, 1.0))).rg;
  vec3  ibl_F       = F0 * brdf.x + vec3(brdf.y);
  vec3  ibl_kD      = (vec3(1.0) - ibl_F) * (1.0 - metallic);
  vec3  ibl         = (ibl_kD * irradiance * albedo + prefiltered * ibl_F) *
                      ibl_gate * 0.6;

  vec3 color = direct + ibl;

  // R3: output linear HDR. The composite pass in hello_engine
  // (kCompositeFS) owns the tonemap + saturation pull-away + gamma
  // transform once at the swapchain step. PBR FS used to run these
  // inline; the off-screen render target lets us centralise them.

  out_color = vec4(color, pc.albedo.a);
  out_normal = vec4(N, 1.0);
  out_albedo = vec4(clamp(albedo, vec3(0.0), vec3(1.0)), 1.0);
  out_mr = vec2(clamp(metallic, 0.0, 1.0), clamp(roughness, 0.04, 1.0));
}
)glsl";

/// Push-constant block layout for the standard PBR shader.
/// 128 bytes — fits Vulkan's minimum push-constant range guarantee.
struct StandardPbrPush
{
    float mvp[16];
    float albedo[4];
    float mr_amb[4];     ///< x=metallic, y=roughness, z=sheen, w=clearcoat
    float camera_pos[4]; ///< xyz; w unused
    float light_dir[4];  ///< xyz=direction (will be negated in shader to get L); w=intensity
};

static_assert(sizeof(StandardPbrPush) == 128,
              "StandardPbrPush must equal 128 (Vulkan minimum push-constant range)");

}  // namespace cd::material
