// =============================================================================
// HelloEngineFx.hpp
// -----------------------------------------------------------------------------
// hello_engine-local POD aggregate for the realism-roadmap (R1..R8) post-fx
// + tonemap + debug-view-mode state. Lifted out of main() in Marathon Run 9
// phase 304 (N3-prep) so the R-Showcase ImGui panel + composite-pass plumbing
// no longer have to thread ~28 individual references through their call
// signatures.
//
// This is INTENTIONALLY a flat aggregate — no invariants, no methods, no
// observer pattern. Composite passes read fields, UI sliders write fields.
// Default values match the visual tunings settled on in Marathon Runs
// 5..8 (HDR-IBL + Hable tonemap + sun-shaft 0.75, etc.); changing any
// default here changes the boot-time look of the sample, so coordinate
// with the docs/STATUS_AND_PLAN_W8.md visual baseline.
//
// Scope rule: this struct only carries SHOWCASE-PANEL-VISIBLE state. It
// deliberately does NOT carry:
//   - cd::post_*::Settings (those still live next to their library APIs)
//   - TAA history ResourceState[2] (per-frame transition tracking)
//   - PrevCamBasis (motion-blur reprojection feedback)
//   - prev_vp_unjittered (velocity-pass feedback)
// Those remain as locals in main() because they are frame-loop feedback
// state, not user-tweakable knobs.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace cd_sample {

struct HelloEngineFx
{
    // ---- R-Showcase composite knobs (live in composite pass) ----
    // phase450-exp: exposure 3.0 -> 1.0. The 3x default was crushing the
    // top of the tonemap curve, so Sponza walls + lit dielectric objects
    // saturated to near-white and lost their sun-direction shading.
    // 1.0 = no boost (raw HDR -> tonemap). Lit objects now sit in the
    // tonemap's mid range where shadow contrast + directional shading
    // are visible. User can still slider it up via the FX UI for
    // night/dim scenes that need a boost. Proper auto-exposure is the
    // architectural fix; queued.
    // phase857-fx-default-polish: user-requested "senin actigin
    // ayarlari default yapabilir miyiz" — bump first-boot FX so the
    // engine looks polished without the user having to enable
    // anything in the palette. Each new value is justified inline so
    // a future tuning pass can roll back any single one without
    // guessing what was intentional.
    float exposure { 1.0F };          // pre-tonemap exposure boost (was 3.0)
    float saturation_boost { 1.20F }; // post-tonemap saturation pull-away (was 1.50)
    // phase857: bumped 0.02 → 0.035 — small bump so bright highlights
    // (chrome reflections, sun-lit Sponza wall) read as glowing, not
    // flat. Still far below the 0.04 over-tuned regime.
    float bloom_post { 0.035F };      // bloom mip0 contribution mixed into HDR
    float ao_strength { 0.55F };      // composite AO crease darkening
    float dof_strength { 0.0F };      // wired to composite (phase 207)
    float shafts_strength { 0.75F };  // light shafts radial intensity
    float ssr_strength { 0.5F };      // SSR reflection contribution (default on)
    // phase857: motion_blur 0.0 → 0.20 — subtle per-pixel velocity
    // smear gives cinematic continuity to free-flight. Combined with
    // phase 855 TAA motion decay, the blur stops at the silhouette
    // instead of ghosting the whole moving object.
    float motion_blur { 0.20F };      // composite camera-velocity (phase 215)
    // phase857: taa_amount 0.0 → 0.85 — TAA on by default because
    // phase 855 added velocity-based history decay, so the camera-
    // movement blur user reported is no longer an issue and the
    // anti-aliasing benefit is significant on static frames.
    float taa_amount { 0.85F };       // composite TAA ping-pong (phase 216-217)
    // phase876-clouds-coverage-dropped: 0.45 default meant the cloud
    // overlay always fired at boot, painting stable_sky_base on top
    // of the analytical sky and hiding the new (phase 876) saturated
    // blue horizon. Drop to 0.20 — clouds visible as light wisps,
    // but the analytical sky tone shows through dominantly.
    float clouds_coverage { 0.20F };  // sky overlay (phase 853 fBm)
    // phase875-fog-off-by-default: user reports persistent haze even
    // with the volumetric-fog checkbox off and clouds slider at 0.
    // Root: aerial_perspective default 0.10 + fog floor were
    // painting the sky. The fog slider is now the canonical opt-in,
    // and that requires it to start at 0.
    float fog_density { 0.0F };
    // phase512-volumetric-fog-wire: when true, composite uses the
    // Wronski 2014 integrated single-scatter path (16 quadratic-warped
    // slices, Beer-Lambert transmittance, HG phase) instead of the
    // legacy single-tap exp(-lz * density). Same UI slider drives
    // density; main.cpp signs cp.atmo[0] negative when this is set
    // to pass the mode flag without expanding the 256-B push layout.
    bool volumetric_fog_on { false };
    // phase875-aerial-off-by-default: the previous 0.10 default
    // painted a persistent gray haze on every distant pixel, which
    // the user perceived as "fog still on" even after turning fog
    // density to 0. Aerial perspective is now opt-in (slider in
    // R-Showcase panel) — the cinematic first-boot look prioritises
    // scene contrast over atmospheric depth.
    float aerial_perspective { 0.0F };
    // phase857: chromab_strength 0.0 → 0.12 — barely-perceptible
    // radial RGB split adds lens character without distracting from
    // edges. The pc.lens.x * (4 + 28*r²) formula keeps the floor at
    // 4 px so the effect shows on object silhouettes near centre too.
    float chromab_strength { 0.12F }; // 0 = off; 0.5 = visible split
    // phase857: film_grain 0.0 → 0.08 — adds a low-frequency filmic
    // noise that hides banding in dark gradients (the gradient on the
    // Sponza sandstone walls used to show step banding at near-black).
    float film_grain { 0.08F };       // 0 = off; 0.5 = visible noise
    // phase857: vignette 0.25 → 0.32 — slightly more cinematic edge
    // without crushing the corners of the frame.
    float vignette_strength { 0.32F };// soft default — cinematic edge

    // ---- R-Showcase legacy / queued knobs ----
    float gtao_strength { 0.0F };     // legacy inline GTAO (deprecated)
    float bloom_strength { 0.0F };    // legacy inline bloom (deprecated)
    float smaa_strength { 0.0F };     // legacy inline SMAA (deprecated)
    float light_shafts { 0.0F };      // legacy var kept for compat
    float decal_count { 0.0F };       // count placeholder (queued v1.7)
    float particle_emit_rate { 0.0F };// /sec placeholder (queued v1.7)

    // ---- R6 advanced BRDFs (queued v1.7 material rework) ----
    float ltc_ggx_strength { 0.0F };
    float sheen_strength { 0.0F };
    float clearcoat_strength { 0.0F };
    float sss_strength { 0.0F };

    // ---- Debug view modes ----
    // 0 final, 1 albedo, 2 world normal, 3 MR map, 4 AO,
    // 5 normal-mapped surface normal, 6 vertex UVs.
    int view_mode { 0 };

    // ---- Tonemap operator selector ----
    // 0=Narkowicz 1=Hill 2=Hable 3=AGX 4=HDR10 PQ (Sobotka 2022 / ST.2084).
    // Default Hable preserves tints on the sample's LDR-range shading
    // (AGX desaturates that range; AGX wins on HDR-heavy frames — switch
    // via the palette / R3 Composite preset buttons).
    int tonemap_op { 2 };

    // ---- R-Showcase HDR / GI toggles ----
    bool hdr10_request { false };  // queued for swapchain-output rework
    bool restir_di_on { false };
    bool restir_gi_on { false };
    bool ddgi_on { false };
    bool nrc_on { false };
    // phase1005-3d-viewport-ddgi-probe-debug: when true, hello_engine
    // renders one small sphere per DDGI probe at its world position
    // (per docs/RESEARCH_3D_VIEWPORT_DEBUG_VIZ.md §2 Tier-1 #2).
    // Probes are colour-coded by their grid index hash so the user
    // can SEE the probe layout in the 3D scene, not just in the
    // R-Showcase R4 GI panel's ImGui plot. Toggled live from the
    // R4 GI panel.
    bool ddgi_show_probes_3d { false };
    // phase1006-3d-viewport-frustum-cull-debug: when true,
    // hello_engine renders a sphere at each cluster AABB centre in
    // a synthetic 3x3x3 grid centred on the origin. Spheres are
    // colour-coded by the cull result returned by
    // cd::camera::test_aabb against the current camera frustum:
    //   kInside       -> green
    //   kIntersecting -> yellow
    //   kOutside      -> red
    // Toggled live from the R-Showcase R4 GI / Frustum panel
    // (per docs/RESEARCH_3D_VIEWPORT_DEBUG_VIZ.md §3 Tier-1 #3).
    // True wireframe AABB edges are deferred until a line-list
    // pipeline + dedicated debug line renderer lib (§12 Tier-2)
    // lands; the centre-sphere proxy is the fastest visible-quality
    // path with zero new pipelines.
    bool frustum_cull_show_aabbs_3d { false };
    // phase1007-3d-viewport-light-position-gizmo: when true, every
    // non-directional light in the scene is rendered as a sphere at
    // its world position, tinted by the light's CCT-derived colour
    // (so a 2700 K warm-tungsten lantern reads orange and a 25 000 K
    // cool-zenith fill reads blue). Directional lights are skipped
    // (they have no position). Toggled live from R-Showcase R4 GI
    // panel. Per docs/RESEARCH_3D_VIEWPORT_DEBUG_VIZ.md §10 Tier-2
    // (area light polygon visualisation), centre-sphere proxy first;
    // full polygon outline once the line renderer ships.
    bool lights_show_gizmos_3d { false };
    // phase1008-3d-viewport-decal-obb-gizmo: shared state for the
    // "Run25 Decal Projector Probe" panel and the 3D viewport decal-
    // OBB overlay. The panel writes these via sliders; the render
    // loop reads them to materialise a cd::decal::Decal each frame
    // (axis-aligned defaults; rotation extension queued — see
    // docs/RESEARCH_3D_VIEWPORT_DEBUG_VIZ.md §11). When
    // decal_show_obb_3d is on, the render loop emits 1 sphere at
    // the centre + 8 spheres at the OBB corners, tinted by the
    // cd::decal::decal_intersects_aabb result against the fixed
    // scene AABB [-1, 1]^3 (same probe the panel reports textually).
    // Plain std::array<float,3> instead of cd::math::Vec3f keeps
    // this header zero-dependency (the fx_defaults gtest target does
    // not link cd::math).
    std::array<float, 3> decal_demo_position    { 0.0F, 0.0F, 0.0F };
    std::array<float, 3> decal_demo_half_extents { 0.5F, 0.5F, 0.5F };
    std::array<float, 3> decal_demo_test_point  { 0.2F, 0.1F, 0.1F };
    bool decal_show_obb_3d { false };
    // phase1010-3d-viewport-csm-cascade-depth: when true, hello_engine
    // renders 4 spheres along the camera view-direction at the centre
    // depth of each CSM cascade computed via
    // cd::light::practical_split_distances. Cascade index sets the
    // tint (0=red, 1=yellow, 2=green, 3=blue, matching the textbook
    // SDSM debug palette). Lets the user SEE how the cascade splits
    // distribute along view depth, which is invisible from the atlas
    // bitmap view alone.
    bool csm_show_cascade_depth_3d { false };
    // phase1011-3d-viewport-cluster-density-heatmap: when true,
    // hello_engine renders a sphere at the centre of each cell in
    // an 8x4x8 world-space grid (spacing 2 m), tinted by the number
    // of scene lights (s.lights) whose position+range sphere covers
    // the cell centre. The dim/green/yellow/red palette mirrors the
    // production cluster shading heat-map (Olsson 2012 + DOOM 2016
    // talks) so the user can SEE which regions of the scene the
    // clustered-deferred light culling would consider hot.
    bool cluster_show_density_3d { false };
    // phase1012-3d-viewport-cubic-bezier-curve: 4 control points
    // (std::array<float,3> keeps the header zero-dep) for the
    // "Run25 Cubic Bezier Probe" panel. The 3D viz emits 4 control-
    // point spheres (large, white) plus 32 curve-sample spheres
    // (small, magenta) so the user sees the curve shape evolve
    // live in the 3D scene as they drag the sliders -- not just the
    // x/y/z PlotLines in the panel.
    std::array<float, 3> bezier_p0 { -1.5F, 0.5F, -1.0F };
    std::array<float, 3> bezier_p1 { -0.5F, 2.5F, -1.0F };
    std::array<float, 3> bezier_p2 {  0.5F, 2.5F,  1.0F };
    std::array<float, 3> bezier_p3 {  1.5F, 0.5F,  1.0F };
    bool bezier_show_curve_3d { false };
    // phase1013-3d-viewport-gpu-particles: toggle for the live
    // particle simulation overlay. The render loop maintains its
    // own 64-particle pool (independent from the panel's "Step 1
    // dt" probe) and drives cd::gpu_particles::advance +
    // compact_alive every frame so the user sees particles emit,
    // arc under gravity, and die in the 3D scene.
    bool gpu_particles_show_3d { false };
    // phase1019-3d-viewport-quat-slerp-triad: shared state for the
    // "Run25 Quaternion Slerp Probe" panel and its 3D overlay. The
    // overlay renders an orientation TRIAD (3 spheres at the rotated
    // X/Y/Z axis tips, tinted red/green/blue) for slerp(A, B, t),
    // plus a 16-sample great-circle path of the X-axis tip from A to
    // B so the user SEES the spherical interpolation arc — the same
    // path the animation runtime walks for skin-pose blending.
    // (std::array keeps the header zero-dep; x,y,z,w order.)
    std::array<float, 4> quat_slerp_a { 1.0F, 0.0F, 0.0F, 0.0F };
    std::array<float, 4> quat_slerp_b { 0.7071F, 0.0F, 0.0F, 0.7071F };
    float quat_slerp_t { 0.5F };
    bool quat_slerp_show_3d { false };
    // phase1021-3d-viewport-motion-vector: shared state for the
    // "Run25 Motion Vector Probe" panel and its 3D overlay. The
    // overlay maps the clip-space (x, y) plane onto a fixed 2x2 m
    // world panel above the origin and renders the prev position
    // (red sphere), curr position (green sphere) and 8 interpolated
    // samples between them (small fading spheres) so the user SEES
    // the screen-space motion vector as an arrow — the same delta
    // TAA reprojection + per-object motion blur consume per pixel.
    // (x, y, z, w; w stays 1 for the demo.)
    std::array<float, 4> mvec_prev { 0.0F, 0.0F, 0.0F, 1.0F };
    std::array<float, 4> mvec_curr { 0.05F, 0.02F, 0.0F, 1.0F };
    bool mvec_show_3d { false };
    // phase1022-3d-viewport-cubemap-globe: shared state for the
    // "Run25 IBL Cubemap Sample Probe" panel and its 3D overlay. The
    // overlay renders a GLOBE of ~50 small spheres around a fixed
    // anchor, each tinted by sample_cubemap_dir() along its own
    // direction — the whole cubemap content becomes visible as a 3D
    // ball (blue top = zenith, warm band = horizon). The user's
    // sample-direction slider is shown as one larger sphere riding
    // the same globe, so "which direction am I sampling" is obvious.
    std::array<float, 3> cubemap_sample_dir { 0.0F, 1.0F, 0.0F };
    bool cubemap_show_globe_3d { false };
    // phase1023-3d-viewport-vg-lod-frontier: shared state for the
    // "Run25 Virtual Geometry LOD Probe" panel and its 3D overlay.
    // The overlay rebuilds the same synthetic 4-node DAG, runs
    // cd::virtual_geometry::pick_clusters with THESE values, and
    // renders each node's bounds sphere at a fixed anchor — bright
    // green when the node is in the picked LOD frontier, dim grey
    // when culled/refined away. A white marker shows the virtual
    // camera distance. Dragging the sliders makes nodes light up /
    // drop out live (Karis 2021 Nanite-style picker).
    float vg_lod_cam_z { 6.0F };
    float vg_lod_threshold { 4.0F };
    int   vg_lod_vp_h { 720 };
    bool  vg_show_lod_3d { false };
    // phase1024-3d-viewport-noise-heightfield: shared state for the
    // "Run25 Texture-Synth Noise Probe" panel and its 3D overlay.
    // The overlay renders a 16x16 sphere carpet where each sphere's
    // HEIGHT and BRIGHTNESS encode fbm2_quintic_6oct(u, v, freq) —
    // the 2D noise texture becomes a watchable terrain patch.
    // Dragging the frequency slider reshapes the terrain live (same
    // quintic kernel the phase-853 cloud shader uses).
    int  noise_freq { 8 };
    bool noise_show_field_3d { false };
    // phase1025-3d-viewport-restir-reservoir: shared state for the
    // ReSTIR DI live demo and its 3D overlay. The overlay re-runs the
    // SAME deterministic WRS stream (same seed + sample budget) and
    // renders the 8 light candidates as a sphere row — brightness =
    // candidate radiance (log-scaled), radius = how often that
    // candidate was streamed, and a large warm sphere floats above
    // the WRS survivor. Dragging seed / budget shows the estimator
    // re-picking live.
    int           restir_samples { 32 };
    std::uint32_t restir_seed { 0xC0FFEEU };
    bool          restir_show_3d { false };
    // phase1026-3d-viewport-earth-globe: 3D overlay for the Earth
    // procedural section of the Texture-Synth panel. Renders a globe
    // of ~98 small spheres, each tinted by the SAME fbm2 + pole
    // falloff + land/sea/snow colour ramp the panel evaluates for a
    // single (u, v) — the whole procedural planet becomes visible
    // (green-brown continents, blue oceans, white polar caps) instead
    // of one colour swatch.
    bool earth_show_globe_3d { false };
    // phase1027-3d-viewport-camera-basis: shared state for the
    // "Run25 Camera Basis Probe" panel and its 3D overlay. The
    // overlay renders the derived free-look basis as a triad at the
    // probe camera position: red = right, green = up, blue = forward
    // (+ a white sphere at the position itself). Dragging yaw/pitch/
    // position swings the triad live — the same basis math
    // FreeLookController feeds the view matrix.
    float camera_basis_yaw_deg { 0.0F };
    float camera_basis_pitch_deg { 0.0F };
    std::array<float, 3> camera_basis_pos { 0.0F, 1.5F, 4.0F };
    bool camera_basis_show_3d { false };
    // phase1028-3d-viewport-cct-sweep: shared state for the "Run25
    // Light CCT Probe" panel and its 3D overlay. The overlay renders
    // a 16-sphere rail sweeping 1500 K -> 15000 K through
    // cd::light::cct_to_linear_rgb (firelight orange -> noon white ->
    // sky-shade blue) with a larger marker sphere riding the rail at
    // the user's current Kelvin slider position.
    float cct_kelvin { 6500.0F };
    bool  cct_show_sweep_3d { false };
    // phase1029-3d-viewport-attenuation-rail: shared state for the
    // "Run25 Light Attenuation Probe" panel and its 3D overlay. The
    // overlay renders a warm "light" marker plus a 20-sphere rail
    // marching away from it; each sphere's brightness encodes
    // cd::light::distance_attenuation(d, range) (Frostbite 2014
    // windowed inverse-square). Dragging range stretches the visible
    // falloff live; the sphere just past `range` going black makes
    // the window function's hard cutoff obvious.
    float atten_range { 8.0F };
    bool  atten_show_rail_3d { false };
    // phase1035-3d-viewport-phase-function-polar: shared state for
    // the "Run25 Atmosphere Phase Functions Probe" panel and its 3D
    // overlay. The overlay renders POLAR plots of the two scattering
    // phase functions as cd::debug_line polylines around a fixed
    // anchor: radius(theta) = normalised phase value. Warm curve =
    // Henyey-Greenstein at the panel's g (forward lobe stretches
    // toward +X as g -> 1); cool curve = Rayleigh (symmetric dumbbell).
    // A grey unit circle gives the isotropic reference.
    float atmo_phase_g { 0.8F };
    bool  atmo_show_polar_3d { false };
    // phase1036-3d-viewport-audio-waveform: shared state for the
    // "Run25 Audio Tone Synth Probe" panel and its 3D overlay. The
    // overlay renders ~2.5 cycles of the synthesized sine as a
    // cd::debug_line polyline ribbon at a fixed anchor (x = time,
    // y = sample value * amplitude) over a grey zero-axis line.
    // Dragging frequency compresses the ribbon, amplitude scales it
    // — the panel's RMS number becomes a visible wave.
    float audio_tone_hz { 440.0F };
    float audio_tone_amp { 0.5F };
    bool  audio_show_wave_3d { false };

    // phase1094-3d-viewport-nrc-mse: NRC demo MSE history, migrated off
    // the panel-local static so the pure-line overlay builder can read
    // it; toggle promotes the panel's 2D PlotLines into the viewport.
    bool nrc_show_mse_3d { false };
    std::vector<float> nrc_mse_history {};
    // phase1037-3d-viewport-rng-histogram: shared state for the
    // "Run25 Random Distribution Probe" panel and its 3D overlay.
    // The panel's Generate/Reset buttons fill these bins (PCG32
    // next_float into 32 uniform buckets); the overlay renders one
    // vertical line column per bin (height = count / max) plus a
    // grey reference line at the expected-uniform height, so the
    // histogram's convergence toward flat is watchable in 3D.
    std::array<std::uint32_t, 32> rng_hist {};
    std::uint64_t rng_total { 0 };
    bool rng_show_hist_3d { false };
    // phase1038-3d-viewport-brdf-lut-surface: shared state for the
    // "Run25 IBL BRDF Split-Sum LUT Probe" panel and its 3D overlay.
    // The overlay lazily bakes a small 16x16 LUT and renders the
    // SCALE channel as a wireframe height surface over the
    // (n.v, roughness) plane via cd::debug_line polylines, with a
    // warm marker cross at the panel's lookup point. The Karis
    // split-sum table stops being an abstract 2D texture and reads
    // as the "Fresnel-scale terrain" it actually is.
    float brdf_lut_nv { 0.7F };
    float brdf_lut_r  { 0.3F };
    bool  brdf_show_lut_3d { false };
    // phase1039-3d-viewport-brdf-lobes: shared state for the "Run25
    // Sheen + Clearcoat BRDF Probe" panel and its 3D overlay. The
    // overlay draws the two microfacet distributions as POLAR LOBES
    // above a surface line: for half-angle theta in [-90, 90] deg,
    // radius = normalised D(cos theta). Warm lobe = Charlie sheen D
    // (wide, flat-topped at high roughness); cool lobe = clearcoat
    // D*V (tight specular spike). The textbook BRDF lobe diagram,
    // standing in the scene, reshaping as the sliders move.
    float sc_roughness { 0.3F };
    float sc_nv { 0.7F };
    float sc_nl { 0.5F };
    bool  sc_show_lobes_3d { false };
    // phase1040-3d-viewport-sss-falloff: shared state for the "Run25
    // SSS BRDF Probe" panel and its 3D overlay. The overlay renders
    // the three per-channel Burley diffusion falloff curves as
    // red / green / blue polylines over a shared baseline (x = radius
    // in mm scaled to 3 m, y = R(r)). The channel SEPARATION — red
    // bleeding farther than green/blue, the whole reason skin glows
    // red at shadow edges — reads instantly when the three curves
    // overlay in one plot instead of three stacked panel rows.
    std::array<float, 3> sss_mfp { 0.6F, 0.3F, 0.2F };
    bool sss_show_falloff_3d { false };
    // phase1041-3d-viewport-shafts-ring: shared state for the "Run25
    // Light Shafts Inline Probe" panel and its 3D overlay. The
    // overlay renders the cone-alignment sweep as a polar ring
    // around the vertical sun axis: radius(azimuth) = inline shaft
    // intensity for a camera ray jittered toward that azimuth. The
    // ring bulges toward the azimuth where the camera direction
    // aligns against the sun — the same falloff the GLSL inline
    // fragment fallback applies when the ray-march budget is tight.
    std::array<float, 3> shafts_cam_dir { 0.0F, 0.0F, -1.0F };
    bool shafts_show_ring_3d { false };
    // phase1053-3d-viewport-vt-atlas: the VT probe's PageTable moved
    // from a panel function-static into EngineState so the 3D overlay
    // can enumerate residents() — the panel and the render loop talk
    // through these plain fields (panel writes the request + bumps
    // the serial; the loop applies it once per bump and mirrors the
    // results back; the panel displays them with a 1-frame lag,
    // fine for a debug probe).
    std::array<int, 3> vt_req { 0, 0, 0 };  // x, y, mip
    std::uint32_t vt_req_serial { 0 };
    std::uint32_t vt_resident_count { 0 };
    bool vt_lookup_found { false };
    std::uint32_t vt_lookup_slot_x { 0 };
    std::uint32_t vt_lookup_slot_y { 0 };
    bool vt_show_atlas_3d { false };
    // phase1054-3d-viewport-meshlets: shared state for the "Run25
    // Mesh Shader Meshlet Builder Probe" panel and its 3D overlay.
    // The overlay rebuilds the same synthetic strip + meshlets and
    // draws every triangle's edges tinted by a per-meshlet hash —
    // the Nanite-style cluster colour view (research doc Tier-1 #1,
    // originally blocked on the mesh-shader GPU path; trivial via
    // cd::debug_line).
    int  ms_tri_count { 100 };
    bool ms_show_meshlets_3d { false };
    // phase1056-3d-viewport-ecs-cloud: the ECS stress probe mirrors
    // its World::alive_count() here each frame; the overlay renders
    // one tiny cross per alive entity on a deterministic golden-angle
    // spiral disc (positions are synthetic — the POPULATION and its
    // growth/reset dynamics are the demo, not spatial data). Display
    // capped at 2048 crosses; the panel hint states the cap.
    std::uint32_t ecs_alive_mirror { 0 };
    bool ecs_show_cloud_3d { false };
};

}  // namespace cd_sample
