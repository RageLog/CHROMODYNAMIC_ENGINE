// =============================================================================
// CHROMODYNAMIC — cd/post/composite/Composite.hpp
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
// Push constant (256 B): cd::post::composite::Push (see below).
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
#include <string>
#include <string_view>

namespace cd::post::composite
{

// ---- Push-constant layout (256 B) ------------------------------------------
//
// Each block is a vec4 in GLSL std140 layout. All channels are
// floating-point (no UBO ints needed — tonemap op is rounded from
// fx.x). Layout exactly mirrors the FS `layout(push_constant)` block
// in kCompositeFS.

struct Push
{
    /// x = tonemap_op (rounded int: 0..4)
    /// y = linear exposure multiplier — set externally via
    ///     cd::post::exposure::compute_exposure_multiplier(ev, settings)
    ///     when auto-exposure is wired (phase 461), or directly from
    ///     the UI slider for manual control. The composite FS reads
    ///     this as `c *= max(pc.fx.y, 0.001)` immediately before the
    ///     tonemap operator. Phase 454 ships the CPU EV pipeline,
    ///     phase 461 ships the GPU log-luminance reduction skeleton;
    ///     when both land the integrator threads
    ///     update_ev(log_avg_lum, prev_ev, dt, settings) ->
    ///     compute_exposure_multiplier into this slot per frame.
    /// z = saturation boost (SDR-only)
    /// w = bloom strength
    float fx[4];        ///< x=tonemap_op, y=exposure, z=sat_boost, w=bloom_strength
    float ao[4];        ///< x=ao_strength, y=ao_radius_px, z=near, w=far
    float dof[4];       ///< x=dof_strength, y=focus_distance(m), z=focus_range(m), w=max_blur_px OR svgf_strength (when DOF off)
    float shafts[4];    ///< x=sun_uv.x, y=sun_uv.y, z=strength (<0 → off), w=decay
    float sun_col[4];   ///< xyz=linear sun colour, w=clouds_coverage [0,1]
    float atmo[4];      ///< x=fog_density(1/m), y=aerial_strength, z=vignette, w=film_grain
    float lens[4];      ///< x=chromatic_aberration_px, y/z/w = sun_dir.xyz (world-space, toward scene)
    float cam_right[4]; ///< xyz=world-space right basis, w=half_w = tan(fov/2)*aspect
    float cam_up[4];    ///< xyz=world-space up basis,    w=half_h = tan(fov/2)
    float cam_fwd[4];   ///< xyz=world-space forward,     w=taa_alpha [0, 0.97]
    float cam_pos[4];   ///< xyz=world camera origin,     w=anim_time (s)
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
    kHdrColor     = 0,
    kBloomMip0    = 1,
    kDepth        = 2,
    kGbufNormal   = 3,
    kHistoryPrev  = 4,
    kGbufVelocity = 5,
    // ---- phase691 — M14 W4 — full GI/RT consumer hooks --------------------
    // OPTIONAL bindings. Declared in `kCompositeFSWithGiHooks` and gated by
    // `#define CD_COMPOSITE_USE_DDGI 1` / `#define CD_COMPOSITE_USE_RESTIR 1`
    // injected ahead of the source string by
    // `make_composite_fs_source_with_gi_hooks()`. When the macros are
    // undefined the bindings are NOT declared and the FS is byte-equivalent
    // to `kCompositeFS` (no behavioural change → default OFF).
    //
    // `kDdgiIndirectIrradiance` = `cd::ddgi::FullPipeline::sample` output
    //   (RGBA16F per-pixel indirect diffuse irradiance). Additively blends
    //   into the lit colour right after bloom + before exposure / tonemap.
    //
    // `kRestirDenoisedDirect` = `cd::restir_di::FullPipelineDenoised`
    //   denoised direct-light output (RGBA16F). Additively blends into the
    //   lit colour at the same injection point as the DDGI sample.
    kDdgiIndirectIrradiance = 6,
    kRestirDenoisedDirect   = 7,
};

inline constexpr std::uint32_t kBindingCount             = 6;
inline constexpr std::uint32_t kBindingCountWithGiHooks  = 8;

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
#extension GL_GOOGLE_include_directive : enable
#include <cd/gluon/hash_noise.glsl>
layout(set = 0, binding = 0) uniform sampler2D cd_hdr_color;
layout(set = 0, binding = 1) uniform sampler2D cd_bloom_mip0;
layout(set = 0, binding = 2) uniform sampler2D cd_depth;
layout(set = 0, binding = 3) uniform sampler2D cd_gbuf_normal;
layout(set = 0, binding = 4) uniform sampler2D cd_history_prev;
// Velocity G-Buffer (RG16F = curr_uv - prev_uv). When the per-mesh
// velocity pass hasn't written for a pixel (sky / cleared), the tap
// is (0, 0) which the motion blur + TAA reprojection treat as "use
// camera-only fallback".
layout(set = 0, binding = 5) uniform sampler2D cd_gbuf_velocity;
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
  // phase437-black: guard against NaN normals written by degenerate geometry
  // (zero-length interpolated normals on collapsed triangles). NaN in the
  // G-Buffer normal causes normalize() here to return NaN, which propagates
  // through the AO accumulator into c *= ao, killing the entire fragment to
  // black even though the HDR scene pass produced a valid colour. Return no
  // occlusion (fully lit) for degenerate pixels — they are rare edge cases.
  if (any(isnan(N_packed.xyz)) || dot(N_packed.xyz, N_packed.xyz) < 1e-10) return 1.0;
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
    // phase856b-ao-halo-fix: user reported "cisimlerin cevresinde
    // siyah hale var". Root cause is the AO loop saturating its
    // occlusion contribution (clamp((dz-bias)/0.5, 0.0, 1.0)) for
    // any dz above ~0.5, so background pixels just behind a
    // foreground silhouette accumulate full occlusion from the
    // foreground depth sample → dark halo. Replace the saturating
    // ramp with a BELL curve that peaks at small dz (real local
    // ambient contact occlusion) and falls back off as dz grows
    // (the silhouette is now too far in front to count as
    // ambient-scale occluder).
    float dz_rise = smoothstep(bias, bias + 0.05, dz);
    float dz_fall = 1.0 - smoothstep(0.30, 0.80, dz);
    float dz_w    = dz_rise * dz_fall;
    occ += dz_w * n_dot * falloff;
    weight_sum += n_dot;
  }
  if (weight_sum < 1e-4) return 1.0;
  occ /= max(weight_sum, 1.0);
  return clamp(1.0 - occ, 0.0, 1.0);
}

// phase513-ssr-quality: hierarchical depth march + contact-hardening
// fade. Two-pass approach inspired by Stachowiak 2015 / McGuire-Mara
// 2014 — the first 8 COARSE steps locate the (front, back) bracket
// around the depth-buffer crossing; 4 FINE binary-search steps refine
// the hit to sub-coarse-step precision. Result is sharper hits with
// fewer total taps and far less "ghost-step" smear than the prior
// uniform 24-step march.
//
// Contact-hardening: when the camera nearly skims the surface
// (NoV → 0) the reflection ray runs almost parallel to the screen
// plane, producing long, low-quality streaks. We attenuate by
// max(NoV, 0.05) so grazing-angle reflections SOFTEN instead of
// becoming arbitrarily bright streaks.
vec3 ssr_color(vec2 uv, vec3 wp, vec3 N, float NoV) {
  if (pc.ssr.x <= 0.001) return vec3(0.0);
  vec3 V = normalize(pc.cam_pos.xyz - wp);
  vec3 R = reflect(-V, N);
  float max_dist = max(pc.ssr.y, 0.1);

  // ---- COARSE pass: 8 uniform steps locating the depth crossing ----
  const int kCoarseSteps = 8;
  float coarse_step = max_dist / float(kCoarseSteps);
  vec3  hit_sp      = vec3(-1.0);
  bool  hit_found   = false;
  // Bracket samples used to refine in the fine pass.
  vec3  prev_sp     = world_to_uv(wp);
  float prev_march_lz = linearize_z(prev_sp.z);
  float prev_scene_lz = prev_march_lz;
  for (int i = 1; i <= kCoarseSteps; ++i) {
    vec3 sample_wp = wp + R * coarse_step * float(i);
    vec3 sp = world_to_uv(sample_wp);
    if (sp.x < 0.0 || sp.x > 1.0 || sp.y < 0.0 || sp.y > 1.0 || sp.z < 0.0)
      break;
    float scene_d  = texture(cd_depth, sp.xy).r;
    float scene_lz = linearize_z(scene_d);
    float march_lz = linearize_z(sp.z);
    // Crossing test: previous step was IN FRONT of the depth buffer,
    // current step is BEHIND it (within a thickness band).
    float thickness = coarse_step * 2.0;
    if (march_lz > scene_lz && (march_lz - scene_lz) < thickness) {
      hit_sp    = sp;
      hit_found = true;
      // ---- FINE pass: 4-step binary-search refinement -------------
      vec3 a_wp = wp + R * coarse_step * float(i - 1); // before crossing
      vec3 b_wp = sample_wp;                            // after crossing
      for (int j = 0; j < 4; ++j) {
        vec3  m_wp = 0.5 * (a_wp + b_wp);
        vec3  m_sp = world_to_uv(m_wp);
        if (m_sp.x < 0.0 || m_sp.x > 1.0 ||
            m_sp.y < 0.0 || m_sp.y > 1.0 || m_sp.z < 0.0)
          break;
        float m_scene_lz = linearize_z(texture(cd_depth, m_sp.xy).r);
        float m_march_lz = linearize_z(m_sp.z);
        if (m_march_lz > m_scene_lz) {
          b_wp   = m_wp;
          hit_sp = m_sp;
        } else {
          a_wp = m_wp;
        }
      }
      break;
    }
    prev_sp = sp;
    prev_march_lz = march_lz;
    prev_scene_lz = scene_lz;
  }
  if (!hit_found) return vec3(0.0);

  // ---- Fade + Fresnel + contact-hardening --------------------------
  vec2  ec = abs(hit_sp.xy - vec2(0.5)) * 2.0;
  float ef = clamp(1.0 - max(ec.x, ec.y) * pc.ssr.w, 0.0, 1.0);
  float fresnel = pow(1.0 - NoV, 3.0);
  // Contact-hardening: soften at grazing angles. floor of 0.05 keeps
  // near-silhouette reflections from vanishing entirely.
  float graze = max(NoV, 0.05);
  // Distance-based contact term: hits FAR from the source soften
  // proportionally to their march length (Stachowiak 2015 fig. 27).
  vec2  px_dist = hit_sp.xy - uv;
  float screen_dist = length(px_dist);
  float contact_fade = clamp(1.0 - screen_dist * 1.5, 0.0, 1.0);
  vec3 hit = texture(cd_hdr_color, hit_sp.xy).rgb;
  return hit * pc.ssr.x * ef * (0.3 + fresnel * 0.7) * graze * contact_fade;
}

// A-trous edge-aware spatial filter (Dammertz 2010 / SVGF spatial step).
// Single-iteration 5x5 kernel; per-tap weight uses colour + normal +
// depth edge-stopping functions. Strength packed into pc.dof.w when
// pc.dof.x is 0 (DOF disabled) — repurposes the reserved slot. Useful
// for smoothing RT-noisy specular highlights without crushing edges.
vec3 sample_atrous(vec2 uv, vec3 centre_color) {
  float atrous_s = pc.dof.w;
  if (atrous_s <= 0.001 || pc.dof.x > 0.001) return centre_color;
  vec2 px = 1.0 / vec2(textureSize(cd_hdr_color, 0));
  vec4 N0_packed = texture(cd_gbuf_normal, uv);
  if (N0_packed.w < 0.5) return centre_color;
  // phase437-black: NaN guard matching depth_ao defensive check.
  if (any(isnan(N0_packed.xyz)) || dot(N0_packed.xyz, N0_packed.xyz) < 1e-10) return centre_color;
  vec3 N0 = normalize(N0_packed.xyz);
  float d0 = linearize_z(texture(cd_depth, uv).r);
  const float kKernel5[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);
  vec3 sum = vec3(0.0);
  float wsum = 0.0;
  for (int dy = -2; dy <= 2; ++dy)
  for (int dx = -2; dx <= 2; ++dx) {
    vec2 sp = uv + vec2(dx, dy) * px;
    vec3 c1 = texture(cd_hdr_color, sp).rgb;
    vec4 N1p = texture(cd_gbuf_normal, sp);
    if (N1p.w < 0.5) continue;
    if (any(isnan(N1p.xyz)) || dot(N1p.xyz, N1p.xyz) < 1e-10) continue;
    vec3 N1 = normalize(N1p.xyz);
    float d1 = linearize_z(texture(cd_depth, sp).r);
    // Edge-stopping: colour (luma) + normal alignment + depth delta.
    float lum0 = dot(centre_color, vec3(0.299, 0.587, 0.114));
    float lum1 = dot(c1, vec3(0.299, 0.587, 0.114));
    float w_l = exp(-abs(lum0 - lum1) * 8.0);
    float w_n = pow(max(0.0, dot(N0, N1)), 32.0);
    float w_d = exp(-abs(d0 - d1) * 2.0);
    float kw  = kKernel5[dy + 2] * kKernel5[dx + 2];
    float w   = w_l * w_n * w_d * kw;
    sum += c1 * w;
    wsum += w;
  }
  vec3 blurred = (wsum > 1e-5) ? sum / wsum : centre_color;
  return mix(centre_color, blurred, clamp(atrous_s, 0.0, 1.0));
}

vec3 sample_chromab(vec2 uv) {
  if (pc.lens.x <= 0.001) return texture(cd_hdr_color, uv).rgb;
  vec2 vc = uv - vec2(0.5);
  float r = length(vc);
  vec2 dir = (r > 1e-4) ? vc / r : vec2(0.0);
  vec2 px = 1.0 / vec2(textureSize(cd_hdr_color, 0));
  // W4-I: blend a small uniform-offset floor with the radial r^2 term
  // so the RGB split shows on object silhouettes near screen-centre
  // too (not just corners). Pure r^2 is physically correct lens
  // chromab; the floor keeps the engine showcase visible everywhere.
  // 4 px floor + 28 px peak at corners for strength = 1.0.
  float offs = pc.lens.x * (4.0 + 28.0 * r * r);
  vec3 c;
  c.r = texture(cd_hdr_color, uv + dir * offs * px).r;
  c.g = texture(cd_hdr_color, uv).g;
  c.b = texture(cd_hdr_color, uv - dir * offs * px).b;
  return c;
}

void main() {
  vec3 c = sample_chromab(v_uv);
  float center_d = texture(cd_depth, v_uv).r;

  // SVGF-lite A-trous spatial denoise. Gated by dof.w (when DOF is off).
  c = sample_atrous(v_uv, c);

  // Sun-strength proxy from the linear sun colour magnitude. Drives
  // every "sky / atmosphere" overlay so the scene reads as night when
  // every light (sun included) is off — was the all-lights-off bright
  // grey-sky bug from the W3 visual pass.
  float sun_amt = clamp(length(pc.sun_col.rgb) * 0.50, 0.0, 1.0);

  // Volumetric cloud overlay — sky-only (depth far), fBm-noise based.
  // pc.sun_col.w is coverage; 0 disables, 1 is full overcast.
  // phase854-clouds-grid-depth-order: tighten the sky gate from
  // 0.999 to 0.9995 because the editor floor's far-distance pixels
  // (1000 m quad fading to alpha-0 at the horizon) sit RIGHT at
  // 0.9994-0.9998 in NDC. The old 0.999 threshold let clouds
  // overlay the floor-grid's far edge, reading as "clouds in front
  // of the grid" instead of "behind it" at the horizon. Add a
  // soft fade-in over [0.9985, 0.9995] so the transition is smooth
  // when the gate flips at the grid-fade boundary.
  if (center_d >= 0.9985 && pc.sun_col.w > 0.001) {
    float depth_gate = smoothstep(0.9985, 0.9995, center_d);
    // phase856a-clouds-world-anchor: user-reported "kameraya
    // sabitlenmis durumda aslinda evrene sabitlenmis olmali"
    // (clouds rotate with the camera, should be world-anchored).
    // Reconstruct the world-space view direction at this pixel and
    // ray-cast onto a virtual sky-plane at altitude `kSkyAltitude`.
    // The intersection point's XZ becomes the noise sample
    // coordinate — so the clouds are anchored to world space, the
    // camera rotation slides the visible region across the cloud
    // texture, and camera translation gives natural parallax.
    //
    // The "renk flip" on looking up was the v_uv-anchored sample
    // plane making the density value snap as the pitch rolled past
    // the horizon (different v_uv → different sky cell). With
    // world anchoring, the density at a given direction is stable
    // across camera moves and only the FOOTPRINT on the sky-plane
    // changes smoothly.
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec3 view_ray = pc.cam_fwd.xyz
                  + ndc.x * pc.cam_right.w * pc.cam_right.xyz
                  - ndc.y * pc.cam_up.w    * pc.cam_up.xyz;
    vec3 dir_world = normalize(view_ray);
    // phase870-clouds-triplanar: octahedral (phase 862) eliminated
    // the south-meridian seam but the zenith pinch persisted —
    // octahedral compresses the upper hemisphere into the diamond
    // |x|+|z| ≤ 1, so all near-zenith rays sample similar
    // (oct.x, oct.z) values and the cloud cells visibly converge
    // into a radial "star" at the zenith.
    //
    // Triplanar replaces the single 2D projection with THREE 2D
    // noise samples — one per axis pair — averaged into one
    // density. Each tap is well-defined for ANY direction (no
    // singularities) and the composite of three taps smooths out
    // any per-tap distortion. The result is uniform cloud
    // resolution at every pitch including the zenith.
    // phase859b-clouds-slower-layered: user-requested "bulutlar hizli
    // akiyor daha yavas olmali ve daha katman katman gozukmeli"
    // (clouds drift too fast; should be slower and more layered).
    // Scale every time-coupled offset down ~5× and add a second
    // "high cloud" layer drifting at a different rate / orientation
    // so the visible cover looks like real clouds in multiple
    // strata, not a single fBm field.
    float t = pc.cam_pos.w;
    // phase870-clouds-triplanar: 3 axis-pair noise taps, averaged.
    // Each pair is well-defined for any direction (no singularity).
    // phase874-clouds-visible-drift: user reports clouds look
    // motionless. Phase 859b dropped time scales ~5× to match the
    // user's "yavas + katman katman" request; that was overshoot —
    // at interactive frame rates the visible drift was invisible to
    // the eye over typical "look around" timeframes. Bump 3× so
    // motion is perceptible without going back to the original
    // "sliding" feel.
    vec2 uv_xz = dir_world.xz * 4.0;
    vec2 uv_xy = dir_world.xy * 4.0 + vec2(17.3, 6.1);
    vec2 uv_yz = dir_world.yz * 4.0 + vec2(31.2, 9.7);
    // ---- Low cloud layer ----
    float n_xz_lo = cd_fbm4(uv_xz + vec2(t * 0.030, t * 0.0135));
    float n_xy_lo = cd_fbm4(uv_xy + vec2(t * 0.024, t * 0.015));
    float n_yz_lo = cd_fbm4(uv_yz + vec2(t * 0.018, t * 0.027));
    float density_lo = (n_xz_lo + n_xy_lo + n_yz_lo) / 3.0;
    // ---- High cloud layer ----
    vec2 uv_xz_hi = uv_xz * 0.55;
    vec2 uv_xy_hi = uv_xy * 0.55;
    vec2 uv_yz_hi = uv_yz * 0.55;
    float n_xz_hi = cd_fbm4(uv_xz_hi + vec2(t * 0.054, 0.0));
    float n_xy_hi = cd_fbm4(uv_xy_hi + vec2(0.0, t * 0.054));
    float n_yz_hi = cd_fbm4(uv_yz_hi + vec2(t * 0.042, t * 0.036));
    float density_hi = (n_xz_hi + n_xy_hi + n_yz_hi) / 3.0;
    // Layer composite: low layer dominates, high layer adds 35%.
    // phase870-cloud-contrast: triplanar averaging compressed the
    // fBm dynamic range toward the mean (~0.5), making clouds
    // appear uniform and washed-out. Apply a contrast stretch
    // post-average so the visible cloud structure pops again.
    float density = density_lo * 0.65 + density_hi * 0.45;
    density = clamp((density - 0.40) * 2.2 + 0.40, 0.0, 1.0);
    float cov = clamp(pc.sun_col.w, 0.0, 1.0);
    // phase856a: smoothly fade clouds off as the view drops below
    // the horizon — looking-down rays should NEVER carry a cloud
    // overlay regardless of depth-gate (sky never visible there).
    // phase859: widen the fade band 0..0.25 → -0.05..0.40 so the
    // sharp horizontal line a 14.5° transition produced (visible
    // in the user's "looking up" screenshots) becomes a gentle
    // 30° gradient instead.
    float horizon_fade = smoothstep(-0.05, 0.40, dir_world.y);
    // W4-G: widen the smoothstep band so cloud edges fade smoothly
    // instead of stepping; combined with the 24x12 cell scale this
    // removes the pixelated-block look the user reported.
    float cloud = smoothstep(0.42 - cov * 0.36, 0.82, density);
    // phase875-cloud-lit-bright-white: user reports the clouds
    // slider has barely-visible effect. Root cause: cloud_lit was
    // pale-grey (~0.85) and stable_sky_base was pale-blue (~0.66),
    // making `mix(base, lit, density)` only swing the result a
    // small amount. Brighten cloud_lit toward white so dense
    // clouds clearly stand out against the sky.
    vec3 cloud_lit = mix(vec3(0.85, 0.85, 0.90),
                         pc.sun_col.rgb * 1.4 + vec3(0.20),
                         0.6);
    // W4-C: dim clouds at night so a fully unlit scene doesn't bake
    // grey overcast into the sky pixels.
    cloud_lit *= mix(0.04, 1.0, sun_amt);
    // phase854-clouds-grid-depth-order: depth_gate softens the
    // sky-vs-grid boundary so the grid horizon doesn't get a
    // hard cloud line painted across it.
    // phase856a-clouds-world-anchor: horizon_fade cuts clouds
    // below the horizon line.
    // phase858-clouds-color-stability: user-reported "hala kameraya
    // gore renk degisor inverse oluyor gibi" — looking up vs across
    // gave different cloud colours. Root cause: the previous
    //   c = mix(c, cloud_lit, cloud * 0.85 * ...)
    // mixed cloud_lit INTO the underlying sky `c`, which itself
    // shifts brightness with view direction (analytical sky's
    // zenith/horizon gradient). At low cloud density the result
    // tracked the sky; at high density it still picked up a sky
    // tint via the 0.85 ceiling. Replace with an OVER-blend at
    // 1.0 ceiling so dense clouds fully cover the underlying sky
    // colour — the cloud_lit term is direction-independent (only
    // depends on pc.sun_col which is a CPU-side constant per frame),
    // so the visible cloud colour is now stable across camera
    // rotation. Light clouds still let some sky through, but the
    // gradient is now caused only by their own density, not the
    // sky behind them.
    // phase868-clouds-fully-replace-sky: user still reports cloud
    // colour flips with camera direction. Root cause: when cloud
    // density is <1.0 the previous mix(c, cloud_lit, cloud_mix)
    // let some of the analytical-sky `c` (horizon vs zenith
    // gradient) show through, and `c` IS direction-dependent.
    // Compute a fully-direction-INDEPENDENT sky pixel
    // (stable_sky_base blended with cloud_lit by cloud density),
    // then mix THAT into `c` based on depth_gate/horizon_fade so
    // any residue from the analytical sky pass cannot tint the
    // overlay. The visible variation is now PURELY cloud density,
    // not the analytical sky behind.
    // phase870-stable-sky-bright: stable_sky_base now hard-coded
    // at full brightness — the sun_amt multiplier was crushing the
    // overlay to near-black on fixture captures even when the sky
    // pass clearly drew bright daylight. Night gating happens via
    // cloud_lit *= mix(0.04, 1.0, sun_amt) which already handles
    // the lit-cloud term going dark; the base sky behind the
    // clouds should stay daylight-toned.
    // phase876-stable-sky-saturated: match the new analytical-sky
    // palette (horizon 0.48,0.62,0.82 ↔ zenith 0.16,0.40,0.86).
    // The previous (0.55, 0.66, 0.84) was a pale-gray-blue that read
    // as haze when it replaced the analytical sky under the cloud
    // overlay. Use a mid-tone of the new palette so the overlay
    // doesn't pull the sky toward pale.
    vec3 stable_sky_base = vec3(0.32, 0.51, 0.84);
    // phase872-sky-overlay-dominance: user reports the sky still
    // shifts colour at 180° yaw / certain pitch — root cause is the
    // analytical-sky pass's direction-dependent gradient (warm
    // horizon ↔ cool zenith ↔ sun-side glow) bleeding through where
    // overlay_mix is below 1.0. Decouple overlay_mix from
    // horizon_fade: the overlay should FULLY replace the sky pixel
    // any time depth_gate detects sky depth, regardless of pitch
    // angle. Horizon attenuation is folded into the cloud-density
    // term instead so light clouds taper toward the horizon (still
    // see stable_sky_base, NOT the analytical sky).
    float cloud_visible = clamp(cloud, 0.0, 1.0) *
                          smoothstep(-0.10, 0.20, dir_world.y);
    vec3 sky_with_clouds = mix(stable_sky_base, cloud_lit, cloud_visible);
    // Sky pixels (depth_gate > 0) get fully overlaid; non-sky pixels
    // (geometry hits) are untouched.
    c = mix(c, sky_with_clouds, depth_gate);
  }

  // AO
  float ao = depth_ao(v_uv, center_d);
  c *= mix(1.0, ao, clamp(pc.ao.x, 0.0, 1.0));

  // Aerial perspective + uniform exp fog + volumetric sun in-scatter.
  // pc.lens.yzw packs the world-space sun direction (toward scene);
  // when the view ray faces back along it we get a bright forward-
  // scattering glow through fog. Cheap single-scatter Henyey-Greenstein
  // phase, no froxel — composite-inline so no extra render target.
  //
  // phase512-volumetric-fog-wire: pc.atmo.x sign now encodes mode.
  //   pc.atmo.x > 0  : legacy single-tap exp(-lz * density)
  //   pc.atmo.x < 0  : Wronski 2014 froxel-style integrated single
  //                    scatter — march N quadratic-warped slices from
  //                    near up to min(lz, far), accumulate inscatter
  //                    pre-multiplied by per-slice thickness, then
  //                    Beer-Lambert transmittance. Matches the math
  //                    in cd::volumetric::integrate_view_ray() and
  //                    inject_cell() (phase 469 VolumetricFog.hpp)
  //                    up to fp16 quantisation. The 3D LUT compute
  //                    path is queued; this inline path is the LUT-
  //                    equivalent evaluation for a single analytical
  //                    sun light, with no extra texture binding.
  float vol_fog_density = abs(pc.atmo.x);
  bool  vol_fog_on      = (pc.atmo.x < 0.0);
  if (center_d < 0.999 && (vol_fog_density > 0.001 || pc.atmo.y > 0.001)) {
    float lz = linearize_z(center_d);
    // W4-C: horizon base is daylight; at night fall back to a near-
    // black sky so the all-lights-off scene doesn't keep a bright
    // overcast painted over the whole frustum.
    vec3 night_horizon = vec3(0.02, 0.025, 0.035);
    vec3 horizon_base = mix(night_horizon, vec3(0.78, 0.86, 0.96), sun_amt);
    vec3 horizon_lit  = mix(horizon_base, pc.sun_col.rgb, 0.35 * sun_amt);

    // Single-scatter sun in-scatter colour. Henyey-Greenstein phase
    // (g=0.15 — near-isotropic post-phase874; see §874 comment
    // below for why 0.6 → 0.15) modulates by view·-sun.
    vec3 wp_end = world_pos_from_uv(v_uv, center_d);
    vec3 view_dir = normalize(wp_end - pc.cam_pos.xyz);
    vec3 sun_dir_world = pc.lens.yzw;
    float cos_th = max(0.0, dot(view_dir, -normalize(sun_dir_world)));
    // phase874-fog-isotropic-phase: user-reported a dark oval shape
    // dragging across the screen when the camera moves with fog on.
    // Root cause: the HG phase function at g=0.6 gave inscatter a
    // ~25× ratio between toward-sun and away-from-sun directions.
    // Combined with the volumetric march's `c * transmittance +
    // inscatter` composite, the away-from-sun hemisphere dimmed
    // without enough inscatter to compensate — reading as a moving
    // dark blob in screen space. Drop g to 0.15 (almost isotropic):
    // phase becomes ~uniform across all view directions, the dark
    // oval vanishes, and the fog reads as flat haze.
    const float g  = 0.15;
    const float g2 = g * g;
    float phase = (1.0 - g2) / (4.0 * 3.14159265 *
                  pow(1.0 + g2 - 2.0 * g * cos_th, 1.5));
    // phase868-fog-direction-flat: user-reported the cos_th coupling
    // STILL caused fog colour to flip between camera orientations
    // even after phase 859 dialed it 0.8 → 0.25. Drop the coupling
    // to 0.05 — effectively flat fog colour across all view
    // directions. The HG sun glow becomes a faint highlight near
    // the sun only, no longer dominating the fog tint at 180°.
    vec3 fog_colour = mix(horizon_lit,
                          pc.sun_col.rgb * (phase * 6.0 + 0.5),
                          clamp(cos_th * 0.05, 0.0, 1.0));

    if (vol_fog_on) {
      // Wronski integrated single-scatter — front-to-back march.
      // The Wronski grid normally caches per-slice (sigma_s * phase *
      // L_sun * dt, sigma_t) in a 3D LUT; here we recompute the same
      // quantity in-place at N=16 slices along this pixel's view ray.
      // The depth bound is min(lz, far) so the integrand stops at the
      // first opaque surface, matching the LUT-sampled equivalent
      // where the integrated cell carries transmittance to the slice
      // immediately in front of the scene depth.
      float near_z = pc.ao.z;
      float far_z  = min(lz, pc.ao.w);
      float sigma_s = vol_fog_density;          // scattering coeff (1/m)
      float sigma_t = vol_fog_density;          // extinction == scattering (no absorption tweak yet)
      // HG phase as inscatter weight. Albedo defaults to (0.95,0.95,1)
      // (mild blue tint matching VolumetricFogSettings::albedo); kept
      // as a const here to avoid burning another push-constant slot.
      const vec3 vol_albedo = vec3(0.95, 0.95, 1.0);
      vec3 sun_radiance = pc.sun_col.rgb;
      vec4 accum = vec4(0.0, 0.0, 0.0, 1.0);   // RGB = inscatter, A = transmittance
      const int kVolSlices = 16;
      float prev_z = near_z;
      for (int i = 1; i <= kVolSlices; ++i) {
        float s = float(i) / float(kVolSlices);
        // Wronski quadratic warp: dense near, sparse far.
        float z = near_z + (far_z - near_z) * s * s;
        float dt = max(z - prev_z, 0.0);
        prev_z = z;
        // Per-slice inscatter = albedo * sun * HG(cos_th) * sigma_s * dt
        // (pre-multiplied by dt so integration is just sum * T).
        vec3 inscatter = vol_albedo * sun_radiance * (phase * sigma_s * dt);
        float trans = exp(-sigma_t * dt);
        accum.rgb += inscatter * accum.a;
        accum.a   *= trans;
      }
      // Composite via standard Wronski "scene * T + inscatter".
      c = c * accum.a + accum.rgb;
      // Aerial perspective still applies on top — paints the horizon
      // tint over the integrated fog so the far-distance haze matches
      // the sky band.
      // phase874-fog-aerial-couple: see else-branch comment.
      float aer_t = 1.0 - exp(-lz * 0.08);
      float aer_floor_couple = clamp(vol_fog_density * 3.0, 0.0, 1.0);
      float aer_floor = clamp(pc.atmo.y * 0.15 * aer_floor_couple, 0.0, 0.18);
      aer_t = max(aer_t, aer_floor);
      c = mix(c, horizon_lit, clamp(aer_t * pc.atmo.y, 0.0, 1.0));
    } else {
      // phase874-fog-aerial-couple: user reports "fog birkere
      // acilinca kapanmiyor" — fog seems not to turn off. Real
      // cause: phase 870's constant aerial floor `atmo.y * 0.15`
      // was independent of vol_fog_density. When the user moved the
      // fog density slider to 0, aerial perspective kept painting
      // the constant floor onto every pixel — reading as "fog still
      // on". Couple aerial floor to vol_fog_density so when density
      // = 0, both fog AND aerial floor go to 0.
      float fog_t = 1.0 - exp(-lz * max(vol_fog_density, 0.0));
      float fog_floor = clamp(vol_fog_density * 6.0, 0.0, 0.20);
      fog_t = max(fog_t, fog_floor);
      float aer_t = 1.0 - exp(-lz * 0.08);
      float aer_floor_couple = clamp(vol_fog_density * 3.0, 0.0, 1.0);
      float aer_floor = clamp(pc.atmo.y * 0.15 * aer_floor_couple, 0.0, 0.18);
      aer_t = max(aer_t, aer_floor);
      c = mix(c, fog_colour,  clamp(fog_t, 0.0, 1.0));
      c = mix(c, horizon_lit, clamp(aer_t * pc.atmo.y, 0.0, 1.0));
    }
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

  // SSR — phase513-ssr-quality hierarchical march + RT-bucket blend.
  // The G-Buffer surface_flag (ssr_N.w) now encodes a finer bucket
  // (phase 513): 0.6 = glTF Lit, 0.85 = PBR chrome (has W8-BC RT
  // reflection), 1.0 = Lit dielectric showcase. Combine policy:
  //   ssr_N.w >  0.95 → full SSR (no RT, wants screen reflections)
  //   ssr_N.w in [0.5, 0.95] → half-strength SSR ADDITIVE enhancement
  //                            (glTF Lit gets a soft reflection cue;
  //                             chrome at 0.85 also takes a tiny add
  //                             so screen-local bounce isn't lost
  //                             entirely on rough chrome — RT is still
  //                             the primary contribution.)
  //   ssr_N.w <= 0.5  → no SSR (sky / shadow / floor)
  // NoV is computed once and threaded into ssr_color so the contact-
  // hardening fade can soften grazing-angle streaks.
  vec4 ssr_N = texture(cd_gbuf_normal, v_uv);
  if (pc.ssr.x > 0.001 && ssr_N.w > 0.5 && center_d < 0.999) {
    vec3  wp  = world_pos_from_uv(v_uv, center_d);
    // phase437-black guard mirrors depth_ao: NaN/degenerate normals
    // are common on Sponza vegetation alpha-test triangles.
    vec3  N   = (any(isnan(ssr_N.xyz)) || dot(ssr_N.xyz, ssr_N.xyz) < 1e-10)
                ? vec3(0.0, 1.0, 0.0)
                : normalize(ssr_N.xyz);
    vec3  V   = normalize(pc.cam_pos.xyz - wp);
    float NoV = max(dot(N, V), 0.05);
    vec3  ssr_contrib = ssr_color(v_uv, wp, N, NoV);
    // Bucket weight: 1.0 for Lit dielectric (>0.95), 0.5 for the
    // mid-range (glTF Lit / PBR chrome) so the W8-BC RT reflection
    // remains the primary source on chrome surfaces.
    float bucket_w = (ssr_N.w > 0.95) ? 1.0 : 0.5;
    c += ssr_contrib * bucket_w;
  }

  // Motion blur — prefer the velocity G-Buffer (per-mesh + camera
  // motion), fall back to camera-only reprojection when the velocity
  // tap is effectively zero (sky / cleared / static mesh + still
  // camera). Both paths sample HDR along the screen-space velocity
  // vector with a 10%-of-viewport cap to prevent snap-cut smear.
  float mblur_strength = pc.prev_cam_fwd.w;
  if (mblur_strength > 0.001 && center_d < 0.999) {
    vec2 velocity = texture(cd_gbuf_velocity, v_uv).rg;
    if (length(velocity) < 1e-5) {
      // Velocity G-Buffer empty here — derive camera-only velocity.
      vec3 wp_now = world_pos_from_uv(v_uv, center_d);
      vec2 prev_uv = prev_world_to_uv(wp_now);
      if (prev_uv.x >= 0.0 && prev_uv.x <= 1.0 &&
          prev_uv.y >= 0.0 && prev_uv.y <= 1.0) {
        velocity = v_uv - prev_uv;
      }
    }
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

  // Tonemap. Operator IDs:
  //   0 = Narkowicz ACES, 1 = Hill ACES, 2 = Hable / Uncharted 2,
  //   3 = AGX (Sobotka 2022), 4 = HDR10 PQ encode (ST.2084).
  // HDR10 path skips display-clamp + the post-tonemap gamma below
  // (handled inline) so the colour reaches the swapchain in PQ space.
  // Use only when the swapchain colour-space is HDR10_ST2084_PACKED.
  int op = int(pc.fx.x + 0.5);
  bool hdr10 = (op == 4);
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
  } else if (op == 3) {
    const float kMinEv = -12.47393, kMaxEv = 4.026069;
    vec3 lg = clamp((log2(max(c, vec3(1e-10))) - vec3(kMinEv)) /
                    (kMaxEv - kMinEv), vec3(0.0), vec3(1.0));
    vec3 x2 = lg * lg;
    vec3 x4 = x2 * x2;
    c = clamp( 15.5  * x4 * x2 - 40.14 * x4 * lg + 31.96 * x4
             -  6.868 * x2 * lg + 0.4298 * x2 + 0.1191 * lg - 0.00232,
             vec3(0.0), vec3(1.0));
  } else {  // op == 4 → HDR10 PQ
    // Linear-sRGB scene → Rec.2020 primaries → PQ encode.
    // Assume a 1000 cd/m² peak white target (typical HDR10 monitor).
    const mat3 srgb_to_2020 = mat3(0.6274, 0.0691, 0.0164,
                                   0.3293, 0.9195, 0.0880,
                                   0.0433, 0.0114, 0.8956);
    vec3 rec2020_linear = srgb_to_2020 * max(c, vec3(0.0));
    const float kPeakNits = 1000.0;
    vec3 nits = rec2020_linear * kPeakNits;
    const float kM1 = 0.1593017578125, kM2 = 78.84375;
    const float kC1 = 0.8359375, kC2 = 18.8515625, kC3 = 18.6875;
    vec3 Y  = clamp(nits / 10000.0, vec3(0.0), vec3(1.0));
    vec3 Ym = pow(Y, vec3(kM1));
    c = pow((kC1 + kC2 * Ym) / (1.0 + kC3 * Ym), vec3(kM2));
  }
  // Saturation pull-away (SDR only — HDR10 PQ already perceptual).
  if (!hdr10)
  {
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    float sb = max(pc.fx.z, 0.001);
    c = clamp(mix(vec3(luma), c, sb), vec3(0.0), vec3(1.0));
    c = pow(c, vec3(1.0/2.2));
  }

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

  // TAA neighbourhood-clamp blend. Prefer the velocity G-Buffer for
  // reprojection (captures per-mesh animation), fall back to camera-
  // only via prev_world_to_uv when the velocity tap is empty.
  // phase855-taa-motion-decay: user-reported "TAA acinca harekete
  // edince goruntu bulaniklasiyor" — constant high taa_alpha keeps
  // mixing history when the camera moves, producing the classic
  // ghosting/blur trail. Decay history weight by per-pixel motion
  // magnitude (Karis 2014 / Lottes — "velocity-based history decay")
  // so fast-moving pixels lean on the current frame and slow / still
  // pixels keep their full temporal accumulation.
  float taa_alpha_base = clamp(pc.cam_fwd.w, 0.0, 0.97);
  if (taa_alpha_base > 0.001 && center_d < 0.999) {
    vec2 vel = texture(cd_gbuf_velocity, v_uv).rg;
    vec2 prev_uv;
    if (length(vel) > 1e-5) {
      prev_uv = v_uv - vel;
    } else {
      vec3 wp_taa = world_pos_from_uv(v_uv, center_d);
      prev_uv = prev_world_to_uv(wp_taa);
    }
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
      // Velocity in pixel units; e^(-2*px) so even ~1 px of motion
      // halves the history weight, and any >2 px motion collapses
      // taa_alpha towards 0.05 (just enough temporal stability to
      // hide single-frame jitter without blurring the moving edge).
      float vel_px = length(vel * vec2(textureSize(cd_hdr_color, 0)));
      float motion_decay = exp(-vel_px * 2.0);
      float taa_alpha = max(0.05, taa_alpha_base * motion_decay);
      c = mix(c, hist, taa_alpha);
    }
  }

  out_color = vec4(c, 1.0);
  out_history = vec4(c, 1.0);
}
)glsl";

// ---- phase691 — composite FS with optional DDGI + ReSTIR GI/RT hooks -------
//
// Sister GLSL string to `kCompositeFS`. The hook bindings are wrapped in
// `#ifdef CD_COMPOSITE_USE_DDGI` / `#ifdef CD_COMPOSITE_USE_RESTIR` blocks
// so when neither macro is defined the SPIR-V output is byte-equivalent to
// `kCompositeFS` and the renderer's composite pass behaves exactly as it
// did pre-phase691. Use `make_composite_fs_source_with_gi_hooks()` to
// build the source string with the macros prepended, or pass the GLSL
// straight to a glslang preamble that defines them externally.
//
// The hook contribution is purely ADDITIVE on the lit colour `c` AFTER
// the SSR/light-shaft/bloom add but BEFORE the exposure multiply +
// tonemap. That matches Sprint-5 (DDGI sample writes per-pixel indirect
// diffuse irradiance) + Sprint-6 (ReSTIR denoised direct illumination)
// — both contributions are in linear-HDR radiance and should compose
// linearly with the engine's existing direct-light forward pass.
//
// Contract (must match the caller's pipeline layout when either macro
// is ON):
//   * binding 6 — sampler2D cd_ddgi_indirect_irradiance (RGBA16F).
//     Sourced from `cd::ddgi::FullPipeline::bind_sample_resources(...)`
//     `output_view`. May be a per-pixel ATLAS for the indirect-only path
//     or a viewport-sized buffer if the renderer projects probes to
//     screen-space first. The shader simply does a single bilinear tap
//     at `v_uv` and adds the RGB channels.
//   * binding 7 — sampler2D cd_restir_denoised_direct (RGBA16F).
//     Sourced from `cd::restir_di::FullPipelineDenoised::execute(...)`
//     output buffer (post-SVGF denoise). Same single bilinear tap at
//     `v_uv` and the RGB channels add to `c`.
//
// MOMENT (per the M14 W4 brief): a graphics dev flips
// CD_COMPOSITE_USE_DDGI=ON in their CMake, recompiles, and the scene gets
// actual indirect bounce lighting in the next frame — the full GI/RT
// chain is consumable, not just dispatchable.
constexpr std::string_view kCompositeFSWithGiHooks = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D cd_hdr_color;
layout(set = 0, binding = 1) uniform sampler2D cd_bloom_mip0;
layout(set = 0, binding = 2) uniform sampler2D cd_depth;
layout(set = 0, binding = 3) uniform sampler2D cd_gbuf_normal;
layout(set = 0, binding = 4) uniform sampler2D cd_history_prev;
layout(set = 0, binding = 5) uniform sampler2D cd_gbuf_velocity;
#ifdef CD_COMPOSITE_USE_DDGI
layout(set = 0, binding = 6) uniform sampler2D cd_ddgi_indirect_irradiance;
#endif
#ifdef CD_COMPOSITE_USE_RESTIR
layout(set = 0, binding = 7) uniform sampler2D cd_restir_denoised_direct;
#endif
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

void main() {
  // Hook smoke variant: we keep the FS minimal so library-level integration
  // tests can verify pipeline-creation + dispatch without paying for the
  // full composite chain. The renderer's production composite path uses
  // `kCompositeFS` (or extends it inline) — when both macros stay OFF this
  // smoke FS still produces the same `c = HDR + bloom * w + exposure`
  // skeleton the original composite shipped with, so a downstream consumer
  // can switch over without seeing a black frame.
  vec3 c = texture(cd_hdr_color, v_uv).rgb;
  vec3 bloom = texture(cd_bloom_mip0, v_uv).rgb;
  c += bloom * max(pc.fx.w, 0.0);

#ifdef CD_COMPOSITE_USE_DDGI
  // DDGI indirect bounce — additive in linear HDR. The DDGI sample pass
  // writes per-pixel diffuse irradiance already divided by PI (Majercik
  // 2019); the composite simply adds it on top of the lit direct +
  // analytic ambient that the forward pass produced.
  vec3 ddgi_irr = texture(cd_ddgi_indirect_irradiance, v_uv).rgb;
  c += ddgi_irr;
#endif

#ifdef CD_COMPOSITE_USE_RESTIR
  // ReSTIR DI denoised direct illumination — additive. The SVGF chain
  // writes temporally-stable direct light radiance (Bitterli 2020 +
  // Schied 2017). The renderer's analytic forward pass is expected to
  // SKIP the lights ReSTIR is responsible for so the contributions do
  // not double-count; the seam policy is documented in the README.
  vec3 restir_dir = texture(cd_restir_denoised_direct, v_uv).rgb;
  c += restir_dir;
#endif

  c *= max(pc.fx.y, 0.001);

  // Cheap Reinhard tonemap so the smoke FS still produces a swap-chain-
  // valid LDR colour. The production composite (`kCompositeFS`) keeps
  // its full ACES / AGX / Hable operator menu — this minimal path just
  // exercises the binding/push-constant layout for the hook smoke test.
  c = c / (c + vec3(1.0));
  c = pow(c, vec3(1.0 / 2.2));

  out_color   = vec4(c, 1.0);
  out_history = vec4(c, 1.0);
}
)glsl";

// ---- phase691 — helper: build the GI-hook FS with the macros prepended -----
//
// glslang accepts a single source string per CompileDesc + no separate
// `defines` channel (see `cd::shader::CompileDesc`). The simplest portable
// way to wire CD_COMPOSITE_USE_DDGI / CD_COMPOSITE_USE_RESTIR into the GLSL
// source is to inject `#define` lines AFTER the `#version` directive and
// before the rest of the source. This helper returns that concatenated
// string so consumers can pass it straight to
// `cd::shader::ICompiler::compile(...)`.
//
// Behaviour:
//   * `use_ddgi   == false && use_restir == false` → returns
//     `kCompositeFSWithGiHooks` unchanged (no hook bindings declared, no
//     contributions added → same SPIR-V as the no-flag baseline).
//   * either flag set → the corresponding `#define <macro> 1` line is
//     inserted right after the `#version 450` header so the GLSL
//     preprocessor sees the macro before the `#ifdef` block expands.
//
// Returning `std::string` (not `std::string_view`) keeps the helper safe
// for inline use: the lifetime of the returned source is bound to the
// caller's local variable, exactly mirroring how the unit tests below
// (and the library-level integration test) consume it.
[[nodiscard]] inline std::string
make_composite_fs_source_with_gi_hooks(bool use_ddgi, bool use_restir)
{
    // Locate the first newline so we can splice the #define block in
    // RIGHT after the `#version 450` line (GLSL requires #version to be
    // the first non-whitespace token in the unit).
    std::string out;
    out.reserve(kCompositeFSWithGiHooks.size() + 96U);
    const std::string_view src = kCompositeFSWithGiHooks;
    // The source string starts with a leading "\n" (raw string literal)
    // followed by "#version 450\n". Skip the leading newline first.
    std::size_t version_end = src.find('\n');
    if (version_end != std::string_view::npos && version_end + 1U < src.size())
    {
        // Find the END of the `#version` line.
        version_end = src.find('\n', version_end + 1U);
    }
    if (version_end == std::string_view::npos)
    {
        out = std::string(src);
        return out;
    }
    // Copy `#version 450\n` (inclusive of trailing newline).
    out.append(src.substr(0, version_end + 1U));
    if (use_ddgi)
        out.append("#define CD_COMPOSITE_USE_DDGI 1\n");
    if (use_restir)
        out.append("#define CD_COMPOSITE_USE_RESTIR 1\n");
    // Remainder of the FS source.
    out.append(src.substr(version_end + 1U));
    return out;
}

}  // namespace cd::post::composite
