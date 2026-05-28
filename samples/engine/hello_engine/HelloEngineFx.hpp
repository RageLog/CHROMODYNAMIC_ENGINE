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
    float exposure { 3.0F };          // pre-tonemap exposure boost
    float saturation_boost { 1.50F }; // post-tonemap saturation pull-away
    float bloom_post { 0.04F };       // bloom mip0 contribution mixed into HDR
    float ao_strength { 0.55F };      // composite AO crease darkening
    float dof_strength { 0.0F };      // wired to composite (phase 207)
    // W4-E: bumped default 0.35 -> 0.75 so light shafts are obviously visible
    // on first run. User reported they were hard to read at the previous
    // default.
    float shafts_strength { 0.75F };  // light shafts radial intensity
    float ssr_strength { 0.5F };      // SSR reflection contribution (default on)
    float motion_blur { 0.0F };       // composite camera-velocity (phase 215)
    float taa_amount { 0.0F };        // composite TAA ping-pong (phase 216-217)
    float clouds_coverage { 0.0F };   // queued — needs 3D Worley/Perlin noise tex
    float fog_density { 0.0F };
    float aerial_perspective { 0.0F };
    float chromab_strength { 0.0F };  // 0 = off; 0.5 = subtle radial RGB split
    float film_grain { 0.0F };        // 0 = off; 0.5 = visible filmic noise
    float vignette_strength { 0.25F };// soft default — readable cinematic edge

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
};

}  // namespace cd_sample
