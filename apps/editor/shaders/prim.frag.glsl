#version 460
// Faz 1.7 - inline RT shadows via ray queries inside the raster FS.
// VK_KHR_ray_query is required at the device level; the material
// creation gates on device.features().ray_query so this extension
// guard never fires on unsupported hardware. GLSL 460 is required
// because ray-query intrinsics were introduced for that profile.
#extension GL_EXT_ray_query : require
// phase847-W8-BE-fix: Move the descriptor_indexing extension directive
// up here next to ray_query so the GLSL preprocessor sees it before
// the binding declarations that depend on it (compiler-strict path).
#extension GL_EXT_nonuniform_qualifier : require
// phase1135 (SL-D wave 1): shader-library module includes, resolved by
// cd::gluon::ModuleResolver (MaterialDesc::include_resolver).
#extension GL_GOOGLE_include_directive : enable
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
// kMaxGeomsPerInst = 32 (matches StudioRayTracingTypes.hpp).  Closest-hit rays
// fetch the pair via rayQueryGetIntersectionInstanceIdEXT +
// rayQueryGetIntersectionGeometryIndexEXT so multi-geometry BLAS hits
// (Sponza: vegetation / fabric / stone) sample THEIR OWN prim albedo
// rather than the single instance-level tint that ALL Sponza hits
// shared before phase465.  Single-geometry instances (procedural prims,
// CesiumMan, the editor floor) still fill geom slot 0 + replicate to
// 1..31 on the host so geom_index >= 1 reads back the same albedo.
// phase840-W8-BE-rt-bindless-texture-sampling: InstanceMat extended
// with `albedo_tex_slot` + `index_offset` fields. Slot value
// `kBindlessAlbedoSlotNone` (= 0xFFFFFFFFu) signals "no per-prim
// texture, fall back to the albedo (avg-colour) field" (W8-BD path).
//
// The new bindings 11 (sampler2D array), 12 (vertex SSBO), 13 (index
// SSBO) are added in phase842 alongside the host-side wiring so the
// validation layer never sees a shader that references a binding the
// pipeline layout does not yet expose.
struct InstanceMat {
  vec4 albedo;
  vec4 emissive;
  uint albedo_tex_slot;
  uint index_offset;
  // phase866-2-bounce-sphere-normal: is_sphere = 1 enables the
  // analytical-normal 2-bounce path; sphere_center_radius carries
  // the world-space centre + radius for that path.
  uint is_sphere;
  // phase886-non-sponza-bindless-prep: 0 = Sponza (sponza_vb/ib
  // bindings 11/12), 1 = future CesiumMan binding. Shader uses
  // it as a hint; texture-UV interp still uses Sponza VB/IB
  // until a future phase wires the multi-mesh selection.
  uint mesh_id;
  vec4 sphere_center_radius;
};
layout(set = 0, binding = 10) readonly buffer InstanceMats {
  InstanceMat data[];
} cd_instance_mats;
// phase798-rt-chrome-sponza-geom-cap: 32 -> 128 to cover Sponza's 103
// primitives (every curtain / column past slot 31 was clamping to 31).
// Slot count = kMaxInstances(64) * kMaxGeomsPerInst(128) = 8192.
const int  kMaxGeomsPerInst        = 128;
const int  kMaxInstMatSlots        = 8192;
const float kIblMaxMipLod          = 5.0;
// phase840 sentinel: matches StudioRayTracingTypes::kBindlessAlbedoSlotNone.
const uint kBindlessAlbedoSlotNone = 0xFFFFFFFFu;

// phase842c-W8-BE-rt-bindless-texture-sampling: new bindings.
//   11 — Sponza vertex buffer as readonly storage (positions + UVs).
//        Mirrors cd::asset::PrimitiveVertex (3×float pos + 3×float normal +
//        2×float uv + 3×float color = 44 B, std430 padded to 48 B per
//        vec3 alignment rules — we use explicit vec4 stride to make
//        the layout deterministic).
//   12 — Sponza index buffer (uint32) as readonly storage. Looks up
//        the three vertex indices of the ray-hit triangle.
//   13 — bindless sampler2D array. Per-prim albedo textures live in
//        slots indexed by InstanceMat.albedo_tex_slot. The
//        nonuniform_qualifier extension is required because the slot
//        index varies per-pixel across the chrome surface.
// (#extension GL_EXT_nonuniform_qualifier moved to the file head in
// phase847 for compiler strictness.)
// Mirrors cd::asset::PrimitiveVertex (3×float pos + 3×float normal +
// 2×float uv + 3×float color = 11 floats = 44 B packed with 4-byte alignment).
layout(set = 0, binding = 11, std430) readonly buffer SponzaVB {
  float data[];
} cd_sponza_vb;
layout(set = 0, binding = 12, std430) readonly buffer SponzaIB {
  uint idx[];
} cd_sponza_ib;
// phase888-non-sponza-bindless-shader: bindings 14/15 carry the
// CesiumMan VB/IB. mesh_id == 1 in the per-prim SSBO entry
// selects this pair instead of sponza_vb/ib for the chrome
// reflection UV interpolation.
layout(set = 0, binding = 14, std430) readonly buffer CesiumVB {
  float data[];
} cd_cesium_vb;
layout(set = 0, binding = 15, std430) readonly buffer CesiumIB {
  uint idx[];
} cd_cesium_ib;

vec2 sponza_vert_uv(uint vi) {
  uint b = vi * 11u;
  return vec2(cd_sponza_vb.data[b + 6u], cd_sponza_vb.data[b + 7u]);
}
vec3 sponza_vert_normal(uint vi) {
  uint b = vi * 11u;
  return vec3(cd_sponza_vb.data[b + 3u], cd_sponza_vb.data[b + 4u], cd_sponza_vb.data[b + 5u]);
}
vec2 cesium_vert_uv(uint vi) {
  uint b = vi * 11u;
  return vec2(cd_cesium_vb.data[b + 6u], cd_cesium_vb.data[b + 7u]);
}
vec3 cesium_vert_normal(uint vi) {
  uint b = vi * 11u;
  return vec3(cd_cesium_vb.data[b + 3u], cd_cesium_vb.data[b + 5u], -cd_cesium_vb.data[b + 4u]);
}
uint cesium_read_index(uint k) {
  uint word = cd_cesium_ib.idx[k >> 1u];
  return (word >> ((k & 1u) * 16u)) & 0xFFFFu;
}
// phase864-bindless-dedicated-set: bindless sampler2D array moved
// onto its own descriptor set at index 1. The shared per-prim set 0
// no longer carries binding 13 — phase 851 + 860 proved NVIDIA
// crashes the dynamic-index branch when bindless lives in the same
// set as classic bindings. Set 1 has a single binding (binding=0)
// and is allocated independently from the global descriptor pool.
layout(set = 1, binding = 0) uniform sampler2D cd_bindless_albedo[];

// phase1138 (SL-D wave 2): moved VERBATIM to the shader library.
#include <cd/gluon/cotangent_frame.glsl>
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec3 v_albedo;
layout(location = 3) in vec4 v_shadow_pos;
layout(location = 4) in vec2 v_uv;
// Optional baseColor texture (gap #1/#13). fx_params.y = 1.0
// flags the draw as 'sample texture'; 0.0 = use vertex-coloured
// albedo path. Single texture slot for chromodynamic_studio - production
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

// phase1138 (SL-D wave 2): moved VERBATIM to the shader library.
#include <cd/gluon/light_atten.glsl>

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
// phase842c-W8-BE-rt-bindless-texture-sampling: extended to also
// return the hit primitive (triangle) index AND the barycentrics so
// the caller can recover the per-vertex UV at the hit point by
// indexing the Sponza VB/IB SSBOs.
float reflection_hit_id(vec3 origin, vec3 N, vec3 dir, float tmax,
                        out int  out_inst, out int  out_geom,
                        out int  out_prim, out vec2 out_bary) {
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
    out_prim = -1;
    out_bary = vec2(0.0);
    return 0.0;
  }
  out_inst = rayQueryGetIntersectionInstanceIdEXT(rq, true);
  // phase465-perprim: also capture the geometry index inside the BLAS.
  // For single-geom BLAS (procedural prims / CesiumMan) this is always 0;
  // for Sponza's multi-geom BLAS this picks the prim sub-range that the
  // ray actually hit (vegetation / fabric / stone).
  out_geom = rayQueryGetIntersectionGeometryIndexEXT(rq, true);
  out_prim = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
  out_bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
  return 1.0;
}

// Shared UV + RGBA bindless texture resolver with 5-tap footprint filter.
// Returns alpha = 0.0 when the ray hits a transparent cutout texel (a < 0.5)
// so reflection_hit_id_t and reflection_hit_color can step past alpha-masked
// foliage/chain quads (e.g., Sponza Prim 53 ivy, Prim 4 chains, Prim 20 flowers).
vec4 sample_hit_albedo_rgba(int slot, int prim, vec2 bary, vec3 hit_pos,
                            float hit_t, vec3 fallback) {
  uint tex_slot   = cd_instance_mats.data[slot].albedo_tex_slot;
  uint idx_offset = cd_instance_mats.data[slot].index_offset;
  if (tex_slot == kBindlessAlbedoSlotNone || tex_slot >= 256u)
    return vec4(fallback, 1.0);
  uint mesh_id = cd_instance_mats.data[slot].mesh_id;
  vec2 uv = vec2(0.0);
  bool uv_ok = false;
  if (mesh_id == 2u) {
    vec4 sphc = cd_instance_mats.data[slot].sphere_center_radius;
    vec3 d_sp = hit_pos - sphc.xyz;
    vec3 obj_radial = (dot(d_sp, d_sp) > 1e-8) ? normalize(d_sp) : vec3(0.0, 1.0, 0.0);
    const float kInv2Pi = 0.15915494;  // 1 / (2 * pi)
    const float kInvPi  = 0.31830989;  // 1 / pi
    float u_sp = (abs(obj_radial.x) > 1e-6 || abs(obj_radial.z) > 1e-6)
                 ? (atan(obj_radial.z, obj_radial.x) * kInv2Pi + 0.5)
                 : 0.5;
    uv = vec2(u_sp, asin(clamp(obj_radial.y, -1.0, 1.0)) * kInvPi + 0.5);
    uv_ok = true;
  } else if (prim >= 0) {
    uint i0, i1, i2;
    vec2 uv0, uv1, uv2;
    if (mesh_id == 1u) {
      i0 = cesium_read_index(idx_offset + uint(prim) * 3u + 0u);
      i1 = cesium_read_index(idx_offset + uint(prim) * 3u + 1u);
      i2 = cesium_read_index(idx_offset + uint(prim) * 3u + 2u);
      uv0 = cesium_vert_uv(i0);
      uv1 = cesium_vert_uv(i1);
      uv2 = cesium_vert_uv(i2);
    } else {
      i0 = cd_sponza_ib.idx[idx_offset + uint(prim) * 3u + 0u];
      i1 = cd_sponza_ib.idx[idx_offset + uint(prim) * 3u + 1u];
      i2 = cd_sponza_ib.idx[idx_offset + uint(prim) * 3u + 2u];
      uv0 = sponza_vert_uv(i0);
      uv1 = sponza_vert_uv(i1);
      uv2 = sponza_vert_uv(i2);
    }
    float w0 = 1.0 - bary.x - bary.y;
    uv = uv0 * w0 + uv1 * bary.x + uv2 * bary.y;
    uv_ok = true;
  }
  if (!uv_ok) return vec4(fallback, 1.0);
  vec4 c0 = texture(cd_bindless_albedo[nonuniformEXT(tex_slot)], uv);
  if (c0.a < 0.5) return vec4(fallback, 0.0);
  if (mesh_id == 1u) return vec4(c0.rgb, 1.0);

  // 5-tap rotated-grid prefilter to suppress Mip-0 minification aliasing
  // (salt-and-pepper mortar speckles & floor crack Moire on curved reflections).
  float r_uv = clamp(hit_t * 0.0015, 0.0015, 0.012);
  vec2 o1 = vec2( r_uv,  r_uv * 0.5);
  vec2 o2 = vec2(-r_uv * 0.5,  r_uv);
  vec3 accum = c0.rgb * 2.0;
  float w_sum = 2.0;
  vec4 c1 = texture(cd_bindless_albedo[nonuniformEXT(tex_slot)], uv + o1);
  if (c1.a >= 0.5) { accum += c1.rgb; w_sum += 1.0; }
  vec4 c2 = texture(cd_bindless_albedo[nonuniformEXT(tex_slot)], uv - o1);
  if (c2.a >= 0.5) { accum += c2.rgb; w_sum += 1.0; }
  vec4 c3 = texture(cd_bindless_albedo[nonuniformEXT(tex_slot)], uv + o2);
  if (c3.a >= 0.5) { accum += c3.rgb; w_sum += 1.0; }
  vec4 c4 = texture(cd_bindless_albedo[nonuniformEXT(tex_slot)], uv - o2);
  if (c4.a >= 0.5) { accum += c4.rgb; w_sum += 1.0; }
  vec3 filtered = accum / w_sum;

  // Prevent sub-texel black mortar/crack pits from creating dark speckles,
  // and blend smoothly toward the prim's representative albedo with distance.
  filtered = max(filtered, fallback * 0.55);
  float dist_blend = clamp(hit_t * 0.025, 0.15, 0.45);
  return vec4(mix(filtered, fallback, dist_blend), 1.0);
}

vec3 sample_hit_albedo(int slot, int prim, vec2 bary, vec3 hit_pos,
                       vec3 fallback) {
  return sample_hit_albedo_rgba(slot, prim, bary, hit_pos, 3.0, fallback).rgb;
}

// phase866-2-bounce-sphere-normal: t-returning variant of
// reflection_hit_id with alpha-cutout step-through so transparent
// foliage/chain quads (alphaMode = MASK) do not paint black cutouts
// onto chrome reflections.
float reflection_hit_id_t(vec3 origin, vec3 N, vec3 dir, float tmax,
                          out int  out_inst, out int  out_geom,
                          out int  out_prim, out vec2 out_bary,
                          out float out_t) {
  vec3 ray_orig = origin + N * 0.01;
  float tmin = 0.01;
  for (int step = 0; step < 5; ++step) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq, cd_tlas,
        gl_RayFlagsOpaqueEXT,
        0xFFu,
        ray_orig,
        tmin, dir, tmax);
    while (rayQueryProceedEXT(rq)) {}
    if (rayQueryGetIntersectionTypeEXT(rq, true) ==
        gl_RayQueryCommittedIntersectionNoneEXT) {
      break;
    }
    int inst = rayQueryGetIntersectionInstanceIdEXT(rq, true);
    int geom = rayQueryGetIntersectionGeometryIndexEXT(rq, true);
    int prim = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
    vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
    float t   = rayQueryGetIntersectionTEXT(rq, true);
    int g = (geom < 0) ? 0 : min(geom, kMaxGeomsPerInst - 1);
    int slot = inst * kMaxGeomsPerInst + g;
    if (slot >= 0 && slot < kMaxInstMatSlots) {
      uint tex_slot = cd_instance_mats.data[slot].albedo_tex_slot;
      if (tex_slot != kBindlessAlbedoSlotNone && tex_slot < 256u) {
        vec4 s_rgba = sample_hit_albedo_rgba(
            slot, prim, bary, ray_orig + dir * t, t,
            cd_instance_mats.data[slot].albedo.rgb);
        if (s_rgba.a < 0.5) {
          tmin = t + 0.008;
          if (tmin < tmax) continue;
        }
      }
    }
    out_inst = inst;
    out_geom = geom;
    out_prim = prim;
    out_bary = bary;
    out_t    = t;
    return 1.0;
  }
  out_inst = -1; out_geom = -1; out_prim = -1;
  out_bary = vec2(0.0); out_t = 0.0;
  return 0.0;
}

float reflection_hit_color(vec3 origin, vec3 dir, float tmax,
                           out vec3 out_color) {
  float tmin = 0.01;
  for (int step = 0; step < 4; ++step) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq, cd_tlas,
        gl_RayFlagsOpaqueEXT,
        0xFFu,
        origin,
        tmin, dir, tmax);
    while (rayQueryProceedEXT(rq)) {}
    if (rayQueryGetIntersectionTypeEXT(rq, true) ==
        gl_RayQueryCommittedIntersectionNoneEXT) {
      out_color = vec3(0.0);
      return 0.0;
    }
    int inst2 = rayQueryGetIntersectionInstanceIdEXT(rq, true);
    int geom2 = rayQueryGetIntersectionGeometryIndexEXT(rq, true);
    if (geom2 < 0) geom2 = 0;
    if (geom2 >= kMaxGeomsPerInst) geom2 = kMaxGeomsPerInst - 1;
    int slot2 = inst2 * kMaxGeomsPerInst + geom2;
    if (slot2 < 0 || slot2 >= kMaxInstMatSlots) {
      out_color = vec3(0.0);
      return 0.0;
    }
    int   prim2 = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
    vec2  bary2 = rayQueryGetIntersectionBarycentricsEXT(rq, true);
    float t2    = rayQueryGetIntersectionTEXT(rq, true);
    vec4 s_rgba = sample_hit_albedo_rgba(
        slot2, prim2, bary2, origin + dir * t2, t2,
        cd_instance_mats.data[slot2].albedo.rgb);
    if (s_rgba.a < 0.5) {
      tmin = t2 + 0.008;
      if (tmin < tmax) continue;
    }
    out_color = s_rgba.rgb;
    return 1.0;
  }
  out_color = vec3(0.0);
  return 0.0;
}

// 3?-3 PCF shadow sampling. Returns 1.0 = fully lit, 0.0 = fully
// occluded. Vulkan clip space x,y ??? [-1,1], depth ??? [0,1]; texture
// uv has y down (matches Vulkan clip y after perspective divide).
// phase1138 (SL-D wave 2): LTC area-light pair moved VERBATIM to the
// shader library (W8-AJ twin lives in cd::brdf::ltc — unification is
// the user-signed visual phase).
#include <cd/gluon/ltc_polygon.glsl>

// phase (SL-D consumer migration): the 3×3 PCF + slope-scaled bias
// (phase451-csm tuned coefficients) moved VERBATIM to the shader
// library (cd_pcf_shadow_3x3 + cd_shadow_slope_bias). The global
// cd_shadow_map sampler is passed as a function argument so the module
// stays self-contained. Pixel-neutral: chrome_probe golden pins it.
#include <cd/gluon/shadow_filtering.glsl>

// phase1135 (SL-D wave 1): the W8-AQ Cook-Torrance helper block moved
// VERBATIM to the shader library (exact-text migration — preprocessed
// token stream unchanged, chrome_probe golden pins the move).
// CANONICAL BRDF UNIFICATION (user-signed visual phase): swapped the
// transitional brdf_w8aq twin (Schlick-GGX k=(r+1)²/8 geometry term)
// for the canonical height-correlated Smith brdf module. The call
// sites below drop their explicit /(4·NoV·NoL) divisor because
// cd_v_smith_ggx_correlated folds that denominator in (D·V·F form).
#include <cd/gluon/brdf.glsl>

// Phase 18 SOTA BRDF: Belcour & Barla (SIGGRAPH 2017) Thin-Film Iridescence
vec3 cd_belcour_barla_iridescence(vec3 f_schlick, float cos_theta_d, float film_nm) {
  if (film_nm <= 0.5) return f_schlick;
  float eta = 1.5;
  float sin2_sq = max(0.0, 1.0 - cos_theta_d * cos_theta_d) / (eta * eta);
  float cos2 = sqrt(max(0.0, 1.0 - sin2_sq));
  float opd = 2.0 * eta * film_nm * cos2;
  vec3 lambdas = vec3(650.0, 530.0, 450.0);
  vec3 phi = 6.2831853 * (opd / lambdas) + 3.14159265;
  float depol = exp(-0.0000015 * opd * opd);
  vec3 cos_phi = cos(phi) * depol;
  float r12 = 0.04 + 0.96 * pow(clamp(1.0 - cos2, 0.0, 1.0), 5.0);
  vec3 r23 = clamp(f_schlick * 0.75, vec3(0.04), vec3(0.92));
  vec3 r12_r23 = vec3(r12) * r23;
  vec3 two_sqrt_r_cos = 2.0 * sqrt(r12_r23) * cos_phi;
  vec3 f_airy = (vec3(r12) + r23 + two_sqrt_r_cos)
              / max(vec3(1.0) + r12_r23 + two_sqrt_r_cos, vec3(1e-4));
  return mix(f_schlick, clamp(f_airy, vec3(0.0), vec3(1.0)), clamp(film_nm / 20.0, 0.0, 1.0));
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

  // surface_flag encodes the SSR bucket for the composite pass:
  //   > 0.95 → full SSR
  //   [0.5, 0.95] → half-strength SSR additive enhancement
  //   ≤ 0.5 → no SSR (sky / shadow / floor / non-metallic surfaces)
  // Computed as a baseline here; overridden below once metallic is known
  // (non-metallic glTF prims must NOT get SSR — that was the curtain sheen bug).
  float surface_flag = (is_shadow_w || is_floor_w) ? 0.0
                     : is_pbr_w                    ? 0.85
                     : is_gltf_prim                ? 0.6
                                                   : 1.0;

  // Safe normal
  vec3 raw_N = v_world_normal;
  vec3 safe_N = (dot(raw_N, raw_N) > 1e-10) ? normalize(raw_N) : vec3(0.0, 1.0, 0.0);
  out_normal = vec4(safe_N, surface_flag);

  // Compute albedo up-front (supports bound GPU texture slot 1 + procedural asset textures 2..6)
  vec3 albedo = v_albedo;
  if (pc.fx_params.y > 0.5) {
    int tex_mode = int(floor(pc.fx_params.y + 0.25));
    vec2 uv_p = v_uv;
    if (length(uv_p) < 1e-4) {
      uv_p = v_world_pos.xz * 0.5 + vec2(v_world_pos.y * 0.35);
    }
    if (tex_mode == 1) {
      albedo = texture(cd_albedo_tex, uv_p).rgb * pc.tint.rgb;
    } else if (tex_mode == 2) {
      // Slot 2: High-contrast UV Checkerboard + sub-grid
      vec2 cuv = uv_p * 8.0;
      float chk = mod(floor(cuv.x) + floor(cuv.y), 2.0);
      vec2 sub = abs(fract(cuv * 2.0) - 0.5);
      float sline = smoothstep(0.46, 0.49, max(sub.x, sub.y));
      vec3 c_dark  = vec3(0.16, 0.18, 0.22);
      vec3 c_light = vec3(0.86, 0.88, 0.92);
      vec3 pat = mix(c_dark, c_light, chk);
      pat = mix(pat, vec3(0.32, 0.58, 0.95), sline * 0.35);
      albedo = pat * pc.tint.rgb;
    } else if (tex_mode == 3) {
      // Slot 3: Architectural Running-Bond Brick & Mortar
      vec2 buv = uv_p * vec2(6.0, 12.0);
      float row_idx = floor(buv.y);
      buv.x += 0.5 * mod(row_idx, 2.0);
      vec2 bcell = fract(buv);
      vec2 bid   = floor(buv);
      float mortar = (bcell.x < 0.06 || bcell.y < 0.10) ? 1.0 : 0.0;
      float b_hash = fract(sin(dot(bid, vec2(12.9898, 78.233))) * 43758.5453);
      vec3 brick_col = mix(vec3(0.62, 0.22, 0.15), vec3(0.78, 0.32, 0.20), b_hash);
      vec3 mortar_col = vec3(0.76, 0.74, 0.70);
      albedo = mix(brick_col, mortar_col, mortar) * pc.tint.rgb;
    } else if (tex_mode == 4) {
      // Slot 4: 2x2 Twill Carbon Fiber Weave
      vec2 wuv = uv_p * 24.0;
      vec2 wid = floor(wuv);
      vec2 wfr = fract(wuv);
      float diag = mod(wid.x + wid.y, 2.0);
      float ridge = (diag > 0.5) ? (0.55 + 0.45 * sin(wfr.x * 3.14159))
                                 : (0.35 + 0.35 * sin(wfr.y * 3.14159));
      vec3 c_weave = mix(vec3(0.08, 0.09, 0.11), vec3(0.26, 0.28, 0.32), ridge);
      albedo = c_weave * pc.tint.rgb;
    } else if (tex_mode == 5) {
      // Slot 5: Carrara Veined Marble
      vec2 muv = uv_p * 4.5;
      float n1 = sin(muv.x * 3.2 + sin(muv.y * 4.1 + sin(muv.x * 2.7)) * 1.6);
      float n2 = cos(muv.y * 5.3 - sin(muv.x * 3.8) * 1.4);
      float vein = pow(clamp(1.0 - abs(n1 * 0.65 + n2 * 0.35), 0.0, 1.0), 4.0);
      vec3 m_base = vec3(0.92, 0.91, 0.89);
      vec3 m_vein = vec3(0.32, 0.36, 0.42);
      albedo = mix(m_base, m_vein, vein * 0.75) * pc.tint.rgb;
    } else if (tex_mode >= 10 && tex_mode < 256) {
      vec3 s_col = texture(cd_bindless_albedo[nonuniformEXT(uint(tex_mode))], uv_p).rgb;
      albedo = s_col * pc.tint.rgb;
    } else {
      // Slot 6: Warm Oak Wood Planks
      vec2 wuv = uv_p * vec2(4.0, 1.5);
      float plank_id = floor(wuv.x);
      float ring = sin((wuv.y * 14.0 + plank_id * 2.3) + sin(wuv.x * 18.0) * 0.6);
      float grain = 0.5 + 0.5 * ring;
      float seam = (fract(wuv.x) < 0.03 || fract(wuv.y + 0.5 * mod(plank_id, 2.0)) < 0.02) ? 1.0 : 0.0;
      vec3 wood_col = mix(vec3(0.46, 0.28, 0.14), vec3(0.72, 0.48, 0.26), grain);
      albedo = mix(wood_col, vec3(0.18, 0.11, 0.06), seam) * pc.tint.rgb;
    }
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
    if (pc.fx_params.y > 0.5 && pc.fx_params.y < 1.5) {
      mr_sample = texture(cd_mr_tex, v_uv);
    }
    metallic  = clamp(mr_sample.b * mr_metal_factor, 0.0, 1.0);
    roughness = clamp(mr_sample.g * mr_rough_factor, 0.04, 1.0);
    ao_factor = mr_sample.a;
  } else if (pc.fx_params.y > 0.5 && pc.fx_params.y < 1.5) {
    vec4 mr_sample = texture(cd_mr_tex, v_uv);
    metallic  = clamp(mr_sample.b, 0.0, 0.05);
    roughness = clamp(mr_sample.g, 0.04, 1.0);
    ao_factor = mr_sample.a;
  }

  // Write correct G-buffer MRT values
  out_albedo = vec4(clamp(albedo, vec3(0.0), vec3(1.0)), 1.0);
  out_mr     = vec2(metallic, roughness);

  // Override surface_flag for non-metallic surfaces and hardware-RT PBR spheres:
  // disable coarse 8-step SSR so dielectrics don't get a metallic sheen and
  // curved RT mirror probes (is_pbr_w) don't get concentric screen-space depth
  // march contour bands smeared over their exact ray-traced reflections.
  if (metallic < 0.25 || is_pbr_w)
    surface_flag = 0.0;
  out_normal = vec4(safe_N, surface_flag);

  // tint.w sentinel: < 0.5 = "shadow-projection draw"
  if (is_shadow_w) {
    out_color = vec4(pc.tint.rgb, 1.0);
    return;
  }

  // Floor grid overlay
  bool is_floor = (pc.tint.w > 1.5 && pc.tint.w < 2.5);
  float floor_fade = 1.0;
  if (is_floor) {
    bool is_transparent_grid = (pc.tint.w > 2.15 && pc.tint.w < 2.45);
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
    if (is_transparent_grid) {
      vec3 dark_base = vec3(0.038, 0.044, 0.056);
      vec3 dark_line = mix(vec3(0.16, 0.19, 0.24), vec3(0.32, 0.36, 0.44), smoothstep(0.0, 0.8, a_major));
      dark_line = mix(dark_line, ax_x_col * 0.82, on_axis_z * 0.85);
      dark_line = mix(dark_line, ax_z_col * 0.82, on_axis_x * 0.85);
      vec3 grid_rgb = mix(dark_base, dark_line, clamp(line_a, 0.0, 1.0));
      float grid_alpha = clamp(0.42 + line_a * 0.44, 0.0, 0.86) * floor_fade;
      out_normal = vec4(0.0);
      out_albedo = vec4(grid_rgb, 1.0);
      out_mr     = vec2(0.0, 1.0);
      out_color  = vec4(grid_rgb, grid_alpha);
      return;
    }
    albedo  = mix(albedo, line_col, clamp(line_a, 0.0, 1.0));
    albedo  = mix(vec3(0.55, 0.60, 0.66) * 0.0, albedo, floor_fade);
  }

  // Normal mapping (only for slot 1 bound normal map on non-PBR-sphere prims)
  vec3 N = safe_N;
  bool sample_normal_map = (pc.fx_params.y > 0.5 && pc.fx_params.y < 1.5) &&
                           !is_gltf_prim && !is_pbr_w;
  if (sample_normal_map) {
    vec3 nm_sample = texture(cd_normal_tex, v_uv).xyz * 2.0 - 1.0;
    mat3 TBN = cotangent_frame(N, v_world_pos, v_uv);
    vec3 N_mapped = TBN * nm_sample;
    N = (dot(N_mapped, N_mapped) > 1e-10) ? normalize(N_mapped) : N;
  }

  vec3 V = normalize(pc.camera_pos.xyz - v_world_pos);
  float NoV = max(dot(N, V), 0.0);
  vec3 F0 = mix(vec3(0.04), albedo, metallic);

  float pbr_iridescence_nm = (is_pbr_w && pc.fx_params4.z > 0.001) ? floor(pc.fx_params4.z) : 0.0;
  float pbr_ms_weight      = (is_pbr_w && pc.fx_params4.z > 0.001) ? clamp(fract(pc.fx_params4.z) / 0.99, 0.0, 1.0) : 0.0;
  vec2  brdf_v             = texture(cd_brdf_lut, vec2(clamp(NoV, 0.0, 1.0),
                                                       clamp(roughness, 0.0, 1.0))).rg;
  float Ess_p              = brdf_v.x + brdf_v.y;
  float Ess_safe           = max(Ess_p, 0.08);
  vec3  turquin_ms_comp    = (pbr_ms_weight > 0.001)
                           ? min(vec3(1.0) + F0 * (pbr_ms_weight * (1.0 - Ess_safe) / Ess_safe), vec3(1.45))
                           : vec3(1.0);

  // Energy conservation is handled by kD = (1-F)*(1-metallic), NOT by
  // dividing diffuse by PI. PBR spheres use 1/PI for physical correctness
  // in the dedicated Cook-Torrance path; glTF prims and default entities
  // keep 1.0 so their diffuse contribution is not artificially dimmed
  // (which would expose specular artifacts as metallic sheen).
  float diff_scale = is_pbr_w ? 0.318309886 : 1.0;

  // Non-metallic surfaces (curtains, stone, vegetation, procedural entities)
  // get ZERO Cook-Torrance specular. Rough dielectrics (roughness >= 0.3,
  // F0 = 0.04) have negligible visible specular in reality, and even a small
  // percentage of GGX specular becomes visible under high-intensity lights
  // (3000-6000 lm), creating a metallic sheen that doesn't belong on fabric.
  // Only the PBR demo spheres (is_pbr_w) retain full Cook-Torrance specular.
  bool diel_no_spec = !is_pbr_w && metallic < 0.25;

  vec3 lit = vec3(0.0);

  // ---- Directional sun (Cook-Torrance + CSM shadow) ----
  {
    vec3 L = normalize(-pc.sun_dir.xyz);
    vec3 H = normalize(L + V);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float HoV = max(dot(H, V), 0.0);
    float D = cd_d_ggx(NoH, roughness);
    float Vis = cd_v_smith_ggx_correlated(NoV, NoL, roughness);
    vec3  F = cd_belcour_barla_iridescence(cd_f_schlick(F0, HoV), HoV, pbr_iridescence_nm);
    vec3 specular = (D * Vis) * F * turquin_ms_comp;
    if (diel_no_spec) specular = vec3(0.0);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo * diff_scale;
    float shade = cd_pcf_shadow_3x3(cd_shadow_map, v_shadow_pos, N, L);
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
      if (d_c >= rng || (ff_diff <= 1e-5 && atten_rp <= 1e-5)) continue;
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
      float D_a = cd_d_ggx(NoHrp, roughness);
      float Vis_a = cd_v_smith_ggx_correlated(NoV, NoLrp, roughness);
      vec3  F_a = cd_belcour_barla_iridescence(cd_f_schlick(F0, HoVrp), HoVrp, pbr_iridescence_nm);
      vec3 spec_rect = (D_a * Vis_a) * F_a * turquin_ms_comp * NoLrp *
                       acol * atten_rp;
      if (diel_no_spec) spec_rect = vec3(0.0);

      vec3 F_diff = cd_f_schlick_roughness(F0, NoV, roughness);
      vec3 kD_a   = (vec3(1.0) - F_diff) * (1.0 - metallic);
      lit += vis_a * (kD_a * albedo * acol * ff_diff + spec_rect);
      continue;
    }

    // Point / spot path
    vec3 to_p = lp_pos - v_world_pos;
    float d   = length(to_p);
    if (d < 1e-4 || d >= rng) continue;
    vec3 Lp   = to_p / d;
    float NoL = max(dot(N, Lp), 0.0);
    if (NoL <= 0.0) continue;
    float atten = distance_atten(d, rng);
    if (atten <= 1e-5) continue;
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
    float D = cd_d_ggx(NoH, roughness);
    float Vis = cd_v_smith_ggx_correlated(NoV, NoL, roughness);
    vec3  F = cd_belcour_barla_iridescence(cd_f_schlick(F0, HoV), HoV, pbr_iridescence_nm);
    vec3 specular = (D * Vis) * F * turquin_ms_comp;
    if (diel_no_spec) specular = vec3(0.0);
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
  vec3  F_ibl   = cd_belcour_barla_iridescence(cd_f_schlick_roughness(F0, NoV, roughness), NoV, pbr_iridescence_nm);
  vec3  ibl_kD  = (vec3(1.0) - F_ibl) * (1.0 - metallic);
  float Ems_p   = 1.0 - Ess_p;
  vec3  F0_ibl  = (pbr_iridescence_nm > 0.5) ? F_ibl : F0;
  vec3  Favg_p  = F0_ibl + (1.0 - F0_ibl) * (1.0 / 21.0);
  vec3  Fms_p   = (Favg_p * Ess_p) / (vec3(1.0) - Favg_p * Ems_p);
  vec3  raw_ibl_spec = spec_e * (F0_ibl * brdf_v.x + vec3(brdf_v.y) + Fms_p * Ems_p);
  // Zero IBL specular for non-metallic non-PBR surfaces to match the
  // zeroed direct-light specular above. Only PBR spheres and metallic
  // surfaces get environment reflections.
  float ibl_spec_metallic_gate = diel_no_spec ? 0.0
                                : mix(0.12, 1.0, clamp(metallic, 0.0, 1.0));
  vec3  ibl_spec_p = raw_ibl_spec * ibl_spec_metallic_gate;
  // phase794 IBL interior tint reverted in phase795 — the brightness fix
  // on the RT branch below (refl_color * 5x sun_color) now drives the
  // visible result for chrome, no IBL pre-tint needed.

  float ibl_gate_factor = is_pbr_w ? 1.0 : 0.6;
  float ibl_gate = clamp(pc.sun_dir.w * ibl_gate_factor, 0.0, 1.0);

  int   hit_inst   = -1;
  int   hit_geom   = -1;
  int   hit_prim   = -1;
  vec2  hit_bary   = vec2(0.0);
  // Skip RT reflection probe entirely for non-metallic surfaces to avoid
  // the performance cost AND the visual artifact of dielectric surfaces
  // mirroring scene objects like chrome.
  float scene_hit  = 0.0;
  float hit_t      = 0.0;
  if (metallic > 0.1)
    scene_hit = reflection_hit_id_t(v_world_pos, safe_N, Ri, 80.0,
                                    hit_inst, hit_geom, hit_prim, hit_bary,
                                    hit_t);
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
    vec3 hit_alb = cd_instance_mats.data[hit_slot].albedo.rgb;
    uint tex_slot   = cd_instance_mats.data[hit_slot].albedo_tex_slot;
    uint idx_offset = cd_instance_mats.data[hit_slot].index_offset;
    vec3 hit_pos1   = (v_world_pos + safe_N * 0.01) + Ri * hit_t;
    if (tex_slot != kBindlessAlbedoSlotNone && tex_slot < 256u) {
      hit_alb = sample_hit_albedo_rgba(
          hit_slot, hit_prim, hit_bary, hit_pos1, hit_t, hit_alb).rgb;
    }
    uint  hit_is_sphere = cd_instance_mats.data[hit_slot].is_sphere;
    vec4  hit_sc        = cd_instance_mats.data[hit_slot].sphere_center_radius;
    if (hit_is_sphere == 1u) {
        vec3 d_sp1    = hit_pos1 - hit_sc.xyz;
        vec3 N1       = (dot(d_sp1, d_sp1) > 1e-8) ? normalize(d_sp1) : -Ri;
        vec3 Ri2      = reflect(Ri, N1);
        vec3 bounce_color = vec3(0.0);
        float bounce_hit  = reflection_hit_color(
                              hit_pos1 + N1 * 0.02,
                              Ri2, 60.0,
                              bounce_color);
        if (bounce_hit > 0.5) {
            hit_alb = mix(hit_alb, hit_alb * (bounce_color + 0.2), 0.55);
        } else {
            vec3 miss_ibl = textureLod(cd_ibl_spec, Ri2, 0.0).rgb;
            hit_alb = mix(hit_alb, hit_alb * miss_ibl * 1.2, 0.35);
        }
    } else if ((cd_instance_mats.data[hit_slot].mesh_id == 0u ||
                cd_instance_mats.data[hit_slot].mesh_id == 1u) &&
               tex_slot != kBindlessAlbedoSlotNone && hit_prim >= 0) {
        // Interpolate the true vertex normal from SponzaVB / CesiumVB at the barycentric
        // hit point to shade 3D relief/columns/arches/avatars naturally without
        // turning diffuse surfaces into secondary mirrors.
        vec3 n0, n1, n2;
        if (cd_instance_mats.data[hit_slot].mesh_id == 1u) {
            uint i0 = cesium_read_index(idx_offset + uint(hit_prim) * 3u + 0u);
            uint i1 = cesium_read_index(idx_offset + uint(hit_prim) * 3u + 1u);
            uint i2 = cesium_read_index(idx_offset + uint(hit_prim) * 3u + 2u);
            n0 = cesium_vert_normal(i0);
            n1 = cesium_vert_normal(i1);
            n2 = cesium_vert_normal(i2);
        } else {
            uint i0 = cd_sponza_ib.idx[idx_offset + uint(hit_prim) * 3u + 0u];
            uint i1 = cd_sponza_ib.idx[idx_offset + uint(hit_prim) * 3u + 1u];
            uint i2 = cd_sponza_ib.idx[idx_offset + uint(hit_prim) * 3u + 2u];
            n0 = sponza_vert_normal(i0);
            n1 = sponza_vert_normal(i1);
            n2 = sponza_vert_normal(i2);
        }
        float w0 = 1.0 - hit_bary.x - hit_bary.y;
        vec3 n_raw = n0 * w0 + n1 * hit_bary.x + n2 * hit_bary.y;
        vec3 N_sponza = (dot(n_raw, n_raw) > 1e-8) ? normalize(n_raw) : -Ri;
        if (dot(N_sponza, Ri) > 0.0) N_sponza = -N_sponza;
        vec3 L_sun = normalize(-pc.sun_dir.xyz);
        float n_hemi = N_sponza.y * 0.5 + 0.5;
        float n_sun  = max(dot(N_sponza, L_sun), 0.0);
        float n_shade = 0.85 + 0.18 * n_hemi + 0.15 * n_sun;
        hit_alb *= n_shade;
    } else {
        vec3 second_bounce_ibl = textureLod(cd_ibl_spec, Ri,
                                            roughness * kIblMaxMipLod).rgb;
        hit_alb = mix(hit_alb, hit_alb + second_bounce_ibl * 0.35, 0.25);
    }
    // phase795-rt-chrome-sponza-brightness:
    // OLD: refl_color = hit_alb * (0.3 + 0.7 * NoL_hit) * sun_color * 3.0
    //
    // The OLD math used a pseudo_N = -Ri "fake normal" dotted with the
    // sun direction. For typical chrome reflection rays this dot ends up
    // close to zero (or even negative — clamped to zero), so refl_color
    // ended up at the 0.3 floor: ~0.9 * hit_alb * sun_color. After the
    // brdf_term * blend the surviving signal was so dim that chrome
    // mostly fell back to the IBL term, painting the spheres in the
    // outdoor sky colour even when the RT ray DID land on Sponza
    // geometry. The user-reported bug ("spheres look like they're in a
    // different universe than Sponza") was driven by this energy loss
    // — not by an actual RT miss.
    //
    // NEW: emit hit_alb at a flat HDR-bright level (5x sun_color). No
    // pseudo-normal cosine term. The intent is "this surface returns
    // its full diffuse albedo at the IBL energy range" — appropriate
    // for the indirect GI-ish role this branch is playing, and matches
    // how the cubemap encodes interior brightness. Saturated per-prim
    // colours (red curtain, green curtain, sandstone wall) now actually
    // make it into the mirror image.
    // phase835-rt-chrome-sponza-tame-whitewash:
    // After phase833 fixed the BLAS cap (Sponza was silently truncated
    // to 32/103 prims), ray hits land on the FULL 103 prims. With the
    // 8x sun-strength boost set when most hits were missing, the white
    // sandstone walls now blow out post-ACES into a flat white wash —
    // visible curtain colours got drowned out and prim-edge detail
    // softened. 4x compromises between:
    //   * dimmer overall (white sandstone walls render as bright but
    //     not clipping the ACES knee),
    //   * coloured curtain prims (R 0.85/0.12/0.08, G 0.15/0.6/0.1)
    //     keep their saturation post-tonemap,
    //   * chrome still reads as bright-mirror, not low-contrast plastic.
    //
    // The per-prim "average colour" return remains a known limit —
    // prim-internal detail (curtain damask pattern, leaf texture)
    // requires ray-side texture sampling (queued strand L-rt-tex,
    // see ADR W8-BD rejected alternative #1).
    vec3 refl_tint = is_pbr_w ? (F_ibl * 1.55) : vec3(4.0);
    vec3 refl_color = hit_alb * pc.sun_color.rgb * refl_tint;
    float rough_blend = clamp(is_pbr_w ? (roughness * 2.2) : (roughness * roughness), 0.0, 1.0);
    float metal_gate  = clamp(metallic, 0.0, 1.0);
    float blend_t     = mix(1.0, rough_blend, metal_gate);
    // Mirror-direction RT branch: hit colour direct (no brdf_term).
    // IBL fallback: with brdf_term (the existing prefilter path).
    ibl_spec_blended = mix(refl_color, ibl_spec_p, blend_t);
  }

  // phase831-rt-chrome-sponza-bypass-ambient-gates:
  // A perfect mirror reflection is direct specular — it should NOT be
  // attenuated by `ao_factor` (the mirror surface itself is not
  // shadowed by ambient occlusion at the reflection direction) or
  // `ibl_gate` (which fades the IBL term when the sun is low, but the
  // RT scene reflection is independent of sun strength). Split the
  // ambient contribution so the diffuse + IBL-spec path keeps its
  // existing ao*gate attenuation while the smooth-metallic RT-hit
  // mirror path is added at full energy.
  bool is_rt_mirror = (scene_hit > 0.5) && (metallic > 0.5) && (roughness < 0.3);
  vec3 ibl_spec_for_ambient = is_rt_mirror ? vec3(0.0) : ibl_spec_blended;
  vec3 ibl_contrib =
      (ibl_kD * diff_e * albedo + ibl_spec_for_ambient) * ao_factor * ibl_gate;
  if (is_rt_mirror)
      ibl_contrib += ibl_spec_blended;
  float ibl_scale = is_pbr_w ? 1.0 : 0.55;
  ambient += ibl_contrib * ibl_scale;

  // GTAO crease darkening
  float ao_strength = clamp(pc.fx_params.z, 0.0, 1.0);
  if (ao_strength > 0.001) {
    vec3 dn_dx = dFdx(v_world_normal);
    vec3 dn_dy = dFdy(v_world_normal);
    float curv = clamp(length(dn_dx) + length(dn_dy), 0.0, 1.0);
    // Reduce crease-AO intensity to avoid dark halos around objects.
    float ao   = 1.0 - ao_strength * curv * 0.35;
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
