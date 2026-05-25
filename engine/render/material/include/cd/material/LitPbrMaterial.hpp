// =============================================================================
// CHROMODYNAMIC — cd/material/LitPbrMaterial.hpp
// Phase 171 / v0.99.94 — Cook-Torrance PBR shader driven by cd::light.
//
// Successor to StandardPbrMaterial (Phase 104) — the legacy shader
// hardcoded a 3-light rig (warm key + cool fill + warm rim) into
// the fragment body. LitPbrMaterial pulls light data from a UBO
// owned by the renderer and iterates the runtime light list.
//
// Per fragment shader pass:
//
//   for each Light L in scene_lights[0..count):
//     resolve L vector + radiance per LightType
//       (directional / point / spot)
//     accumulate Cook-Torrance(N, V, L) * attenuation * mask
//   add IBL ambient (cubemap version of Phase 155 lands in Phase 172)
//   ACES tonemap + gamma
//
// Light record layout (UBO binding 0 set 0) MATCHES cd::light::Light
// std140-packed:
//
//   vec4 position_range   (xyz=position, w=range)
//   vec4 direction_intensity (xyz=direction, w=intensity)
//   vec4 color_kelvin     (xyz=color, w=kelvin or 0)
//   vec4 cone_params      (cos_inner, cos_outer, inv_cone_range, _)
//   vec4 area_tangent_w   (xyz=tangent, w=width)
//   vec4 area_bitangent_h (xyz=bitangent, w=height)
//   uvec4 slots           (shadow_slot, ies_profile, type, flags)
//   uvec4 _tail_pad       (-/-/-/-)
//
// 7×16 + 16 pad = 112 bytes per light, matching the C++ struct.
//
// Push constants (128 B):
//   mat4 mvp
//   vec4 albedo
//   vec4 mr_amb (metallic, roughness, _, _)
//   vec4 camera_pos
//   vec4 light_count_pad (x=light count, y=ibl strength, z=_, w=_)
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::material
{

constexpr std::uint32_t kLitPbrMaxLights = 32;

/// Vertex shader — same as StandardPbrMaterial Phase 104.
constexpr const char* kLitPbrVS = R"glsl(
#version 450
layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 albedo;
  vec4 mr_amb;
  vec4 camera_pos;
  vec4 light_count_pad;
} pc;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
void main() {
  v_world_pos = in_pos;
  v_normal    = in_normal;
  vec4 clip   = pc.mvp * vec4(in_pos, 1.0);
  clip.y      = -clip.y;
  gl_Position = clip;
}
)glsl";

/// Fragment shader. Iterates the LightsUbo[0..count) and dispatches
/// per type. Same Cook-Torrance + GGX + Schlick + Smith G as Phase 104,
/// but the light geometry comes from the runtime.
constexpr const char* kLitPbrFS = R"glsl(
#version 450
const uint kLightTypeDirectional = 0u;
const uint kLightTypePoint       = 1u;
const uint kLightTypeSpot        = 2u;

struct Light {
  vec4 position_range;       // xyz=pos, w=range
  vec4 direction_intensity;  // xyz=dir, w=intensity (lumens/lux)
  vec4 color_kelvin;         // xyz=color rgb, w=kelvin or 0
  vec4 cone_params;          // cos_inner, cos_outer, inv_cone_range, _
  vec4 area_tangent_w;       // xyz=tangent, w=width
  vec4 area_bitangent_h;     // xyz=bitangent, w=height
  uvec4 slots;               // shadow_slot, ies_profile, type, flags
  uvec4 _tail_pad;
};

layout(set = 0, binding = 0) uniform LightsUbo {
  Light lights[32];
} u_lights;

layout(push_constant) uniform PC {
  mat4 mvp;
  vec4 albedo;
  vec4 mr_amb;
  vec4 camera_pos;
  vec4 light_count_pad;  // x=count, y=ibl strength
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
vec3 F_Schlick_rough(float NoV, vec3 F0, float roughness) {
  vec3 ceiling = max(vec3(1.0 - roughness), F0);
  return F0 + (ceiling - F0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
}

// Frostbite windowed inverse-square distance attenuation.
float distance_atten(float d, float range) {
  if (range <= 0.0) return 0.0;
  float ratio = d / range;
  float w = clamp(1.0 - ratio*ratio*ratio*ratio, 0.0, 1.0);
  return (w * w) / (d * d + 0.01);
}

float cone_atten(float cos_theta, float cos_inner, float cos_outer) {
  if (cos_theta >= cos_inner) return 1.0;
  if (cos_theta <= cos_outer) return 0.0;
  float t = (cos_theta - cos_outer) / max(1e-5, cos_inner - cos_outer);
  return t * t;
}

vec3 direct_lobe(vec3 N, vec3 V, vec3 L,
                 vec3 albedo, float metallic, float roughness,
                 vec3 F0, vec3 radiance)
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
  return (diffuse + specular) * NoL * radiance;
}

// Phase 172 — real IBL cubemap samplers + split-sum BRDF LUT.
// Caller binds: set 0 binding 1 = irradiance cubemap (diffuse env),
//                set 0 binding 2 = pre-filtered specular cubemap (mips),
//                set 0 binding 3 = BRDF LUT (2D, RG, sampled at (NoV, roughness)).
// When the IBL strength push constant (light_count_pad.y) is 0,
// these samplers can be unbound — the analytical fallback covers.
layout(set = 0, binding = 1) uniform samplerCube u_irradiance;
layout(set = 0, binding = 2) uniform samplerCube u_prefiltered;
layout(set = 0, binding = 3) uniform sampler2D   u_brdf_lut;

// Analytical sky fallback (when IBL textures aren't bound / strength=0).
vec3 sample_env_analytical(vec3 dir) {
  vec3 zenith  = vec3(0.18, 0.42, 0.85);
  vec3 horizon = vec3(0.78, 0.86, 0.96);
  vec3 ground  = vec3(0.10, 0.10, 0.14);
  float h = dir.y;
  if (h >= 0.0) return mix(horizon, zenith, pow(clamp(h, 0.0, 1.0), 0.6));
  return mix(horizon, ground, pow(clamp(-h, 0.0, 1.0), 0.5));
}

void main() {
  vec3 albedo = pc.albedo.rgb;
  float metallic = clamp(pc.mr_amb.x, 0.0, 1.0);
  float roughness = clamp(pc.mr_amb.y, 0.04, 1.0);

  vec3 N = normalize(v_normal);
  vec3 V = normalize(pc.camera_pos.xyz - v_world_pos);
  float NoV = max(dot(N, V), 0.0);
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  // ---- Iterate runtime lights ----
  uint count = uint(pc.light_count_pad.x);
  if (count > 32u) count = 32u;
  vec3 direct = vec3(0.0);
  for (uint i = 0u; i < count; ++i) {
    Light L = u_lights.lights[i];
    uint type = L.slots.z;
    vec3 light_color = L.color_kelvin.rgb;
    float intensity = L.direction_intensity.w;

    vec3 L_dir;   // direction FROM surface TO light
    float atten = 1.0;

    if (type == kLightTypeDirectional) {
      L_dir = normalize(-L.direction_intensity.xyz);
      // intensity is lux — drives directly.
    } else {
      vec3 to_light = L.position_range.xyz - v_world_pos;
      float dist = length(to_light);
      if (dist < 1e-4) continue;
      L_dir = to_light / dist;
      atten = distance_atten(dist, L.position_range.w);
      if (type == kLightTypeSpot) {
        float cos_theta = dot(-L.direction_intensity.xyz, L_dir);
        atten *= cone_atten(cos_theta,
                            L.cone_params.x, L.cone_params.y);
      }
      // lumens → radiant intensity (point: /4π; spot ≈ /4π for now)
      intensity *= 0.0795775F;  // 1/(4π)
    }

    vec3 radiance = light_color * intensity * atten;
    direct += direct_lobe(N, V, L_dir, albedo, metallic, roughness, F0, radiance);
  }

  // ---- IBL ambient (split-sum: cubemap × BRDF LUT) ----
  // light_count_pad.y is the IBL strength multiplier.
  // light_count_pad.z >= 1 means "real cubemap textures bound";
  //   otherwise fall back to the analytical sky.
  vec3 R = reflect(-V, N);
  vec3 ibl_F  = F_Schlick_rough(NoV, F0, roughness);
  vec3 ibl_kD = (vec3(1.0) - ibl_F) * (1.0 - metallic);

  vec3 env_diffuse;
  vec3 prefiltered;
  if (pc.light_count_pad.z >= 1.0) {
    // Real IBL: irradiance cube for diffuse, prefiltered cube for
    // specular (LOD = roughness * max_mip), BRDF LUT for F0 scale+bias.
    env_diffuse = texture(u_irradiance, N).rgb;
    float max_mip = float(textureQueryLevels(u_prefiltered) - 1);
    prefiltered  = textureLod(u_prefiltered, R, roughness * max_mip).rgb;
  } else {
    env_diffuse = sample_env_analytical(N);
    prefiltered = mix(sample_env_analytical(R), env_diffuse, roughness);
  }
  vec2 brdf = texture(u_brdf_lut, vec2(NoV, roughness)).rg;
  // Split-sum specular reconstruction: F = F0 * scale + bias.
  vec3 specular_ibl = prefiltered * (F0 * brdf.x + vec3(brdf.y));

  vec3 ibl = ibl_kD * env_diffuse * albedo + specular_ibl;
  ibl *= pc.light_count_pad.y;

  vec3 color = direct + ibl;

  // ACES Narkowicz tonemap + gamma.
  const float a_ = 2.51, b_ = 0.03, c_ = 2.43, d_ = 0.59, e_ = 0.14;
  color = clamp((color * (a_ * color + b_)) /
                (color * (c_ * color + d_) + e_),
                vec3(0.0), vec3(1.0));
  color = pow(color, vec3(1.0 / 2.2));
  out_color = vec4(color, pc.albedo.a);
}
)glsl";

/// Push-constant block for LitPbrMaterial (128 B).
///   light_count_pad.x = light count (0..32)
///   light_count_pad.y = IBL strength multiplier (0..1)
///   light_count_pad.z = >=1 if real IBL cubemaps are bound, else analytical
///   light_count_pad.w = reserved (0)
struct LitPbrPush
{
    float mvp[16];
    float albedo[4];
    float mr_amb[4];          ///< metallic, roughness, _, _
    float camera_pos[4];
    float light_count_pad[4];
};

static_assert(sizeof(LitPbrPush) == 128, "LitPbrPush must equal 128 bytes");

/// std140-packed mirror of cd::light::Light for direct memcpy upload.
/// MUST match the GLSL `struct Light` layout above byte-for-byte.
struct LitPbrLightStd140
{
    float position_range[4];
    float direction_intensity[4];
    float color_kelvin[4];
    float cone_params[4];
    float area_tangent_w[4];
    float area_bitangent_h[4];
    std::uint32_t slots[4];     ///< shadow_slot, ies_profile, type, flags
    std::uint32_t tail_pad[4];
};

static_assert(sizeof(LitPbrLightStd140) == 128,
              "LitPbrLightStd140 std140 layout drifted (8x16 expected)");

}  // namespace cd::material

// Include after the namespace closes to keep the header self-contained
// without forcing cd::material consumers to drag in cd::light.
#include <cd/light/Light.hpp>

namespace cd::material
{

/// Pack a cd::light::Light into the std140-mirrored UBO record.
/// The conversion is a direct field copy — both sides agree on the
/// 7-vec4 prefix layout; the GPU side just gets a trailing 16-byte
/// pad slot for future flags / per-light userdata.
[[nodiscard]] inline LitPbrLightStd140
pack_light_std140(const cd::light::Light& l) noexcept
{
    LitPbrLightStd140 o {};
    o.position_range[0] = l.position.x; o.position_range[1] = l.position.y;
    o.position_range[2] = l.position.z; o.position_range[3] = l.range;
    o.direction_intensity[0] = l.direction.x; o.direction_intensity[1] = l.direction.y;
    o.direction_intensity[2] = l.direction.z; o.direction_intensity[3] = l.intensity;
    o.color_kelvin[0] = l.color.x; o.color_kelvin[1] = l.color.y;
    o.color_kelvin[2] = l.color.z; o.color_kelvin[3] = l.color_kelvin;
    o.cone_params[0] = l.cos_inner_cone; o.cone_params[1] = l.cos_outer_cone;
    o.cone_params[2] = l.inv_cone_range; o.cone_params[3] = 0.0F;
    o.area_tangent_w[0] = l.area_tangent.x; o.area_tangent_w[1] = l.area_tangent.y;
    o.area_tangent_w[2] = l.area_tangent.z; o.area_tangent_w[3] = l.area_width;
    o.area_bitangent_h[0] = l.area_bitangent.x; o.area_bitangent_h[1] = l.area_bitangent.y;
    o.area_bitangent_h[2] = l.area_bitangent.z; o.area_bitangent_h[3] = l.area_height;
    o.slots[0] = static_cast<std::uint32_t>(l.shadow_slot);
    o.slots[1] = l.ies_profile;
    o.slots[2] = static_cast<std::uint32_t>(l.type);
    o.slots[3] = l.flags;
    return o;
}

}  // namespace cd::material
