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
};

}  // namespace cd_sample
