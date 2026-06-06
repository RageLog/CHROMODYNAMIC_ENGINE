// SPDX: see PrimShader.hpp banner.
#pragma once

namespace cd::hello_engine
{

// AUTO-SYNCED with samples/engine/hello_engine/shaders/prim.frag.glsl
// (phase 465). Embedded fallback used when on-disk shaders/ directory
// is missing next to the binary. Keep in lockstep with the .glsl file
// — drift loses runtime fixes silently.
inline constexpr const char* kPrimFS = R"glsl(
#version 460
// Faz 1.7 - inline RT shadows via ray queries inside the raster FS.
// VK_KHR_ray_query is required at the device level; the material
// creation gates on device.features().ray_query so this extension
// guard never fires on unsupported hardware. GLSL 460 is required
// because ray-query intrinsics were introduced for that profile.
#extension GL_EXT_ray_query : require
layout(push_constant) uniform PC {
  mat4 mvp;
  mat4 model;
  vec4 tint;
  vec4 sun_dir;
  vec4 sun_color;
  vec4 fx_params;     // x=tonemap_op (0=Nark, 1=Hill, 2=Hable, 3=AGX)
  vec4 fx_params2;    // x=smaa, y=motion_blur, z=taa, w=dof (v1.4+)
  vec4 fx_params3;    // x=fog, y=atmosphere, z=clouds, w=light_shafts
  vec4 camera_pos;    // xyz=world camera (atmospherics distance)
  vec4 fx_params4;    // x=clearcoat, y=sheen, z=sss, w=reserved (R6)
} pc;
// Faz 1.6 CSM descriptors - match the VS layout.
layout(set = 0, binding = 0) uniform Shadow {
  mat4 light_vp;
} cd_shadow;
layout(set = 0, binding = 1) uniform sampler2D cd_shadow_map;
// Faz 1.7 TLAS - rebuilt every frame on the host with the current
// scene transforms. Used to shadow-test punctual / spot / area
// lights that CSM can't cover (CSM is single-directional only).
layout(set = 0, binding = 2) uniform accelerationStructureEXT cd_tlas;
// Faz 1.9 multi-light UBO (gap #2 + #3 foundation) - 8 non-sun
// lights with full type-specific data.
struct LightSlot {
  vec4 pos_range;   // xyz=world pos, w=range
  vec4 dir_type;    // xyz=direction or right-basis, w=type (0=Dir,1=Point,2=Spot,3=Rect,4=Disk)
  vec4 color_int;   // xyz=linear colour, w=intensity (scaled)
  vec4 extras;      // x=cos_outer, y=area_w, z=area_h, w=cos_inner
  vec4 tangent;     // W8-N: xyz=unit tangent (area-light rect local +X), w=reserved
};
layout(set = 0, binding = 3) uniform LightArray {
  // std140 packing: 'uint pad[3]' would be stride-16 (48 B) and push
  // slots[] to offset 64, but the C++ LightUboGpu uses packed
  // std::uint32_t pad[3] (12 B contiguous) with slots starting at
  // offset 16. Using 3 separate scalar uints matches the packed C++
  // layout - fixes the entire multi-light contribution being read
  // from a wrong offset on the GPU side.
  uint count;
  uint pad_a;
  uint pad_b;
  uint pad_c;
  LightSlot slots[8];
} cd_lights;
// R2 IBL-on-prim - same cubemaps + LUT the PBR pipeline binds.
layout(set = 0, binding = 5) uniform samplerCube cd_ibl_spec;
layout(set = 0, binding = 6) uniform samplerCube cd_ibl_diff;
layout(set = 0, binding = 7) uniform sampler2D   cd_brdf_lut;
// R2 procedural normal map (tangent-space bump).
layout(set = 0, binding = 8) uniform sampler2D   cd_normal_tex;
// R2 metallic-roughness-AO map. glTF 2.0 packing:
//   R unused, G roughness, B metallic, A AO
layout(set = 0, binding = 9) uniform sampler2D   cd_mr_tex;
// W8-BC + phase465-perprim per-(instance, geometry) material table.
// Slot index = instance_id * kMaxGeomsPerInst + geometry_index, where
// kMaxGeomsPerInst = 32 (matches HelloRayQuery.hpp).  Closest-hit rays
// fetch the pair via rayQueryGetIntersectionInstanceIdEXT +
// rayQueryGetIntersectionGeometryIndexEXT so multi-geometry BLAS hits
// (Sponza: vegetation / fabric / stone) sample THEIR OWN prim albedo
// rather than the single instance-level tint that ALL Sponza hits
// shared before phase465.  Single-geometry instances (procedural prims,
// CesiumMan, the editor floor) still fill geom slot 0 + replicate to
// 1..31 on the host so geom_index >= 1 reads back the same albedo.
struct InstanceMat { vec4 albedo; vec4 emissive; };
layout(set = 0, binding = 10) readonly buffer InstanceMats {
  InstanceMat data[];
} cd_instance_mats;
// phase798-rt-chrome-sponza-geom-cap: 32 -> 128 to cover Sponza's 103
// primitives (every curtain / column past slot 31 was clamping to 31).
const int kMaxGeomsPerInst = 128;
const int kMaxInstMatSlots = 8192;  // matches HelloRayQuery::kMaxInstMats
const float kIblMaxMipLod = 5.0;

// Cotangent-frame from screen-space derivatives (Mikkelsen 2010).
// Avoids needing per-vertex tangents - works for any UV-mapped mesh.
mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv) {
  vec3 dp1 = dFdx(p);
  vec3 dp2 = dFdy(p);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N);
  vec3 dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  // phase437-black: guard against degenerate UV (identical UVs on a
  // Sponza primitive / collapsed triangle → dFdx/dFdy == 0 →
  // max(dot(T,T), dot(B,B)) == 0 → inversesqrt(0) = +Inf →
  // TBN * nm_sample = NaN). Fall back to identity TBN (N unchanged).
  float denom = max(dot(T, T), dot(B, B));
  if (denom < 1e-10) return mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), N);
  float invmax = inversesqrt(denom);
  return mat3(T * invmax, B * invmax, N);
}
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec3 v_albedo;
layout(location = 3) in vec4 v_shadow_pos;
layout(location = 4) in vec2 v_uv;
// Optional baseColor texture (gap #1/#13). fx_params.y = 1.0
// flags the draw as 'sample texture'; 0.0 = use vertex-coloured
// albedo path. Single texture slot for hello_engine - production
// editor needs a per-entity texture array (v1.6+).
layout(set = 0, binding = 4) uniform sampler2D cd_albedo_tex;
layout(location = 0) out vec4 out_color;
// G-Buffer normal MRT - world-space surface normal (xyz) + flag (w=1
// surface, 0 = sky/transparent). Composite + post-fx pipeline samples
// this for SSR, normal-aware AO, future reflections.
layout(location = 1) out vec4 out_normal;
// G-Buffer albedo MRT - base color (rgb) + material flag (a). Used by
// SSR tinting, GI prep, deferred shading downstream.
layout(location = 2) out vec4 out_albedo;
// G-Buffer metallic/roughness MRT - packed pair (xy) for SSR rough
// blur + deferred BRDF + GI.
layout(location = 3) out vec2 out_mr;

// Frostbite windowed inverse-square attenuation.
float distance_atten(float d, float range) {
  if (range <= 0.0) return 0.0;
  float ratio = d / range;
  float w = clamp(1.0 - ratio*ratio*ratio*ratio, 0.0, 1.0);
  return (w * w) / (d * d + 0.01);
}

// Faz 1.7 inline RT shadow visibility test. Shoots a ray from the
// surface point toward `dir` for at most `tmax` metres. Returns 1.0
// when nothing blocks (lit) and 0.0 on any committed intersection
// (shadowed). The kTerminateOnFirstHit ray flag lets us early-out as
// soon as the first opaque triangle is hit - no need to find the
// closest one. Ray origin is biased by +1mm along the surface
// normal to dodge self-intersection acne.
float ray_visibility(vec3 origin, vec3 N, vec3 dir, float tmax) {
  // phase451-rt: bias dropped N*0.05+tmin=0.08 -> N*0.01+tmin=0.01.
  // The previous 8-13cm total bias pushed the ray ORIGIN through any
  // sub-10cm-thick geometry (Sponza at 0.01 scale has ~5cm wall
  // thickness). The ray then had to cross back through the wall to
  // reach an indoor light -> opaque hit -> vis=0 -> non-sun lights
  // had ZERO effect on Sponza receivers. 1cm bias still clears float-
  // precision self-hit on triangle-shared edges (sphere grid uses
  // 0.3m radius + 0.7m gap, 1cm is comfortably inside the clearance).
  rayQueryEXT rq;
  rayQueryInitializeEXT(
      rq, cd_tlas,
      gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
      0xFFu,
      origin + N * 0.01,
      0.01, dir, tmax);
  while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
  return (rayQueryGetIntersectionTypeEXT(rq, true) ==
          gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
}

// W8-BA RT scene reflection probe. Casts a closest-hit ray along the
// reflection direction. Returns 1.0 if the ray hit scene geometry
// within tmax (chrome would mirror that object) and 0.0 on miss
// (chrome shows the sky cube). We use OpaqueEXT but NOT
// TerminateOnFirstHit so the result is consistent regardless of
// BLAS walk order. tmin matches ray_visibility to avoid self-hit on
// merged meshes (CesiumMan, GLB samples).
float reflection_hit(vec3 origin, vec3 N, vec3 dir, float tmax) {
  // phase451-rt: matched ray_visibility bias drop for Sponza scale.
  rayQueryEXT rq;
  rayQueryInitializeEXT(
      rq, cd_tlas,
      gl_RayFlagsOpaqueEXT,
      0xFFu,
      origin + N * 0.01,
      0.01, dir, tmax);
  while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
  return (rayQueryGetIntersectionTypeEXT(rq, true) ==
          gl_RayQueryCommittedIntersectionNoneEXT) ? 0.0 : 1.0;
}

// W8-BC + phase465-perprim closest-hit reflection probe (Option B
// colored). Returns the (instance_id, geometry_index) pair on hit so
// the caller can index the per-(instance, geom) SSBO and pick the
// matching prim albedo.  out_inst / out_geom = -1 on miss (sky).
// The pseudo-normal used by sun-NoL shading at the hit point is just
// -dir (surface-outward for a convex hit) — convex spheres ~exact,
// cubes / Sponza walls approximate.
float reflection_hit_id(vec3 origin, vec3 N, vec3 dir, float tmax,
                        out int out_inst, out int out_geom) {
  // phase451-rt: matched ray_visibility bias drop for Sponza scale.
  rayQueryEXT rq;
  rayQueryInitializeEXT(
      rq, cd_tlas,
      gl_RayFlagsOpaqueEXT,
      0xFFu,
      origin + N * 0.01,
      0.01, dir, tmax);
  while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
  if (rayQueryGetIntersectionTypeEXT(rq, true) ==
      gl_RayQueryCommittedIntersectionNoneEXT) {
    out_inst = -1;
    out_geom = -1;
    return 0.0;
  }
  out_inst = rayQueryGetIntersectionInstanceIdEXT(rq, true);
  // phase465-perprim: also capture the geometry index inside the BLAS.
  // For single-geom BLAS (procedural prims / CesiumMan) this is always 0;
  // for Sponza's multi-geom BLAS this picks the prim sub-range that the
  // ray actually hit (vegetation / fabric / stone).
  out_geom = rayQueryGetIntersectionGeometryIndexEXT(rq, true);
  return 1.0;
}

// 3?-3 PCF shadow sampling. Returns 1.0 = fully lit, 0.0 = fully
// occluded. Vulkan clip space x,y ??? [-1,1], depth ??? [0,1]; texture
// uv has y down (matches Vulkan clip y after perspective divide).
// LTC polygon irradiance for area lights (#3). Lambert-only fit
// (identity inverse matrix - production wants a 64x64 LUT keyed
// by roughness/NoV). N is the surface normal at the shading
// point; corners are in world-space, relative to the shading
// point. Returns the form-factor of the polygon visible from N.
// Edge integral with atan2 - robust at parallel and anti-parallel
// configurations (the prior acos/sin form blew up near sin ~ 0 and
// produced a thin black stripe at the area-light's equatorial plane).
float cd_ltc_edge_integral(vec3 a, vec3 b) {
  float d = clamp(dot(a, b), -1.0, 1.0);
  vec3  c = cross(a, b);
  float l = length(c);
  float th = (l < 1e-6) ? 0.0 : atan(l, d);  // GLSL atan(y,x) = atan2
  return (l < 1e-6) ? 0.0 : (th / l) * c.z;
}
float cd_ltc_polygon_irradiance(vec3 N, vec3 c0, vec3 c1, vec3 c2, vec3 c3) {
  vec3 up = abs(N.y) > 0.95 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
  vec3 T  = normalize(cross(up, N));
  vec3 B  = cross(N, T);
  mat3 frame = transpose(mat3(T, B, N));
  vec3 p0 = normalize(frame * c0);
  vec3 p1 = normalize(frame * c1);
  vec3 p2 = normalize(frame * c2);
  vec3 p3 = normalize(frame * c3);
  float s = cd_ltc_edge_integral(p0, p1) +
            cd_ltc_edge_integral(p1, p2) +
            cd_ltc_edge_integral(p2, p3) +
            cd_ltc_edge_integral(p3, p0);
  // max-not-abs: negative values mean the polygon is back-facing.
  // Closes the 'siyah serit' artifact at the rect's equatorial plane.
  return max(s, 0.0) / 6.28318530;
}

float sample_shadow(vec4 sp, vec3 N, vec3 L) {
  // Perspective divide - ortho gives w=1 but keep for generality.
  vec3 p = sp.xyz / sp.w;
  // Outside the shadow ortho frustum ??' assume lit (sky / far away).
  if (p.x < -1.0 || p.x > 1.0 || p.y < -1.0 || p.y > 1.0 ||
      p.z < 0.0 || p.z > 1.0) return 1.0;
  // Vulkan: NDC y down ??' texture v down, same orientation, no flip.
  vec2 uv = p.xy * 0.5 + 0.5;
  // Slope-scaled depth bias - fights shadow acne on grazing-angle
  // fragments. Coefficient picked empirically.
  // phase451-csm: bias tightened slope 0.0015 -> 0.0003, floor 0.0003
  // -> 0.00005. Combined with the shrunken ortho frustum in
  // draw_shadow_map_pass (40m->20m extents, far 100m->60m), the world-
  // space bias drops from ~6cm to ~0.3cm — small enough that cube /
  // character / cylinder shadows cast onto Sponza floor are no longer
  // swallowed by self-bias. PBR sphere grid acne risk: spheres are
  // smooth, slope-scaled bias still spans the gradient, and the floor
  // is 0.00005 NDC = 3mm world (well below sphere radius).
  float bias = max(0.0003 * (1.0 - max(dot(N, L), 0.0)), 0.00005);
  float ref  = p.z - bias;
  vec2 ts = 1.0 / vec2(textureSize(cd_shadow_map, 0));
  float s = 0.0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      float d = texture(cd_shadow_map, uv + vec2(float(dx), float(dy)) * ts).r;
      s += (d < ref) ? 0.0 : 1.0;
    }
  return s / 9.0;
}

// W8-AQ Cook-Torrance helpers (used by tint.w == 3.0 PBR-sphere branch).
// Same equations as cd::material::StandardPbrMaterial so unified prim
// path renders metallic spheres physically identical to the dedicated
// PBR pipeline used by hello_pbr.
float D_GGX_pbr(float NoH, float a) {
  float a2 = a * a;
  float d  = (NoH * NoH) * (a2 - 1.0) + 1.0;
  return a2 / (3.14159265 * d * d + 1e-7);
}
float G_SchlickGGX_pbr(float NoV, float k) {
  return NoV / (NoV * (1.0 - k) + k + 1e-7);
}
float G_Smith_pbr(float NoV, float NoL, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return G_SchlickGGX_pbr(NoV, k) * G_SchlickGGX_pbr(NoL, k);
}
vec3 F_Schlick_pbr(float HoV, vec3 F0) {
  return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
}
vec3 F_Schlick_roughness_pbr(float NoV, vec3 F0, float roughness) {
  vec3 ceiling = max(vec3(1.0 - roughness), F0);
  return F0 + (ceiling - F0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
}

void main() {
  bool is_shadow_w   = (pc.tint.w < 0.5);
  bool is_floor_w    = (pc.tint.w > 1.5 && pc.tint.w < 2.5);
  bool is_pbr_w      = (pc.tint.w > 2.5 && pc.tint.w < 3.5);
  bool is_gltf_prim  = (pc.tint.w > 3.5 && pc.tint.w < 4.5);

  // Alpha-test / alpha-mask support (glTF alphaMode MASK / BLEND first-cut).
  if (pc.fx_params.w > 0.01 && pc.fx_params.y > 0.5) {
    float alpha_val = texture(cd_albedo_tex, v_uv).a;
    if (alpha_val < pc.fx_params.w) discard;
  }

  // surface_flag now encodes both AO eligibility AND a finer SSR/RT-blend bucket
  float surface_flag = (is_shadow_w || is_floor_w) ? 0.0
                     : is_pbr_w                    ? 0.85
                     : is_gltf_prim                ? 0.6
                                                   : 1.0;

  // Safe normal
  vec3 raw_N = v_world_normal;
  vec3 safe_N = (dot(raw_N, raw_N) > 1e-10) ? normalize(raw_N) : vec3(0.0, 1.0, 0.0);
  out_normal = vec4(safe_N, surface_flag);

  // Compute albedo up-front
  vec3 albedo = v_albedo;
  if (pc.fx_params.y > 0.5) {
    albedo = texture(cd_albedo_tex, v_uv).rgb * pc.tint.rgb;
  }

  // Compute metallic, roughness, and ao_factor up-front
  float metallic = 0.0;
  float roughness = 0.5;
  float ao_factor = 1.0;

  if (is_pbr_w) {
    metallic  = clamp(pc.fx_params4.x, 0.0, 1.0);
    roughness = clamp(pc.fx_params4.y, 0.04, 1.0);
  } else if (is_gltf_prim) {
    float mr_metal_factor = pc.fx_params4.x;
    float mr_rough_factor = pc.fx_params4.y;
    vec4 mr_sample = vec4(0.0, 1.0, 1.0, 1.0);
    if (pc.fx_params.y > 0.5) {
      mr_sample = texture(cd_mr_tex, v_uv);
    }
    metallic  = clamp(mr_sample.b * mr_metal_factor, 0.0, 1.0);
    roughness = clamp(mr_sample.g * mr_rough_factor, 0.04, 1.0);
    ao_factor = mr_sample.a;
  } else if (pc.fx_params.y > 0.5) {
    vec4 mr_sample = texture(cd_mr_tex, v_uv);
    metallic  = clamp(mr_sample.b, 0.0, 0.05);
    roughness = clamp(mr_sample.g, 0.04, 1.0);
    ao_factor = mr_sample.a;
  }

  // Write correct G-buffer MRT values
  out_albedo = vec4(clamp(albedo, vec3(0.0), vec3(1.0)), 1.0);
  out_mr     = vec2(metallic, roughness);

  // tint.w sentinel: < 0.5 = "shadow-projection draw"
  if (is_shadow_w) {
    out_color = vec4(pc.tint.rgb, 1.0);
    return;
  }

  // Floor grid overlay
  bool is_floor = (pc.tint.w > 1.5 && pc.tint.w < 2.5);
  float floor_fade = 1.0;
  if (is_floor) {
    float d_xz = length(v_world_pos.xz);
    floor_fade = clamp(1.0 - (d_xz - 60.0) / 140.0, 0.0, 1.0);
    vec2 p   = v_world_pos.xz;
    vec2 dp  = fwidth(p);
    vec2 mod1 = abs(fract(p) - 0.5);
    vec2 mod5 = abs(fract(p * 0.2) - 0.5);
    float lminor = min(mod1.x, mod1.y);
    float lmajor = min(mod5.x, mod5.y);
    float dminor = max(dp.x, dp.y) * 0.7;
    float dmajor = max(dp.x, dp.y) * 0.7 * 0.2;
    float a_minor = 1.0 - smoothstep(0.5 - dminor * 1.5, 0.5 - dminor * 0.5, lminor + 0.5 - dminor);
    float a_major = 1.0 - smoothstep(0.5 - dmajor * 2.0, 0.5 - dmajor * 0.5, lmajor + 0.5 - dmajor);
    float on_axis_x = step(abs(p.x), max(dp.x, 0.005));
    float on_axis_z = step(abs(p.y), max(dp.y, 0.005));
    vec3 minor_col = vec3(0.50, 0.52, 0.58);
    vec3 major_col = vec3(0.75, 0.78, 0.85);
    vec3 ax_x_col  = vec3(0.95, 0.30, 0.25);
    vec3 ax_z_col  = vec3(0.25, 0.45, 0.95);
    vec3 line_col  = minor_col;
    float line_a   = a_minor * 0.35;
    line_col = mix(line_col, major_col, smoothstep(0.0, 0.8, a_major));
    line_a   = max(line_a, a_major * 0.6);
    line_col = mix(line_col, ax_x_col, on_axis_z * 0.85);
    line_col = mix(line_col, ax_z_col, on_axis_x * 0.85);
    line_a   = max(line_a, max(on_axis_x, on_axis_z));
    line_a *= floor_fade;
    albedo  = mix(albedo, line_col, clamp(line_a, 0.0, 1.0));
    albedo  = mix(vec3(0.55, 0.60, 0.66) * 0.0, albedo, floor_fade);
  }

  // Normal mapping
  vec3 N = safe_N;
  bool sample_normal_map = (pc.fx_params.y > 0.5) &&
                           (!is_gltf_prim || pc.fx_params4.z > 0.001);
  if (sample_normal_map) {
    vec3 nm_sample = texture(cd_normal_tex, v_uv).xyz * 2.0 - 1.0;
    mat3 TBN = cotangent_frame(N, v_world_pos, v_uv);
    vec3 N_mapped = TBN * nm_sample;
    N = (dot(N_mapped, N_mapped) > 1e-10) ? normalize(N_mapped) : N;
  }

  vec3 V = normalize(pc.camera_pos.xyz - v_world_pos);
  float NoV = max(dot(N, V), 0.0);
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  float diff_scale = is_pbr_w ? 0.318309886 : 1.0;

  vec3 lit = vec3(0.0);

  // ---- Directional sun (Cook-Torrance + CSM shadow) ----
  {
    vec3 L = normalize(-pc.sun_dir.xyz);
    vec3 H = normalize(L + V);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float HoV = max(dot(H, V), 0.0);
    float D = D_GGX_pbr(NoH, roughness * roughness);
    float G = G_Smith_pbr(NoV, NoL, roughness);
    vec3  F = F_Schlick_pbr(HoV, F0);
    vec3 specular = (D * G) * F / (4.0 * NoV * NoL + 1e-7);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo * diff_scale;
    float shade = sample_shadow(v_shadow_pos, N, L);
    lit += (diffuse + specular) * NoL * pc.sun_color.rgb *
               (pc.sun_dir.w * shade);
  }

  // ---- Multi-light loop (point / spot / area) ----
  for (uint li = 0u; li < cd_lights.count; ++li) {
    vec3  lp_pos = cd_lights.slots[li].pos_range.xyz;
    float rng    = cd_lights.slots[li].pos_range.w;
    int   ltp    = int(cd_lights.slots[li].dir_type.w);
    if (rng <= 0.0) continue;

    if (ltp == 3 || ltp == 4) {
      // Area light (W4-B one-sided + tangent + winding + Karis rep-point specular)
      vec3 ln = normalize(cd_lights.slots[li].dir_type.xyz);
      vec3 to_pt_w = v_world_pos - lp_pos;
      if (dot(to_pt_w, ln) <= 0.0) continue;
      vec3 right = normalize(cd_lights.slots[li].tangent.xyz);
      vec3 up_v  = cross(ln, right);
      float w = cd_lights.slots[li].extras.y * 0.5;
      float h = cd_lights.slots[li].extras.z * 0.5;

      // LTC diffuse form factor
      vec3 c0 = lp_pos - right*w - up_v*h - v_world_pos;
      vec3 c1 = lp_pos + right*w - up_v*h - v_world_pos;
      vec3 c2 = lp_pos + right*w + up_v*h - v_world_pos;
      vec3 c3 = lp_pos - right*w + up_v*h - v_world_pos;
      float ff_diff = cd_ltc_polygon_irradiance(N, c0, c3, c2, c1);

      // Karis rep-point specular
      vec3 R_lp = reflect(-V, N);
      vec3 d_r  = lp_pos - v_world_pos;
      float denom = dot(R_lp, ln);
      vec3 plane_hit = (abs(denom) > 1e-4)
          ? (v_world_pos + R_lp * (dot(d_r, ln) / denom))
          : lp_pos;
      vec3 local = plane_hit - lp_pos;
      float u_clamp = clamp(dot(local, right), -w, w);
      float v_clamp = clamp(dot(local, up_v),  -h, h);
      vec3 closest  = lp_pos + right * u_clamp + up_v * v_clamp;
      vec3 to_L     = closest - v_world_pos;
      float dist2   = max(dot(to_L, to_L), 1e-4);
      vec3 Lrp      = to_L * inversesqrt(dist2);
      float area_p  = 4.0 * w * h;
      float cos_pan = max(dot(-Lrp, ln), 0.0);
      float omega   = clamp(area_p * cos_pan / dist2, 0.0, 6.28318530);
      float atten_rp = omega * 0.07957747;

      // Shadow ray
      vec3 to_c   = lp_pos - v_world_pos;
      float d_c   = max(length(to_c), 1e-4);
      vec3 Lc     = to_c / d_c;
      float area_tmax = min(d_c, rng);
      rayQueryEXT rq_a;
      rayQueryInitializeEXT(
          rq_a, cd_tlas,
          gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
          0xFFu, v_world_pos + safe_N * 0.01, 0.01, Lc, area_tmax);
      while (rayQueryProceedEXT(rq_a)) {}
      float vis_a = (rayQueryGetIntersectionTypeEXT(rq_a, true) ==
                    gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;

      vec3 acol = cd_lights.slots[li].color_int.xyz *
                  cd_lights.slots[li].color_int.w;

      // Cook-Torrance at Lrp
      vec3 Hrp = normalize(Lrp + V);
      float NoLrp = max(dot(N, Lrp), 0.0);
      float NoHrp = max(dot(N, Hrp), 0.0);
      float HoVrp = max(dot(Hrp, V), 0.0);
      float D_a = D_GGX_pbr(NoHrp, roughness * roughness);
      float G_a = G_Smith_pbr(NoV, NoLrp, roughness);
      vec3  F_a = F_Schlick_pbr(HoVrp, F0);
      vec3 spec_rect = (D_a * G_a) * F_a /
                       (4.0 * NoV * NoLrp + 1e-7) * NoLrp *
                       acol * atten_rp;

      vec3 F_diff = F_Schlick_roughness_pbr(NoV, F0, roughness);
      vec3 kD_a   = (vec3(1.0) - F_diff) * (1.0 - metallic);
      lit += vis_a * (kD_a * albedo * acol * ff_diff + spec_rect);
      continue;
    }

    // Point / spot path
    vec3 to_p = lp_pos - v_world_pos;
    float d   = length(to_p);
    if (d < 1e-4) continue;
    vec3 Lp   = to_p / d;
    float NoL = max(dot(N, Lp), 0.0);
    if (NoL <= 0.0) continue;
    float atten = distance_atten(d, rng);
    float cone  = 1.0;
    if (ltp == 2) {
      vec3  axis    = normalize(cd_lights.slots[li].dir_type.xyz);
      float cos_b   = dot(-Lp, axis);
      float cos_out = cd_lights.slots[li].extras.x;
      float cos_in_cpu = cd_lights.slots[li].extras.w;
      float cos_in     = clamp(max(cos_in_cpu, cos_out + 0.01),
                               cos_out + 0.01, 0.9999);
      cone          = smoothstep(cos_out, cos_in, cos_b);
      if (cone <= 0.0) continue;
    }
    float ray_tmax = min(d, rng);
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq, cd_tlas,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xFFu, v_world_pos + safe_N * 0.01, 0.01, Lp, ray_tmax);
    while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
    float vis = (rayQueryGetIntersectionTypeEXT(rq, true) ==
                 gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;

    vec3 H = normalize(Lp + V);
    float NoH = max(dot(N, H), 0.0);
    float HoV = max(dot(H, V), 0.0);
    float D = D_GGX_pbr(NoH, roughness * roughness);
    float G = G_Smith_pbr(NoV, NoL, roughness);
    vec3  F = F_Schlick_pbr(HoV, F0);
    vec3 specular = (D * G) * F / (4.0 * NoV * NoL + 1e-7);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo * diff_scale;
    vec3 lcol = cd_lights.slots[li].color_int.xyz *
                cd_lights.slots[li].color_int.w;
    lit += (diffuse + specular) * NoL * lcol *
               (atten * cone * vis);
  }

  // Ambient hemisphere lighting
  float any_light = clamp(pc.sun_dir.w + float(cd_lights.count) * 0.5, 0.0, 1.0);
  float up_t   = N.y * 0.5 + 0.5;
  vec3  sky_c  = vec3(0.55, 0.65, 0.85);
  vec3  gnd_c  = vec3(0.18, 0.16, 0.14);
  vec3  hemi   = mix(gnd_c, sky_c, up_t) * pc.sun_color.w;
  vec3  ambient = is_pbr_w ? vec3(0.0) : albedo * (hemi + vec3(0.020) * any_light) * ao_factor;

  // IBL split-sum + Option B colored RT reflections
  vec3  Ri   = reflect(-V, N);
  float lod_p   = roughness * kIblMaxMipLod;
  vec3  spec_e  = textureLod(cd_ibl_spec, Ri, lod_p).rgb;
  vec3  diff_e  = texture(cd_ibl_diff, N).rgb;
  vec2  brdf_v  = texture(cd_brdf_lut, vec2(clamp(NoV, 0.0, 1.0),
                                            clamp(roughness, 0.0, 1.0))).rg;
  vec3  F_ibl   = F_Schlick_roughness_pbr(NoV, F0, roughness);
  vec3  ibl_kD  = (vec3(1.0) - F_ibl) * (1.0 - metallic);
  float Ess_p   = brdf_v.x + brdf_v.y;
  float Ems_p   = 1.0 - Ess_p;
  vec3  Favg_p  = F0 + (1.0 - F0) * (1.0 / 21.0);
  vec3  Fms_p   = (Favg_p * Ess_p) / (vec3(1.0) - Favg_p * Ems_p);
  vec3  ibl_spec_p = spec_e * (F0 * brdf_v.x + vec3(brdf_v.y) + Fms_p * Ems_p);

  float ibl_gate_factor = is_pbr_w ? 1.0 : 0.6;
  float ibl_gate = clamp(pc.sun_dir.w * ibl_gate_factor, 0.0, 1.0);

  int   hit_inst   = -1;
  int   hit_geom   = -1;
  float scene_hit  = reflection_hit_id(v_world_pos, safe_N, Ri, 80.0,
                                       hit_inst, hit_geom);
  vec3  brdf_term  = F0 * brdf_v.x + vec3(brdf_v.y) + Fms_p * Ems_p;
  vec3  ibl_spec_blended = ibl_spec_p;
  int hit_slot = -1;
  if (hit_inst >= 0)
  {
    int g = (hit_geom < 0) ? 0 : hit_geom;
    if (g >= kMaxGeomsPerInst) g = kMaxGeomsPerInst - 1;
    hit_slot = hit_inst * kMaxGeomsPerInst + g;
    if (hit_slot >= kMaxInstMatSlots) hit_slot = -1;
  }
  if (scene_hit > 0.5 && hit_slot >= 0) {
    vec3 hit_alb   = cd_instance_mats.data[hit_slot].albedo.rgb;
    // phase830-rt-chrome-sponza-visible-mirror (auto-synced from prim.frag.glsl):
    // For mirror reflections the BRDF integral at the reflection direction
    // is unity — multiplying by brdf_term was attenuating the chrome
    // reflection and tinting it with F0 a second time, fading the curtain
    // reflections into the white-wall background. Drop the brdf_term for
    // the RT branch; keep it on the IBL fallback.
    vec3 refl_color = hit_alb * pc.sun_color.rgb * 8.0;
    float rough_blend = clamp(roughness * roughness, 0.0, 1.0);
    float metal_gate  = clamp(metallic, 0.0, 1.0);
    float blend_t     = mix(1.0, rough_blend, metal_gate);
    ibl_spec_blended = mix(refl_color, ibl_spec_p, blend_t);
  }

  vec3 ibl_contrib = (ibl_kD * diff_e * albedo + ibl_spec_blended) * ao_factor * ibl_gate;
  float ibl_scale = is_pbr_w ? 1.0 : 0.55;
  ambient += ibl_contrib * ibl_scale;

  // GTAO crease darkening
  float ao_strength = clamp(pc.fx_params.z, 0.0, 1.0);
  if (ao_strength > 0.001) {
    vec3 dn_dx = dFdx(v_world_normal);
    vec3 dn_dy = dFdy(v_world_normal);
    float curv = clamp(length(dn_dx) + length(dn_dy), 0.0, 1.0);
    float ao   = 1.0 - ao_strength * curv * 0.85;
    ambient   *= ao;
  }

  // Advanced BRDF lobes (sheen, clearcoat, sss)
  float fx_cc    = (is_gltf_prim || is_pbr_w) ? 0.0 : clamp(pc.fx_params4.x, 0.0, 1.0);
  float fx_sheen = (is_gltf_prim || is_pbr_w) ? 0.0 : clamp(pc.fx_params4.y, 0.0, 1.0);
  float fx_sss   = (is_gltf_prim || is_pbr_w) ? 0.0 : clamp(pc.fx_params4.z, 0.0, 1.0);
  if (fx_cc > 0.001 || fx_sheen > 0.001 || fx_sss > 0.001) {
    vec3 V_b = normalize(pc.camera_pos.xyz - v_world_pos);
    float NoV_b = max(dot(N, V_b), 0.0);
    if (fx_cc > 0.001) {
      vec3 R_b = reflect(-V_b, N);
      vec3 spec_cc = textureLod(cd_ibl_spec, R_b, 0.5 * kIblMaxMipLod).rgb;
      float fres_cc = 0.04 + 0.96 * pow(1.0 - NoV_b, 5.0);
      lit += spec_cc * fres_cc * fx_cc * 0.6;
    }
    if (fx_sheen > 0.001) {
      float rim = pow(1.0 - NoV_b, 4.0);
      vec3 sheen_col = vec3(0.95, 0.92, 0.88);
      lit += sheen_col * rim * fx_sheen * 1.2;
    }
    if (fx_sss > 0.001) {
      vec3 Ld_b = normalize(-pc.sun_dir.xyz);
      float backlit = clamp(dot(-N, Ld_b), 0.0, 1.0);
      vec3 sss_col = vec3(0.95, 0.55, 0.45);
      lit += sss_col * pow(backlit, 1.5) * fx_sss * pc.sun_dir.w * 0.8;
    }
  }

  vec3 c = lit + ambient;

  // Camera FX
  vec3 cam_dir = normalize(v_world_pos - pc.camera_pos.xyz);
  float radial = length(cam_dir.xy);
  float vignette = clamp(pc.fx_params2.y, 0.0, 1.0);
  if (vignette > 0.001) {
    float vmask = smoothstep(0.0, 1.4, radial);
    c *= mix(1.0, 1.0 - vmask, vignette);
  }
  float ca = clamp(pc.fx_params2.z, 0.0, 1.0);
  if (ca > 0.001) {
    c.r *= 1.0 + ca * 0.08;
    c.b *= 1.0 - ca * 0.08;
  }
  float grain = clamp(pc.fx_params2.w, 0.0, 1.0);
  if (grain > 0.001) {
    float g = fract(sin(dot(v_world_pos.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (g - 0.5) * grain * 0.05;
  }

  // Fog
  float dist = length(v_world_pos - pc.camera_pos.xyz);
  float fog_density = clamp(pc.fx_params3.x, 0.0, 1.0);
  if (fog_density > 0.001) {
    float h_falloff = exp(-max(v_world_pos.y, 0.0) * 0.10);
    float f = 1.0 - exp(-dist * fog_density * 0.030 * h_falloff);
    vec3  fog_col = vec3(0.62, 0.66, 0.74);
    c = mix(c, fog_col, clamp(f, 0.0, 0.95));
  }
  float aerial = clamp(pc.fx_params3.y, 0.0, 1.0);
  if (aerial > 0.001) {
    float t = clamp(dist / 80.0, 0.0, 1.0);
    vec3 aerial_tint = vec3(0.55, 0.62, 0.78);
    c = mix(c, aerial_tint, t * aerial * 0.35);
  }
  float shafts = clamp(pc.fx_params3.w, 0.0, 1.0);
  if (shafts > 0.001 && pc.sun_dir.w > 0.001) {
    vec3 cam_to_p = normalize(v_world_pos - pc.camera_pos.xyz);
    vec3 sun_L    = normalize(-pc.sun_dir.xyz);
    float align  = max(dot(cam_to_p, sun_L), 0.0);
    float shaft  = pow(align, 32.0) * shafts;
    vec3 shaft_col = pc.sun_color.rgb * pc.sun_dir.w;
    c += shaft_col * shaft * 0.6;
  }

  c = clamp(c, vec3(0.0), vec3(100.0));

  // Debug views
  int view_mode = int(pc.fx_params4.w + 0.5);
  if (view_mode == 1) {
    out_color = vec4(albedo, 1.0);
    return;
  } else if (view_mode == 2) {
    out_color = vec4(normalize(v_world_normal) * 0.5 + 0.5, 1.0);
    return;
  } else if (view_mode == 3) {
    out_color = vec4(0.0, roughness, metallic, 1.0);
    return;
  } else if (view_mode == 4) {
    out_color = vec4(vec3(ao_factor), 1.0);
    return;
  } else if (view_mode == 5) {
    out_color = vec4(N * 0.5 + 0.5, 1.0);
    return;
  } else if (view_mode == 6) {
    out_color = vec4(v_uv, 0.0, 1.0);
    return;
  }

  out_color = vec4(c, 1.0);
}
)glsl";

}  // namespace cd::hello_engine
