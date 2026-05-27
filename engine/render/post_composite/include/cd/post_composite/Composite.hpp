// =============================================================================
// CHROMODYNAMIC — cd/post_composite/Composite.hpp
//
// Forward-renderer composite pass — a single fullscreen-triangle FS that
// bundles every screen-space post-fx the engine ships into one render
// pass. Output: tonemapped LDR for the swapchain + the next-frame TAA
// history target (MRT location=1).
//
// Bindings (set 0):
//   0  sampler2D cd_hdr_color       (RGBA16F scene HDR)
//   1  sampler2D cd_bloom_mip0      (RGBA16F bloom upsample mip 0)
//   2  sampler2D cd_depth           (D32 / linearised inside via near/far)
//   3  sampler2D cd_gbuf_normal     (RGBA16F world-space normal + flag)
//   4  sampler2D cd_history_prev    (BGRA8U previous-frame composite out)
//
// Outputs:
//   location 0 — tonemapped LDR (swapchain BGRA8U)
//   location 1 — next-frame TAA history (BGRA8U), same content as loc 0
//
// Push constant (256 B): cd::post_composite::Push (see below).
//
// Effects (in pipeline order inside the FS):
//   * Chromatic aberration (radial RGB split)
//   * Normal-aware AO (8-ring horizon scan)
//   * Aerial perspective + uniform exp fog
//   * Depth-of-field (8-tap golden-spiral bokeh)
//   * Screen-space reflections (24-step world-march, edge-fade + Fresnel)
//   * Camera-velocity motion blur (prev-frame reprojection)
//   * Light shafts (16-tap radial sun march)
//   * Bloom additive halo
//   * Pre-tonemap exposure boost
//   * Tonemap (Narkowicz / Hill / Hable / AGX)
//   * Saturation pull-away
//   * Gamma 2.2 encode
//   * Vignette
//   * Film grain
//   * TAA history blend + 3x3 neighbourhood clamp
//
// Each effect short-circuits when its strength dial is ≤ epsilon, so
// the cost is opt-in. Sky pixels are detected via the depth-far
// threshold + G-Buffer normal.w == 0 flag.
//
// References:
//   * Jimenez 2016 — GTAO horizon scan
//   * Stachowiak 2015 — SSR scene compositing
//   * Sousa 2013 — bokeh CoC formulation
//   * Mitchell 2007 — analytic god rays
//   * Karis 2014 — TAA history with neighbourhood clamp
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace cd::post_composite
{

// ---- Push-constant layout (256 B) ------------------------------------------
//
// Each block is a vec4 in GLSL std140 layout. All channels are
// floating-point (no UBO ints needed — tonemap op is rounded from
// fx.x). Layout exactly mirrors the FS `layout(push_constant)` block
// in kCompositeFS.

struct Push
{
    float fx[4];        ///< x=tonemap_op, y=exposure, z=sat_boost, w=bloom_strength
    float ao[4];        ///< x=ao_strength, y=ao_radius_px, z=near, w=far
    float dof[4];       ///< x=dof_strength, y=focus_distance(m), z=focus_range(m), w=max_blur_px
    float shafts[4];    ///< x=sun_uv.x, y=sun_uv.y, z=strength (<0 → off), w=decay
    float sun_col[4];   ///< xyz=linear sun colour, w=reserved
    float atmo[4];      ///< x=fog_density(1/m), y=aerial_strength, z=vignette, w=film_grain
    float lens[4];      ///< x=chromatic_aberration_px, y/z/w = sun_dir.xyz (world-space, toward scene)
    float cam_right[4]; ///< xyz=world-space right basis, w=half_w = tan(fov/2)*aspect
    float cam_up[4];    ///< xyz=world-space up basis,    w=half_h = tan(fov/2)
    float cam_fwd[4];   ///< xyz=world-space forward,     w=taa_alpha [0, 0.97]
    float cam_pos[4];   ///< xyz=world camera origin,     w=reserved
    float ssr[4];       ///< x=ssr_strength, y=max_distance_m, z=max_steps, w=fade_edge
    float prev_cam_right[4]; ///< xyz=prev right, w=prev_half_w
    float prev_cam_up[4];    ///< xyz=prev up,    w=prev_half_h
    float prev_cam_fwd[4];   ///< xyz=prev fwd,   w=mblur_strength
    float prev_cam_pos[4];   ///< xyz=prev pos,   w=mblur_samples (float, rounded)
};

static_assert(sizeof(Push) == 256, "post_composite::Push must equal 256 B");

// ---- Binding slot names -----------------------------------------------------

enum class BindingSlot : std::uint32_t
{
    kHdrColor    = 0,
    kBloomMip0   = 1,
    kDepth       = 2,
    kGbufNormal  = 3,
    kHistoryPrev = 4,
};

inline constexpr std::uint32_t kBindingCount = 5;

// ---- GLSL — fullscreen-triangle VS -----------------------------------------

constexpr std::string_view kCompositeVS = R"glsl(
#version 450
layout(location = 0) out vec2 v_uv;
void main() {
  // Single triangle covering NDC [-1, 3] x [-1, 3]; clipped to viewport.
  vec2 ndc = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  v_uv = ndc * vec2(1.0, 1.0);
  gl_Position = vec4(ndc * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

// ---- GLSL — forward composite FS -------------------------------------------

constexpr std::string_view kCompositeFS = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D cd_hdr_color;
layout(set = 0, binding = 1) uniform sampler2D cd_bloom_mip0;
layout(set = 0, binding = 2) uniform sampler2D cd_depth;
layout(set = 0, binding = 3) uniform sampler2D cd_gbuf_normal;
layout(set = 0, binding = 4) uniform sampler2D cd_history_prev;
layout(push_constant) uniform PC {
  vec4 fx;
  vec4 ao;
  vec4 dof;
  vec4 shafts;
  vec4 sun_col;
  vec4 atmo;
  vec4 lens;
  vec4 cam_right;
  vec4 cam_up;
  vec4 cam_fwd;
  vec4 cam_pos;
  vec4 ssr;
  vec4 prev_cam_right;
  vec4 prev_cam_up;
  vec4 prev_cam_fwd;
  vec4 prev_cam_pos;
} pc;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_history;

float linearize_z(float d) {
  return pc.ao.z * pc.ao.w / max(pc.ao.w - d * (pc.ao.w - pc.ao.z), 1e-4);
}

vec3 world_pos_from_uv(vec2 uv, float depth) {
  vec2 ndc = uv * 2.0 - 1.0;
  float lz = pc.ao.z * pc.ao.w / max(pc.ao.w - depth * (pc.ao.w - pc.ao.z), 1e-4);
  vec3 ray = pc.cam_fwd.xyz
           + ndc.x * pc.cam_right.w * pc.cam_right.xyz
           - ndc.y * pc.cam_up.w    * pc.cam_up.xyz;
  return pc.cam_pos.xyz + ray * lz;
}

vec3 world_to_uv(vec3 w) {
  vec3 rel = w - pc.cam_pos.xyz;
  float fwd_dot = dot(rel, pc.cam_fwd.xyz);
  if (fwd_dot <= 0.0) return vec3(-1.0);
  float right_dot = dot(rel, pc.cam_right.xyz);
  float up_dot    = dot(rel, pc.cam_up.xyz);
  float ndc_x = (right_dot / fwd_dot) / pc.cam_right.w;
  float ndc_y = (up_dot    / fwd_dot) / pc.cam_up.w;
  float uv_x  = ndc_x * 0.5 + 0.5;
  float uv_y  = -ndc_y * 0.5 + 0.5;
  float lz = fwd_dot;
  float d = (pc.ao.w - pc.ao.z * pc.ao.w / lz) / (pc.ao.w - pc.ao.z);
  return vec3(uv_x, uv_y, clamp(d, 0.0, 1.0));
}

vec2 prev_world_to_uv(vec3 w) {
  vec3 rel = w - pc.prev_cam_pos.xyz;
  float fwd_dot = dot(rel, pc.prev_cam_fwd.xyz);
  if (fwd_dot <= 0.0) return vec2(-1.0);
  float right_dot = dot(rel, pc.prev_cam_right.xyz);
  float up_dot    = dot(rel, pc.prev_cam_up.xyz);
  float ndc_x = (right_dot / fwd_dot) / pc.prev_cam_right.w;
  float ndc_y = (up_dot    / fwd_dot) / pc.prev_cam_up.w;
  return vec2(ndc_x * 0.5 + 0.5, -ndc_y * 0.5 + 0.5);
}

float depth_ao(vec2 uv, float center_d) {
  if (center_d >= 0.999) return 1.0;
  vec4 N_packed = texture(cd_gbuf_normal, uv);
  if (N_packed.w < 0.5) return 1.0;
  vec3 N = normalize(N_packed.xyz);
  vec3 wc = world_pos_from_uv(uv, center_d);
  float lc = linearize_z(center_d);
  vec2 px = 1.0 / vec2(textureSize(cd_depth, 0));
  vec2 ring[8] = vec2[8](
    vec2( 1.0,  0.0), vec2( 0.707,  0.707),
    vec2( 0.0,  1.0), vec2(-0.707,  0.707),
    vec2(-1.0,  0.0), vec2(-0.707, -0.707),
    vec2( 0.0, -1.0), vec2( 0.707, -0.707));
  float occ = 0.0;
  float weight_sum = 0.0;
  for (int i = 0; i < 8; ++i) {
    vec2 sp = uv + ring[i] * pc.ao.y * px;
    float nd = texture(cd_depth, sp).r;
    float ln = linearize_z(nd);
    float dz = lc - ln;
    float bias = 0.02 * lc;
    if (dz <= bias) continue;
    vec3 ws = world_pos_from_uv(sp, nd);
    vec3 dir = ws - wc;
    float dlen = length(dir);
    if (dlen < 1e-4) continue;
    dir /= dlen;
    float n_dot = max(dot(dir, N), 0.0);
    float falloff = 1.0 / (1.0 + dlen * 2.0);
    occ += clamp((dz - bias) / 0.5, 0.0, 1.0) * n_dot * falloff;
    weight_sum += n_dot;
  }
  if (weight_sum < 1e-4) return 1.0;
  occ /= max(weight_sum, 1.0);
  return clamp(1.0 - occ, 0.0, 1.0);
}

vec3 ssr_color(vec2 uv, vec3 wp, vec3 N) {
  if (pc.ssr.x <= 0.001) return vec3(0.0);
  vec3 V = normalize(pc.cam_pos.xyz - wp);
  vec3 R = reflect(-V, N);
  int max_steps = int(max(pc.ssr.z, 1.0));
  float max_dist = max(pc.ssr.y, 0.1);
  float step_size = max_dist / float(max_steps);
  for (int i = 1; i <= max_steps; ++i) {
    vec3 sample_wp = wp + R * step_size * float(i);
    vec3 sp = world_to_uv(sample_wp);
    if (sp.x < 0.0 || sp.x > 1.0 || sp.y < 0.0 || sp.y > 1.0 || sp.z < 0.0)
      return vec3(0.0);
    float scene_d = texture(cd_depth, sp.xy).r;
    float scene_lz = linearize_z(scene_d);
    float march_lz = linearize_z(sp.z);
    float thickness = step_size * 1.5;
    if (march_lz > scene_lz && (march_lz - scene_lz) < thickness) {
      vec2 ec = abs(sp.xy - vec2(0.5)) * 2.0;
      float ef = clamp(1.0 - max(ec.x, ec.y) * pc.ssr.w, 0.0, 1.0);
      float NoV = max(dot(N, V), 0.0);
      float fresnel = pow(1.0 - NoV, 3.0);
      vec3 hit = texture(cd_hdr_color, sp.xy).rgb;
      return hit * pc.ssr.x * ef * (0.3 + fresnel * 0.7);
    }
  }
  return vec3(0.0);
}

vec3 sample_chromab(vec2 uv) {
  if (pc.lens.x <= 0.001) return texture(cd_hdr_color, uv).rgb;
  vec2 vc = uv - vec2(0.5);
  float r = length(vc);
  vec2 dir = (r > 1e-4) ? vc / r : vec2(0.0);
  vec2 px = 1.0 / vec2(textureSize(cd_hdr_color, 0));
  float offs = pc.lens.x * r * r * 8.0;
  vec3 c;
  c.r = texture(cd_hdr_color, uv + dir * offs * px).r;
  c.g = texture(cd_hdr_color, uv).g;
  c.b = texture(cd_hdr_color, uv - dir * offs * px).b;
  return c;
}

void main() {
  vec3 c = sample_chromab(v_uv);
  float center_d = texture(cd_depth, v_uv).r;

  // AO
  float ao = depth_ao(v_uv, center_d);
  c *= mix(1.0, ao, clamp(pc.ao.x, 0.0, 1.0));

  // Aerial perspective + uniform exp fog + volumetric sun in-scatter.
  // pc.lens.yzw packs the world-space sun direction (toward scene);
  // when the view ray faces back along it we get a bright forward-
  // scattering glow through fog. Cheap single-scatter Henyey-Greenstein
  // phase, no froxel — composite-inline so no extra render target.
  if (center_d < 0.999 && (pc.atmo.x > 0.001 || pc.atmo.y > 0.001)) {
    float lz = linearize_z(center_d);
    vec3 horizon_base = vec3(0.78, 0.86, 0.96);
    vec3 horizon_lit  = mix(horizon_base, pc.sun_col.rgb, 0.35);

    // Single-scatter sun in-scatter colour. Henyey-Greenstein phase
    // (g=0.6 — forward-scattering haze) modulates by view·-sun.
    vec3 wp_end = world_pos_from_uv(v_uv, center_d);
    vec3 view_dir = normalize(wp_end - pc.cam_pos.xyz);
    vec3 sun_dir_world = pc.lens.yzw;
    float cos_th = max(0.0, dot(view_dir, -normalize(sun_dir_world)));
    const float g  = 0.6;
    const float g2 = g * g;
    float phase = (1.0 - g2) / (4.0 * 3.14159265 *
                  pow(1.0 + g2 - 2.0 * g * cos_th, 1.5));
    vec3 fog_colour = mix(horizon_lit,
                          pc.sun_col.rgb * (phase * 6.0 + 0.5),
                          clamp(cos_th * 0.8, 0.0, 1.0));

    float fog_t = 1.0 - exp(-lz * max(pc.atmo.x, 0.0));
    float aer_t = 1.0 - exp(-lz * 0.08);
    c = mix(c, fog_colour,  clamp(fog_t, 0.0, 1.0));
    c = mix(c, horizon_lit, clamp(aer_t * pc.atmo.y, 0.0, 1.0));
  }

  // DOF
  if (pc.dof.x > 0.001 && center_d < 0.999) {
    float lz = linearize_z(center_d);
    float coc = clamp(abs(lz - pc.dof.y) / max(pc.dof.z, 0.001), 0.0, 1.0);
    if (coc > 0.05) {
      vec2 px = 1.0 / vec2(textureSize(cd_hdr_color, 0));
      float r = coc * pc.dof.w;
      vec2 spiral[8] = vec2[8](
        vec2( 0.866,  0.500), vec2( 0.000,  1.000),
        vec2(-0.866,  0.500), vec2(-0.866, -0.500),
        vec2( 0.000, -1.000), vec2( 0.866, -0.500),
        vec2( 0.500,  0.000), vec2(-0.500,  0.000));
      vec3 dof_sum = vec3(0.0);
      for (int i = 0; i < 8; ++i)
        dof_sum += texture(cd_hdr_color, v_uv + spiral[i] * r * px).rgb;
      dof_sum *= (1.0 / 8.0);
      c = mix(c, dof_sum,
              smoothstep(0.05, 0.30, coc) * clamp(pc.dof.x, 0.0, 1.0));
    }
  }

  // SSR
  vec4 ssr_N = texture(cd_gbuf_normal, v_uv);
  if (pc.ssr.x > 0.001 && ssr_N.w > 0.5 && center_d < 0.999) {
    vec3 wp = world_pos_from_uv(v_uv, center_d);
    vec3 N  = normalize(ssr_N.xyz);
    c += ssr_color(v_uv, wp, N);
  }

  // Camera-velocity motion blur
  float mblur_strength = pc.prev_cam_fwd.w;
  if (mblur_strength > 0.001 && center_d < 0.999) {
    vec3 wp_now = world_pos_from_uv(v_uv, center_d);
    vec2 prev_uv = prev_world_to_uv(wp_now);
    if (prev_uv.x >= 0.0 && prev_uv.x <= 1.0 &&
        prev_uv.y >= 0.0 && prev_uv.y <= 1.0) {
      vec2 velocity = v_uv - prev_uv;
      float vlen = length(velocity);
      if (vlen > 0.001) {
        float vmax = 0.1;
        if (vlen > vmax) velocity *= vmax / vlen;
        int   nsamples = int(max(pc.prev_cam_pos.w, 1.0));
        vec3  blur_sum = vec3(0.0);
        for (int i = 0; i < nsamples; ++i) {
          float t = float(i) / float(nsamples - 1) - 0.5;
          vec2 sp = v_uv + velocity * t;
          blur_sum += texture(cd_hdr_color, clamp(sp, vec2(0.0), vec2(1.0))).rgb;
        }
        blur_sum *= (1.0 / float(nsamples));
        c = mix(c, blur_sum, clamp(mblur_strength, 0.0, 1.0));
      }
    }
  }

  // Light shafts
  if (pc.shafts.z > 0.0) {
    vec2 to_sun = pc.shafts.xy - v_uv;
    float dist = length(to_sun);
    float density = 0.0;
    const int kShaftSteps = 16;
    for (int i = 0; i < kShaftSteps; ++i) {
      float t = float(i) / float(kShaftSteps - 1);
      vec2 sp = v_uv + to_sun * t;
      if (sp.x < 0.0 || sp.x > 1.0 || sp.y < 0.0 || sp.y > 1.0) continue;
      float sd = texture(cd_depth, sp).r;
      density += smoothstep(0.995, 0.999, sd);
    }
    density *= (1.0 / float(kShaftSteps));
    float falloff = exp(-dist * max(pc.shafts.w, 0.001));
    c += pc.sun_col.rgb * density * falloff * pc.shafts.z;
  }

  // Bloom add
  vec3 bloom = texture(cd_bloom_mip0, v_uv).rgb;
  c += bloom * max(pc.fx.w, 0.0);
  // Exposure
  c *= max(pc.fx.y, 0.001);

  // Tonemap
  int op = int(pc.fx.x + 0.5);
  if (op == 0) {
    const float a_ = 2.51, b_ = 0.03, c_ = 2.43, d_ = 0.59, e_ = 0.14;
    c = clamp((c * (a_*c + b_)) / (c * (c_*c + d_) + e_),
              vec3(0.0), vec3(1.0));
  } else if (op == 1) {
    vec3 a = c * (c + 0.0245786) - 0.000090537;
    vec3 b = c * (0.983729 * c + 0.4329510) + 0.238081;
    c = clamp(a / b, vec3(0.0), vec3(1.0));
  } else if (op == 2) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30, W = 11.2;
    vec3 cf = ((c * (A*c + C*B) + D*E) / (c * (A*c + B) + D*F)) - E/F;
    vec3 wf = vec3(((W * (A*W + C*B) + D*E) / (W * (A*W + B) + D*F)) - E/F);
    c = clamp(cf / wf, vec3(0.0), vec3(1.0));
  } else {
    const float kMinEv = -12.47393, kMaxEv = 4.026069;
    vec3 lg = clamp((log2(max(c, vec3(1e-10))) - vec3(kMinEv)) /
                    (kMaxEv - kMinEv), vec3(0.0), vec3(1.0));
    vec3 x2 = lg * lg;
    vec3 x4 = x2 * x2;
    c = clamp( 15.5  * x4 * x2 - 40.14 * x4 * lg + 31.96 * x4
             -  6.868 * x2 * lg + 0.4298 * x2 + 0.1191 * lg - 0.00232,
             vec3(0.0), vec3(1.0));
  }
  // Saturation pull-away
  {
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    float sb = max(pc.fx.z, 0.001);
    c = clamp(mix(vec3(luma), c, sb), vec3(0.0), vec3(1.0));
  }
  c = pow(c, vec3(1.0/2.2));

  // Vignette
  if (pc.atmo.z > 0.001) {
    vec2 vc = v_uv - vec2(0.5);
    float r2 = dot(vc, vc);
    float v = 1.0 - r2 * 4.0 * clamp(pc.atmo.z, 0.0, 1.0);
    c *= clamp(v, 0.0, 1.0);
  }

  // Film grain
  if (pc.atmo.w > 0.001) {
    vec2 sp = v_uv * vec2(textureSize(cd_hdr_color, 0));
    float h = fract(sin(dot(sp, vec2(12.9898, 78.233))) * 43758.5453);
    c += (h - 0.5) * pc.atmo.w * 0.15;
  }

  // TAA neighbourhood-clamp blend
  float taa_alpha = clamp(pc.cam_fwd.w, 0.0, 0.97);
  if (taa_alpha > 0.001 && center_d < 0.999) {
    vec3 wp_taa = world_pos_from_uv(v_uv, center_d);
    vec2 prev_uv = prev_world_to_uv(wp_taa);
    if (prev_uv.x >= 0.0 && prev_uv.x <= 1.0 &&
        prev_uv.y >= 0.0 && prev_uv.y <= 1.0) {
      vec3 hist = texture(cd_history_prev, prev_uv).rgb;
      vec2 px = 1.0 / vec2(textureSize(cd_history_prev, 0));
      vec3 lo = c, hi = c;
      for (int j = -1; j <= 1; ++j)
      for (int i = -1; i <= 1; ++i) {
        if (i == 0 && j == 0) continue;
        vec3 n = texture(cd_hdr_color, v_uv + vec2(i, j) * px).rgb;
        lo = min(lo, n);
        hi = max(hi, n);
      }
      hist = clamp(hist, lo, hi);
      c = mix(c, hist, taa_alpha);
    }
  }

  out_color = vec4(c, 1.0);
  out_history = vec4(c, 1.0);
}
)glsl";

}  // namespace cd::post_composite
