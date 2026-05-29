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
// W8-BC per-frame TLAS-instance material table. Index matches the
// position used by push_inst() on the host, so
// rayQueryGetIntersectionInstanceIdEXT(rq,true) yields the right slot.
// albedo.rgb = entity tint (Floor entity gets neutral 0.5 grey);
// emissive.rgb reserved for self-lit reflections (0 today). Used by
// the W8-BC colored reflection blend in the kPrimFS PBR branch.
struct InstanceMat { vec4 albedo; vec4 emissive; };
layout(set = 0, binding = 10) readonly buffer InstanceMats {
  InstanceMat data[];
} cd_instance_mats;
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
  float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
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
  // Bias the origin away from the surface AND start the ray walk at
  // tmin > 0 so the source instance's own triangles (very close to
  // the shading point on merged meshes like CesiumMan) don't get
  // false-hit as occluders. tmin 0.08 + N*0.05 bias clears
  // typical compound-mesh self-intersection without losing real
  // shadows from neighbouring objects.
  rayQueryEXT rq;
  rayQueryInitializeEXT(
      rq, cd_tlas,
      gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
      0xFFu,
      origin + N * 0.05,
      0.08, dir, tmax);
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
  rayQueryEXT rq;
  rayQueryInitializeEXT(
      rq, cd_tlas,
      gl_RayFlagsOpaqueEXT,
      0xFFu,
      origin + N * 0.05,
      0.08, dir, tmax);
  while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
  return (rayQueryGetIntersectionTypeEXT(rq, true) ==
          gl_RayQueryCommittedIntersectionNoneEXT) ? 0.0 : 1.0;
}

// W8-BC closest-hit reflection probe (Option B colored). Like
// reflection_hit() but ALSO returns the TLAS instance index on hit
// via out_inst so we can sample cd_instance_mats.data[inst].albedo
// for a colored mirror. out_inst is -1 on miss (sky). The pseudo-
// normal used by the sun-NoL shading at the hit point is just -dir
// (the surface-outward direction for a convex hit), which is good
// enough for v1 (spheres ~exact, cubes/CesiumMan approximate).
float reflection_hit_id(vec3 origin, vec3 N, vec3 dir, float tmax,
                        out int out_inst) {
  rayQueryEXT rq;
  rayQueryInitializeEXT(
      rq, cd_tlas,
      gl_RayFlagsOpaqueEXT,
      0xFFu,
      origin + N * 0.05,
      0.08, dir, tmax);
  while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
  if (rayQueryGetIntersectionTypeEXT(rq, true) ==
      gl_RayQueryCommittedIntersectionNoneEXT) {
    out_inst = -1;
    return 0.0;
  }
  out_inst = rayQueryGetIntersectionInstanceIdEXT(rq, true);
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
  float bias = max(0.0025 * (1.0 - max(dot(N, L), 0.0)), 0.0005);
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
  // R3 G-Buffer: world-space surface normal MRT-write. Done up-front
  // so the every early-return path (shadow draw / view-mode debug /
  // floor / lit path) emits a valid normal for downstream SSR + AO.
  // Sky pixels are produced by a different shader (AnalyticalSkyFS)
  // which writes w=0 so the composite can distinguish "sky" vs
  // "surface" at sample time.
  //
  // B10: floor (tint.w == 2.0) + planar-shadow draws (tint.w < 0.5)
  // also flag w=0 so composite skips SSR/AO. The grid floor is a
  // virtual editor reference — it shouldn't kick reflections or AO
  // crease from below at sample-time post-fx.
  // W8-AQ: tint.w sentinels — <0.5 shadow-projection (no surface),
  // ==1.0 normal entity, ==2.0 floor (no surface), ==3.0 PBR sphere
  // (real surface, goes through the W8-AQ Cook-Torrance branch).
  bool is_shadow_w   = (pc.tint.w < 0.5);
  bool is_floor_w    = (pc.tint.w > 1.5 && pc.tint.w < 2.5);
  float surface_flag = (is_shadow_w || is_floor_w) ? 0.0 : 1.0;
  out_normal = vec4(normalize(v_world_normal), surface_flag);

  // R3 G-Buffer phase 219 - albedo + MR. Sample the same textures
  // the lit path uses so deferred / post-fx consumers see exactly
  // what the forward path drew. Defaults: 0 metallic, 0.5 roughness.
  vec4 mr_pre = (pc.fx_params.y > 0.5) ? texture(cd_mr_tex, v_uv) : vec4(0, 0.5, 0.04, 1);
  out_albedo = vec4(clamp(v_albedo * pc.tint.rgb, vec3(0.0), vec3(1.0)), 1.0);
  out_mr = vec2(clamp(mr_pre.b, 0.0, 1.0), clamp(mr_pre.g, 0.04, 1.0));

  // ==========================================================================
  // W8-AQ PBR-sphere unified path (tint.w == 3.0).
  // Routes the 5x5 metallic/roughness sphere grid through THIS shader so
  // the spheres share lighting state with every other primitive in the
  // scene: sun_dir, sun_color, cd_lights UBO, IBL bindings, RT shadows.
  //   pc.tint.rgb       = albedo
  //   pc.fx_params4.x   = metallic  (was clearcoat — sphere grid never
  //                                   applies clearcoat lobe, safe overload)
  //   pc.fx_params4.y   = roughness (was sheen — same rationale)
  // Cook-Torrance + GGX + Smith + Schlick; multi-light loop matches the
  // textured-entity path below. Output is linear HDR; composite owns
  // tonemap + saturation + gamma like everything else.
  if (pc.tint.w > 2.5 && pc.tint.w < 3.5) {
    vec3  pbr_albedo  = pc.tint.rgb;
    float pbr_metal   = clamp(pc.fx_params4.x, 0.0, 1.0);
    float pbr_rough   = clamp(pc.fx_params4.y, 0.04, 1.0);
    out_albedo = vec4(clamp(pbr_albedo, vec3(0.0), vec3(1.0)), 1.0);
    out_mr     = vec2(pbr_metal, pbr_rough);

    vec3  Npbr = normalize(v_world_normal);
    vec3  Vpbr = normalize(pc.camera_pos.xyz - v_world_pos);
    float NoVpbr = max(dot(Npbr, Vpbr), 0.0);
    vec3  F0pbr  = mix(vec3(0.04), pbr_albedo, pbr_metal);

    vec3 lit_pbr = vec3(0.0);

    // ---- Directional sun (Cook-Torrance + CSM shadow) ----
    {
      vec3 L = normalize(-pc.sun_dir.xyz);
      vec3 H = normalize(L + Vpbr);
      float NoL = max(dot(Npbr, L), 0.0);
      float NoH = max(dot(Npbr, H), 0.0);
      float HoV = max(dot(H, Vpbr), 0.0);
      float D = D_GGX_pbr(NoH, pbr_rough * pbr_rough);
      float G = G_Smith_pbr(NoVpbr, NoL, pbr_rough);
      vec3  F = F_Schlick_pbr(HoV, F0pbr);
      vec3 specular = (D * G) * F / (4.0 * NoVpbr * NoL + 1e-7);
      vec3 kD = (vec3(1.0) - F) * (1.0 - pbr_metal);
      vec3 diffuse = kD * pbr_albedo / 3.14159265;
      float shade = sample_shadow(v_shadow_pos, Npbr, L);
      lit_pbr += (diffuse + specular) * NoL * pc.sun_color.rgb *
                 (pc.sun_dir.w * shade);
    }

    // ---- Multi-light loop (point / spot / area) ----
    for (uint li = 0u; li < cd_lights.count; ++li) {
      vec3  lp_pos = cd_lights.slots[li].pos_range.xyz;
      float rng    = cd_lights.slots[li].pos_range.w;
      int   ltp    = int(cd_lights.slots[li].dir_type.w);
      if (rng <= 0.0) continue;

      if (ltp == 3) {
        // Area rect (W4-B one-sided + W8-N tangent + W8-AJ winding
        // + W8-AN Karis rep-point specular).
        vec3 ln = normalize(cd_lights.slots[li].dir_type.xyz);
        vec3 to_pt_w = v_world_pos - lp_pos;
        if (dot(to_pt_w, ln) <= 0.0) continue;
        vec3 right_a = normalize(cd_lights.slots[li].tangent.xyz);
        vec3 up_a    = cross(ln, right_a);
        float hw = cd_lights.slots[li].extras.y * 0.5;
        float hh = cd_lights.slots[li].extras.z * 0.5;
        // LTC diffuse form factor.
        vec3 c0 = lp_pos - right_a*hw - up_a*hh - v_world_pos;
        vec3 c1 = lp_pos + right_a*hw - up_a*hh - v_world_pos;
        vec3 c2 = lp_pos + right_a*hw + up_a*hh - v_world_pos;
        vec3 c3 = lp_pos - right_a*hw + up_a*hh - v_world_pos;
        float ff_diff = cd_ltc_polygon_irradiance(Npbr, c0, c3, c2, c1);
        // Karis 2013 rep-point: reflect view, intersect rect plane,
        // clamp to (T,B) bounds, use as punctual L with solid-angle atten.
        vec3 Rpbr = reflect(-Vpbr, Npbr);
        vec3 d_r  = lp_pos - v_world_pos;
        float denom = dot(Rpbr, ln);
        vec3 plane_hit = (abs(denom) > 1e-4)
            ? (v_world_pos + Rpbr * (dot(d_r, ln) / denom))
            : lp_pos;
        vec3 local = plane_hit - lp_pos;
        float u_clamp = clamp(dot(local, right_a), -hw, hw);
        float v_clamp = clamp(dot(local, up_a),    -hh, hh);
        vec3 closest  = lp_pos + right_a * u_clamp + up_a * v_clamp;
        vec3 to_L     = closest - v_world_pos;
        float dist2   = max(dot(to_L, to_L), 1e-4);
        vec3 Lrp      = to_L * inversesqrt(dist2);
        float area_p  = 4.0 * hw * hh;
        float cos_pan = max(dot(-Lrp, ln), 0.0);
        float omega   = clamp(area_p * cos_pan / dist2, 0.0, 6.28318530);
        float atten_rp = omega * 0.07957747;  // omega / (4*PI)
        // RT-shadow ray toward rect centre.
        vec3 to_c = lp_pos - v_world_pos;
        float d_c = max(length(to_c), 1e-4);
        vec3 Lc   = to_c / d_c;
        float vis_a = ray_visibility(v_world_pos, Npbr, Lc, min(d_c, rng));
        vec3  acol = cd_lights.slots[li].color_int.xyz *
                     cd_lights.slots[li].color_int.w;
        // Cook-Torrance at Lrp.
        vec3 Hrp = normalize(Lrp + Vpbr);
        float NoLrp = max(dot(Npbr, Lrp), 0.0);
        float NoHrp = max(dot(Npbr, Hrp), 0.0);
        float HoVrp = max(dot(Hrp, Vpbr), 0.0);
        float D_a = D_GGX_pbr(NoHrp, pbr_rough * pbr_rough);
        float G_a = G_Smith_pbr(NoVpbr, NoLrp, pbr_rough);
        vec3  F_a = F_Schlick_pbr(HoVrp, F0pbr);
        vec3 spec_rect = (D_a * G_a) * F_a /
                         (4.0 * NoVpbr * NoLrp + 1e-7) * NoLrp *
                         acol * atten_rp;
        vec3 F_diff = F_Schlick_roughness_pbr(NoVpbr, F0pbr, pbr_rough);
        vec3 kD_a   = (vec3(1.0) - F_diff) * (1.0 - pbr_metal);
        lit_pbr += vis_a * (kD_a * pbr_albedo * acol * ff_diff + spec_rect);
        continue;
      }

      // Point / spot.
      vec3  to_p = lp_pos - v_world_pos;
      float d    = length(to_p);
      if (d < 1e-4) continue;
      vec3  L    = to_p / d;
      float NoL  = max(dot(Npbr, L), 0.0);
      if (NoL <= 0.0) continue;
      float atten = distance_atten(d, rng);
      float cone  = 1.0;
      if (ltp == 2) {
        vec3  axis      = normalize(cd_lights.slots[li].dir_type.xyz);
        float cos_b     = dot(-L, axis);
        float cos_out   = cd_lights.slots[li].extras.x;
        float cos_in_cpu= cd_lights.slots[li].extras.w;
        float cos_in    = clamp(max(cos_in_cpu, cos_out + 0.01),
                                cos_out + 0.01, 0.9999);
        cone = smoothstep(cos_out, cos_in, cos_b);
        if (cone <= 0.0) continue;
      }
      float vis = ray_visibility(v_world_pos, Npbr, L, min(d, rng));
      vec3 H = normalize(L + Vpbr);
      float NoH = max(dot(Npbr, H), 0.0);
      float HoV = max(dot(H, Vpbr), 0.0);
      float D = D_GGX_pbr(NoH, pbr_rough * pbr_rough);
      float G = G_Smith_pbr(NoVpbr, NoL, pbr_rough);
      vec3  F = F_Schlick_pbr(HoV, F0pbr);
      vec3 specular = (D * G) * F / (4.0 * NoVpbr * NoL + 1e-7);
      vec3 kD = (vec3(1.0) - F) * (1.0 - pbr_metal);
      vec3 diffuse = kD * pbr_albedo / 3.14159265;
      vec3 lcol = cd_lights.slots[li].color_int.xyz *
                  cd_lights.slots[li].color_int.w;
      lit_pbr += (diffuse + specular) * NoL * lcol *
                 (atten * cone * vis);
    }

    // ---- IBL split-sum (Karis 2013) ----
    // W8-AY: env-spec ALSO gated on sun. User reported "gunes olmadigi
    // yerde gokyuzu yansitiyolar" — chrome reflecting sky-without-sun
    // breaks the lighting consistency. Gate both env-spec and env-
    // diffuse on the same sun + any-non-sun ramp so dark scenes
    // produce dark spheres.
    vec3  Ripbr   = reflect(-Vpbr, Npbr);
    float lod_p   = pbr_rough * kIblMaxMipLod;
    vec3  spec_e  = textureLod(cd_ibl_spec, Ripbr, lod_p).rgb;
    vec3  diff_e  = texture(cd_ibl_diff, Npbr).rgb;
    vec2  brdf_v  = texture(cd_brdf_lut, vec2(clamp(NoVpbr, 0.0, 1.0),
                                              clamp(pbr_rough, 0.0, 1.0))).rg;
    vec3  F_ibl   = F_Schlick_roughness_pbr(NoVpbr, F0pbr, pbr_rough);
    vec3  ibl_kD  = (vec3(1.0) - F_ibl) * (1.0 - pbr_metal);
    vec3  ibl_spec_p = spec_e * (F0pbr * brdf_v.x + vec3(brdf_v.y));

    // W8-AZ: env-spec gate is now PURELY sun-driven. The previous
    // any_non_sun*0.30 floor caused chrome spheres to keep showing
    // sky reflections in sun-off scenes lit only by area / point
    // lights -- the "olmayan gunes ve gokyuzu yansiyor" bug. Sky
    // comes from the sun-baked cubemap; if there is no sun, there
    // is no sky to reflect. Area / point lights still light the
    // surface through the direct lit_pbr accumulator above.
    float ibl_gate_p = clamp(pc.sun_dir.w, 0.0, 1.0);

    // W8-BC: Option B colored RT reflections. Cast a closest-hit ray
    // along Ripbr; on miss (sky), keep the full IBL spec sample, on
    // hit, sample the hit instance's albedo from the per-frame
    // SSBO and shade it with a sun-NoL using -dir as a pseudo-normal
    // (convex-hit approximation). The reflected colour is then
    // weighted by the same BRDF term the IBL spec uses, and blended
    // with the sky spec by sqrt(roughness): pure mirror (~0.2 weight
    // toward IBL) yields almost full coloured reflection; matte
    // (rough=1.0) keeps the IBL sky sample. NoL_hit is multiplied
    // by pc.sun_dir.w so sun-off scenes leave only the 0.3 ambient
    // floor, matching the W8-AZ env-spec gate philosophy.
    int   hit_inst   = -1;
    float scene_hit  = reflection_hit_id(v_world_pos, Npbr, Ripbr, 80.0, hit_inst);
    vec3  brdf_term  = F0pbr * brdf_v.x + vec3(brdf_v.y);
    vec3  ibl_spec_blended = ibl_spec_p;
    if (scene_hit > 0.5 && hit_inst >= 0) {
      vec3 hit_alb   = cd_instance_mats.data[hit_inst].albedo.rgb;
      // Pseudo-normal = surface-outward direction (-dir) on convex hits.
      vec3 pseudo_N  = normalize(-Ripbr);
      vec3 sun_L     = normalize(-pc.sun_dir.xyz);
      float NoL_hit  = max(dot(pseudo_N, sun_L), 0.0) * pc.sun_dir.w;
      vec3 refl_color = hit_alb * (0.3 + 0.7 * NoL_hit) * pc.sun_color.rgb;
      // Roughness-weighted blend: mirror -> refl_color, matte -> sky.
      float blend_t  = sqrt(clamp(pbr_rough, 0.0, 1.0));
      ibl_spec_blended = mix(refl_color * brdf_term, ibl_spec_p, blend_t);
    }

    vec3  ibl_term_p = (ibl_spec_blended + ibl_kD * diff_e * pbr_albedo) * ibl_gate_p;

    out_color = vec4(lit_pbr + ibl_term_p, 1.0);
    return;
  }

  // Alpha-test / alpha-mask support (glTF alphaMode MASK / BLEND first-cut).
  // fx_params.w carries alpha_cutoff > 0 for masked materials (leaves,
  // curtains, foliage). Sample the baseColor alpha channel and discard
  // fragments below the cutoff. Only active when fx_params.y (texture flag)
  // is also set — opaque or untextured draws leave fx_params.w == 0.
  if (pc.fx_params.w > 0.01 && pc.fx_params.y > 0.5) {
    float alpha_val = texture(cd_albedo_tex, v_uv).a;
    if (alpha_val < pc.fx_params.w) discard;
  }

  // tint.w sentinel: < 0.5 = "shadow-projection draw" - bypass lighting
  // entirely and output a flat dark silhouette. Used by the planar-
  // shadow pass that re-draws each caster, projected onto the floor
  // plane along the sun direction. Alpha-blend would soften the result
  // but isn't wired in MaterialDesc yet, so we ship hard shadows.
  if (pc.tint.w < 0.5) {
    out_color = vec4(pc.tint.rgb, 1.0);
    return;
  }

  // tint.w > 1.5 = "floor draw" - overlay an analytic grid in the
  // fragment shader instead of via ImGui's foreground draw list. This
  // keeps grid lines properly z-occluded by other geometry (the user-
  // flagged "grid objects arasindan gozukmemeli" issue) for free.
  // Otherwise identical to a normal lit shading path.
  // baseColor texture path - when the entity is flagged as
  // textured (fx_params.y > 0.5), override v_albedo with the
  // sampled albedo * tint. The default 1x1 white texture in the
  // descriptor lets non-textured draws fall through harmlessly,
  // but we short-circuit on the flag so the texture sample isn't
  // wasted on primitives that don't use it.
  vec3 albedo = v_albedo;
  if (pc.fx_params.y > 0.5) {
    vec3 sampled = texture(cd_albedo_tex, v_uv).rgb;
    albedo = sampled * pc.tint.rgb;
  }
  bool is_floor = (pc.tint.w > 1.5);
  float floor_fade = 1.0;  // 1 = full body, 0 = fully faded (sky-coloured)
  if (is_floor) {
    // Distance fade - body + lines both attenuate as the camera
    // looks out toward the horizon, so the floor 'reaches into
    // infinity' rather than ending in a hard square edge.
    // 60 m = full opacity, 200 m = fully transparent (faded to sky).
    // The underlying plane is 1000 m so the camera will never reach
    // its hard edge. v1.7 frame-graph swaps this for a true
    // screen-space procedural grid (fullscreen plane intersection).
    float d_xz = length(v_world_pos.xz);
    floor_fade = clamp(1.0 - (d_xz - 60.0) / 140.0, 0.0, 1.0);
    // Minor cells every 1 m, major every 5 m. fwidth gives a
    // distance-aware line width so lines stay constant-thickness as
    // the camera moves, instead of aliasing into glitter.
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
    // Apply fade to line intensity so lines also fade with distance.
    line_a *= floor_fade;
    albedo  = mix(albedo, line_col, clamp(line_a, 0.0, 1.0));
    // Fade the body too (mix back to a faint sky-grey).
    albedo  = mix(vec3(0.55, 0.60, 0.66) * 0.0, albedo, floor_fade);
  }

  vec3 N = normalize(v_world_normal);
  // R2 normal mapping for textured entities - perturbs the surface
  // normal with the tangent-space sample so the procedural Earth
  // bumps register as real 3D relief.
  if (pc.fx_params.y > 0.5) {
    vec3 nm_sample = texture(cd_normal_tex, v_uv).xyz * 2.0 - 1.0;
    mat3 TBN = cotangent_frame(N, v_world_pos, v_uv);
    N = normalize(TBN * nm_sample);
  }
  vec3 lit = vec3(0.0);

  // Directional sun + CSM shadow attenuation.
  vec3 Ld = normalize(-pc.sun_dir.xyz);
  float ndl_sun = max(dot(N, Ld), 0.0);
  float shade = sample_shadow(v_shadow_pos, N, Ld);
  lit += albedo * pc.sun_color.rgb * (pc.sun_dir.w * ndl_sun * shade);

  // Multi-light loop (gap #2 + #3). Per type:
  //   1 = Point  - Frostbite windowed inverse-square, RT shadow
  //   2 = Spot   - same + smoothstep cone falloff
  //   3 = Rect   - LTC polygon irradiance (Heitz 2016, Lambert fit)
  //   4 = Disk   - LTC polygon irradiance with disk approximated by quad
  // Each contribution gated by an inline RT shadow ray (Faz 1.7).
  for (uint li = 0; li < cd_lights.count; ++li) {
    vec3 lp_pos = cd_lights.slots[li].pos_range.xyz;
    float rng  = cd_lights.slots[li].pos_range.w;
    int   ltp  = int(cd_lights.slots[li].dir_type.w);
    if (rng <= 0.0) continue;

    if (ltp == 3 || ltp == 4) {
      // Area light: LTC polygon irradiance from 4 corners.
      vec3 ln = normalize(cd_lights.slots[li].dir_type.xyz);
      // W4-B: one-sided emission. Keep only shading points on the
      // front (emissive) side of the rect plane; back-side points are
      // dark like a real area light instead of lit through. The UBO
      // stores `dir_type.xyz` as the panel's emissive normal, so the
      // shading-point vector (P - lp) projected onto +ln must be
      // positive for the front hemisphere.
      // W8-Q: revert to one-sided emission (W4-B behaviour). Earlier
      // W8-P went two-sided based on a complaint that "diger
      // cisimleri aydinlanmiyor"; the real fix turned out to be
      // user-rotating the gizmo (now possible per-axis via W8-N),
      // and W8-P's two-sided variant produced visibly-wrong back-
      // hemisphere illumination once the user had the right
      // rotation. Cull shading points behind the rect normal so the
      // panel only lights its emissive front face.
      vec3 to_pt_w = v_world_pos - lp_pos;
      if (dot(to_pt_w, ln) <= 0.0) continue;
      // W8-N: use uploaded tangent directly. CPU stores the user-
      // controlled area_tangent in slot.tangent.xyz so the gizmo's
      // per-axis rotation can independently spin the rect around
      // its own normal without the Frisvad derivation overriding
      // the twist on every frame.
      vec3 right = normalize(cd_lights.slots[li].tangent.xyz);
      vec3 up_v  = cross(ln, right);
      float w = cd_lights.slots[li].extras.y * 0.5;
      float h = cd_lights.slots[li].extras.z * 0.5;
      // Corners as world-space positions, then made relative to the
      // shading point so the LTC frame transform yields directions.
      vec3 c0 = lp_pos - right*w - up_v*h - v_world_pos;
      vec3 c1 = lp_pos + right*w - up_v*h - v_world_pos;
      vec3 c2 = lp_pos + right*w + up_v*h - v_world_pos;
      vec3 c3 = lp_pos - right*w + up_v*h - v_world_pos;
      // W8-AJ: pass corners in REVERSED order (c0,c3,c2,c1).
      // The corner basis (-r,-u),(+r,-u),(+r,+u),(-r,+u) with
      // up_v = cross(ln, right) is CCW *as viewed from -ln*
      // (the back of the panel). Receivers culled by the
      // dot(P-lp, ln) > 0 test live on the +ln half-space and
      // see the polygon as CW, producing a negative LTC sum
      // that max(s,0) clamps to zero — that's why every prior
      // calibration step (W8-S/Y/AA/AB/AG) had no effect: E
      // was identically 0 for the LIT side. Reversing the
      // traversal order flips the winding without disturbing
      // the gizmo geometry or cd::light::area_bitangent
      // convention.
      float E = cd_ltc_polygon_irradiance(N, c0, c3, c2, c1);
      // Visibility test from area-light centre (one ray; full
      // many-sample area shadow needs a denoiser).
      vec3 to_c   = lp_pos - v_world_pos;
      float d_c   = max(length(to_c), 1e-4);
      vec3 Lc     = to_c / d_c;
      // W8-AF: restore area shadow ray (W8-AE diagnostic confirmed
      // the shadow ray was NOT the cause — floor stayed dark even
      // with vis_a forced to 1.0). LTC contribution itself is too
      // small to lift the floor above the tonemap noise floor at
      // current calibration. Restore W8-W bias so other receivers
      // (sphere grid, foreground primitives) keep their shadows.
      float area_tmax = min(d_c, rng);
      rayQueryEXT rq_a;
      rayQueryInitializeEXT(
          rq_a, cd_tlas,
          gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
          0xFFu, v_world_pos + N * 0.05, 0.05, Lc, area_tmax);
      while (rayQueryProceedEXT(rq_a)) { /* opaque-only walk */ }
      float vis_a = (rayQueryGetIntersectionTypeEXT(rq_a, true) ==
                     gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
      vec3  col   = cd_lights.slots[li].color_int.xyz;
      float ki    = cd_lights.slots[li].color_int.w;
      lit += albedo * col * (ki * E * vis_a);
      continue;
    }

    // Point / Spot path.
    vec3 to_p = lp_pos - v_world_pos;
    float d   = length(to_p);
    if (d < 1e-4) continue;
    vec3 Lp   = to_p / d;
    float ndl = max(dot(N, Lp), 0.0);
    if (ndl <= 0.0) continue;
    float atten = distance_atten(d, rng);
    float cone  = 1.0;
    if (ltp == 2) {
      vec3  axis    = normalize(cd_lights.slots[li].dir_type.xyz);
      float cos_b   = dot(-Lp, axis);
      float cos_out = cd_lights.slots[li].extras.x;
      // W4-H: read the *configured* inner cone (extras.w on CPU side)
      // instead of synthesising cos_out + 0.05. The synthesised value
      // produced a much narrower hot-spot than the CPU dialled in, so
      // the spot 'didn't appear to work' for shallow-aperture rigs.
      float cos_in_cpu = cd_lights.slots[li].extras.w;
      float cos_in     = clamp(max(cos_in_cpu, cos_out + 0.01),
                               cos_out + 0.01, 0.9999);
      cone          = smoothstep(cos_out, cos_in, cos_b);
      if (cone <= 0.0) continue;
    }
    // W8-C: RT shadow re-enabled. User wants flashlight semantics —
    // crisp cone of direct light + visible shadows on what's behind.
    // Bias tightened (tmin 0.30 + N*0.20) so self-hit and immediate-
    // neighbour false occlusion in the dense PBR sphere grid is
    // avoided while still letting genuinely-occluded shading points
    // fall into shadow.
    float ray_tmax = min(d, rng);
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq, cd_tlas,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
        0xFFu, v_world_pos + N * 0.20, 0.30, Lp, ray_tmax);
    while (rayQueryProceedEXT(rq)) { /* opaque-only walk */ }
    float vis = (rayQueryGetIntersectionTypeEXT(rq, true) ==
                 gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
    vec3  col = cd_lights.slots[li].color_int.xyz;
    float ki  = cd_lights.slots[li].color_int.w;
    lit += albedo * col * (ki * ndl * atten * vis * cone);
  }

  // Hemisphere ambient (sky-up / ground-down) - cheap stand-in for
  // non-textured prim entities. Textured entities (kGltf flagged via
  // fx_params.y > 0.5) get real IBL below.
  // W8-C: tied to pc.sun_color.w again. When sun is off, ambient = 0
  // so non-sun lights stay strictly local (spot only lights what's
  // inside its cone + RT shadow; nothing leaks as 'fill').
  // W8-BD: ADD a sun-independent dielectric floor below the hemisphere
  // (0.04 * albedo). User reported the CesiumMan character looked
  // chrome-like in a tungsten-only (sun OFF) scene — the lit half was
  // saturated warm yellow from the point light while the shadow half
  // was pitch-black (hemi=0, IBL gate=0). The bright-on-black contrast
  // reads as polished metal to the eye. A tiny constant albedo floor
  // gives the shadow side a perceptible diffuse base so the surface
  // looks dielectric (skin/cloth) instead of mirror-finish, without
  // re-introducing the W8-A 'always bright' fill that ruined spot
  // direction. 0.04 picked to stay well below the lit-side intensity.
  float up_t   = N.y * 0.5 + 0.5;
  vec3  sky_c  = vec3(0.55, 0.65, 0.85);
  vec3  gnd_c  = vec3(0.18, 0.16, 0.14);
  vec3  hemi   = mix(gnd_c, sky_c, up_t) * pc.sun_color.w;
  vec3  ambient = albedo * (hemi + vec3(0.020));  // W8-BE: floor 0.04 -> 0.020 per user "biraz daha koyu olsun"

  // R2: True IBL with MR map. Karis split-sum:
  //   IBL = kD * irradiance(N) * albedo + prefiltered(R, rough*mipMax)
  //         * (F0 * brdf.x + brdf.y)
  // MR map gives per-pixel metallic + roughness + AO so the same
  // material sweep covers ocean (rough water), continents (mid),
  // and polar ice (matte snow).
  if (pc.fx_params.y > 0.5) {
    vec4 mr_sample = texture(cd_mr_tex, v_uv);
    float roughness = clamp(mr_sample.g, 0.04, 1.0);
    // W8-BD: clamp the Lit-path metallic to <= 0.05. cd_mr_tex is the
    // procedural Earth MR (binding 9 is never replaced per-entity), so
    // CesiumMan currently samples Earth's metallic at its own UVs.
    // Earth caps at 0.05 today, but a future glTF with a real MR
    // texture would push F0 toward albedo and chrome the dielectric
    // (skin/cloth) character — the very artefact W8-BD is fixing.
    // Hard ceiling keeps the Lit-path strictly dielectric until the
    // per-entity MR descriptor lands (v1.6+ texture array path).
    float metallic  = clamp(mr_sample.b, 0.0, 0.05);
    float ao_factor = mr_sample.a;
    vec3 F0_ibl = mix(vec3(0.04), albedo, metallic);
    vec3 V_v    = normalize(pc.camera_pos.xyz - v_world_pos);
    vec3 R_v    = reflect(-V_v, N);
    float NoV_v = max(dot(N, V_v), 0.0);
    float lod   = roughness * kIblMaxMipLod;
    vec3 spec_e = textureLod(cd_ibl_spec, R_v, lod).rgb;
    vec3 diff_e = texture(cd_ibl_diff, N).rgb;
    vec2 brdf_v = texture(cd_brdf_lut, vec2(clamp(NoV_v, 0.0, 1.0),
                                            clamp(roughness, 0.0, 1.0))).rg;
    vec3 ibl_F  = F0_ibl * brdf_v.x + vec3(brdf_v.y);
    vec3 ibl_kD = (vec3(1.0) - ibl_F) * (1.0 - metallic);
    vec3 ibl    = (ibl_kD * diff_e * albedo + spec_e * ibl_F) * ao_factor;
    // W8-C: revert to sun-only IBL gate. User explicitly wants
    // spot/point/area to act as crisp local emitters with shadows on
    // their occluders — the W8-A non-sun "fill" produced an "always
    // bright" feel that ruined the spot's directional identity.
    // Genuine indirect bounce will return with the R4 GI pass.
    float ibl_gate = clamp(pc.sun_dir.w * 0.6, 0.0, 1.0);
    ambient += ibl * ibl_gate * 0.55;
  }

  // Inline GTAO approximation (v1.4 day-ship wire-in). True multi-pass
  // post_gtao::kGtaoMainCS dispatches land in v1.7 frame-graph rework;
  // here we use a cheap normal-derivative curvature heuristic to
  // darken convex creases. Strength = pc.fx_params.z (0..1).
  float ao_strength = clamp(pc.fx_params.z, 0.0, 1.0);
  if (ao_strength > 0.001) {
    vec3 dn_dx = dFdx(v_world_normal);
    vec3 dn_dy = dFdy(v_world_normal);
    float curv = clamp(length(dn_dx) + length(dn_dy), 0.0, 1.0);
    float ao   = 1.0 - ao_strength * curv * 0.85;
    ambient   *= ao;
  }

  // R6 advanced BRDF lobes - inline approximations matching:
  //   cd::brdf_sheen_clearcoat::kInlineRimApproxGlsl  (clearcoat + sheen)
  //   cd::brdf_sss::kInlineBurleyWrapGlsl             (wrap-diffusion SSS)
  // For the proper full BRDF kernels (Estevez Charlie + Filament
  // clearcoat D*V + Burley separable diffusion), see:
  //   cd::brdf_sheen_clearcoat::kSheenClearcoatGlsl
  //   cd::brdf_sss::kBurleySeparableBlurCS
  // Those land via the v1.7 material-graph dispatch.
  float fx_cc    = clamp(pc.fx_params4.x, 0.0, 1.0);
  float fx_sheen = clamp(pc.fx_params4.y, 0.0, 1.0);
  float fx_sss   = clamp(pc.fx_params4.z, 0.0, 1.0);
  if (fx_cc > 0.001 || fx_sheen > 0.001 || fx_sss > 0.001) {
    vec3 V_b = normalize(pc.camera_pos.xyz - v_world_pos);
    float NoV_b = max(dot(N, V_b), 0.0);
    // Clearcoat: second Schlick Fresnel lobe with IOR ~ 1.5 (F0_cc =
    // 0.04), tinted white, scales with view angle. Adds shiny lacquer.
    if (fx_cc > 0.001) {
      vec3 R_b = reflect(-V_b, N);
      vec3 spec_cc = textureLod(cd_ibl_spec, R_b, 0.5 * kIblMaxMipLod).rgb;
      float fres_cc = 0.04 + 0.96 * pow(1.0 - NoV_b, 5.0);
      lit += spec_cc * fres_cc * fx_cc * 0.6;
    }
    // Sheen: Charlie distribution-inspired rim term. cos^n with high
    // n + saturating boost gives a velvet edge brighten.
    if (fx_sheen > 0.001) {
      float rim = pow(1.0 - NoV_b, 4.0);
      vec3 sheen_col = vec3(0.95, 0.92, 0.88);
      lit += sheen_col * rim * fx_sheen * 1.2;
    }
    // SSS: Burley-inspired wrap diffusion - boost backlit pixels with
    // a warm subsurface tint, simulating skin/wax light bleed.
    if (fx_sss > 0.001) {
      vec3 Ld_b = normalize(-pc.sun_dir.xyz);
      float backlit = clamp(dot(-N, Ld_b), 0.0, 1.0);
      vec3 sss_col = vec3(0.95, 0.55, 0.45);
      lit += sss_col * pow(backlit, 1.5) * fx_sss * pc.sun_dir.w * 0.8;
    }
  }

  vec3  c       = lit + ambient;

  // R3: tonemap + exposure + bloom + saturation + gamma all live in
  // the composite pass now. Scene shaders below only apply world-
  // space effects (vignette / CA / grain / fog / aerial / shafts)
  // that need scene data, then emit linear HDR.

  // R7 camera composition - inline approximations matching
  // cd::post_camera::kInlineCameraGlsl (vignette / chromatic / grain).
  // For the proper off-screen LUT/blur post pass see the v1.7 frame-
  // graph ship.
  // Cheap radial coordinate from the camera-relative direction. Not
  // a true screen-space UV but functionally maps to 0 (centre) -> 1+
  // (edges) without needing the swapchain extent.
  vec3 cam_dir = normalize(v_world_pos - pc.camera_pos.xyz);
  float radial = length(cam_dir.xy);
  float vignette = clamp(pc.fx_params2.y, 0.0, 1.0);
  if (vignette > 0.001) {
    float vmask = smoothstep(0.0, 1.4, radial);
    c *= mix(1.0, 1.0 - vmask, vignette);
  }
  float ca = clamp(pc.fx_params2.z, 0.0, 1.0);
  if (ca > 0.001) {
    // Cheap CA: shift hue toward warmth in centre, cool at edges.
    c.r *= 1.0 + ca * 0.08;
    c.b *= 1.0 - ca * 0.08;
  }
  float grain = clamp(pc.fx_params2.w, 0.0, 1.0);
  if (grain > 0.001) {
    float g = fract(sin(dot(v_world_pos.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (g - 0.5) * grain * 0.05;
  }

  // Inline atmospherics (v1.4 day-ship wire-in).
  //   x = exponential height fog density
  //   y = aerial perspective strength
  //   z = clouds coverage placeholder (needs noise sampler, v1.7)
  //   w = light shafts strength  - R5 inline approximation here:
  //       attenuate visibility radially from the on-screen sun
  //       direction and brighten low-luma pixels in that cone.
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
  // R5 inline god rays - matches cd::light_shafts::kInlineConeShaftGlsl.
  // Full screen-space radial blur lives at
  // cd::light_shafts::kRadialBlurCS and dispatches with the v1.7
  // frame-graph rework.
  float shafts = clamp(pc.fx_params3.w, 0.0, 1.0);
  if (shafts > 0.001 && pc.sun_dir.w > 0.001) {
    vec3 cam_to_p = normalize(v_world_pos - pc.camera_pos.xyz);
    vec3 sun_L    = normalize(-pc.sun_dir.xyz);
    float align  = max(dot(cam_to_p, sun_L), 0.0);
    float shaft  = pow(align, 32.0) * shafts;
    vec3 shaft_col = pc.sun_color.rgb * pc.sun_dir.w;
    c += shaft_col * shaft * 0.6;
  }

  // Linear HDR output - composite pass owns the gamma transform.

  // Debug view modes (fx_params4.w):
  //   1 albedo only, 2 world normal, 3 MR map, 4 AO, 5 perturbed
  //   normal (post-normal-map), 6 UVs as RG.
  int view_mode = int(pc.fx_params4.w + 0.5);
  if (view_mode == 1) {
    out_color = vec4(albedo, 1.0);
    return;
  } else if (view_mode == 2) {
    out_color = vec4(normalize(v_world_normal) * 0.5 + 0.5, 1.0);
    return;
  } else if (view_mode == 3) {
    vec4 mr_s = (pc.fx_params.y > 0.5) ? texture(cd_mr_tex, v_uv) : vec4(0,0.5,0.04,1);
    out_color = vec4(0.0, mr_s.g, mr_s.b, 1.0);
    return;
  } else if (view_mode == 4) {
    vec4 mr_s = (pc.fx_params.y > 0.5) ? texture(cd_mr_tex, v_uv) : vec4(0,0,0,1);
    out_color = vec4(vec3(mr_s.a), 1.0);
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
