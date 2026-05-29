// =============================================================================
// CHROMODYNAMIC -- cd/editor_panel/CompositePresets.hpp
// ADR-005 namespace + ADR-016 D1 replace-ready surface.
//
// Composite-pass post-fx preset data.  4 named presets package the
// 16-field knob bundle that the R3 Composite UI exposes: exposure,
// bloom, AO, DOF, light shafts, SSR, motion blur, TAA, clouds, fog,
// aerial perspective, chromatic aberration, film grain, vignette,
// tonemap operator, plus a saturation boost multiplier.
//
// Pure POD data; no ImGui, no RHI, no rendering-side state.  This
// header is safe to include from any TU (engine, sample, tooling,
// editor binary) and can be regression-tested without a graphics
// device.
//
// W6-E semantics: the 4 named presets were validated against the
// hello_engine baseline in March 2026 (commit 9bf6106, phase344).
// Defaults / Cinematic / Performance / HDR Demo each describe a
// known-good visual setup.
//
// Marathon Run 23 phase N5A: extracted from hello_engine main.cpp
// draw_r_showcase_panel into a real library so editor binaries can
// drive the same post-fx surface without copying the preset table.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string_view>

namespace cd::editor_panel
{

// ---- Tonemap operator enum ------------------------------------------------
// Matches the integer code in the composite pass uniform "tonemap_op":
//   0 = Narkowicz ACES (cheapest), 1 = Hill ACES, 2 = Hable, 3 = AGX.
enum class TonemapOp : std::int32_t
{
    kNarkowicz = 0,
    kHillAces  = 1,
    kHable     = 2,
    kAgx       = 3,
};

// ---- CompositePreset ------------------------------------------------------
// Flat POD bundle of the 16 R3-Composite knob values.  Each field
// matches a single GPU uniform / push-constant scalar in the composite
// pass.  See engine/render/post_composite/include/cd/post_composite/
// CompositePush.hpp for the GPU-side struct.
struct CompositePreset
{
    float       exposure;            ///< linear scene exposure multiplier
    float       saturation_boost;    ///< post-tonemap saturation
    float       bloom_post;          ///< composite bloom mix
    float       ao_strength;         ///< composite GTAO mix
    float       dof_strength;        ///< 8-tap bokeh mix
    float       shafts_strength;     ///< Mitchell 16-tap god-ray mix
    float       ssr_strength;        ///< 24-step SSR ray-march mix
    float       motion_blur;         ///< camera-velocity motion blur mix
    float       taa_amount;          ///< history + Halton(2,3) blend
    float       clouds_coverage;     ///< composite fBm cloud overlay
    float       fog_density;         ///< height fog exponent
    float       aerial_perspective;  ///< sun-direction in-scatter
    float       chromab_strength;    ///< chromatic aberration radius
    float       film_grain;          ///< monochrome grain amount
    float       vignette_strength;   ///< radial darkening
    TonemapOp   tonemap_op;          ///< operator selector
};

// ---- Named preset table ---------------------------------------------------
// 4 constexpr presets, calibrated against the hello_engine baseline.
// Lifted verbatim from the W6-E button block in draw_r_showcase_panel
// (samples/engine/hello_engine/main.cpp lines 1979-2054 in Run 22).

inline constexpr CompositePreset kDefaults {
    .exposure            = 3.0F,
    .saturation_boost    = 1.50F,
    .bloom_post          = 0.04F,
    .ao_strength         = 0.55F,
    .dof_strength        = 0.0F,
    .shafts_strength     = 0.75F,
    .ssr_strength        = 0.5F,
    .motion_blur         = 0.0F,
    .taa_amount          = 0.0F,
    .clouds_coverage     = 0.0F,
    .fog_density         = 0.0F,
    .aerial_perspective  = 0.0F,
    .chromab_strength    = 0.0F,
    .film_grain          = 0.0F,
    .vignette_strength   = 0.25F,
    .tonemap_op          = TonemapOp::kHable,
};

inline constexpr CompositePreset kCinematic {
    .exposure            = 2.5F,
    .saturation_boost    = 1.65F,
    .bloom_post          = 0.08F,
    .ao_strength         = 0.65F,
    .dof_strength        = 0.35F,
    .shafts_strength     = 0.85F,
    .ssr_strength        = 0.55F,
    .motion_blur         = 0.30F,
    .taa_amount          = 0.80F,
    .clouds_coverage     = 0.45F,
    .fog_density         = 0.20F,
    .aerial_perspective  = 0.50F,
    .chromab_strength    = 0.25F,
    .film_grain          = 0.15F,
    .vignette_strength   = 0.40F,
    .tonemap_op          = TonemapOp::kHable,
};

inline constexpr CompositePreset kPerformance {
    .exposure            = 1.5F,
    .saturation_boost    = 1.20F,
    .bloom_post          = 0.0F,
    .ao_strength         = 0.0F,
    .dof_strength        = 0.0F,
    .shafts_strength     = 0.0F,
    .ssr_strength        = 0.0F,
    .motion_blur         = 0.0F,
    .taa_amount          = 0.0F,
    .clouds_coverage     = 0.0F,
    .fog_density         = 0.0F,
    .aerial_perspective  = 0.0F,
    .chromab_strength    = 0.0F,
    .film_grain          = 0.0F,
    .vignette_strength   = 0.0F,
    .tonemap_op          = TonemapOp::kNarkowicz,
};

inline constexpr CompositePreset kHdrDemo {
    .exposure            = 1.0F,
    .saturation_boost    = 1.40F,
    .bloom_post          = 0.12F,
    .ao_strength         = 0.55F,
    .dof_strength        = 0.0F,
    .shafts_strength     = 0.90F,
    .ssr_strength        = 0.5F,
    .motion_blur         = 0.0F,
    .taa_amount          = 0.0F,
    .clouds_coverage     = 0.30F,
    .fog_density         = 0.0F,
    .aerial_perspective  = 0.0F,
    .chromab_strength    = 0.15F,
    .film_grain          = 0.0F,
    .vignette_strength   = 0.30F,
    .tonemap_op          = TonemapOp::kAgx,
};

enum class PresetId : std::int32_t
{
    kDefaults    = 0,
    kCinematic   = 1,
    kPerformance = 2,
    kHdrDemo     = 3,
};

inline constexpr int kPresetCount = 4;

[[nodiscard]] inline constexpr const CompositePreset& preset_by_id(PresetId id) noexcept
{
    switch (id)
    {
        case PresetId::kDefaults:    return kDefaults;
        case PresetId::kCinematic:   return kCinematic;
        case PresetId::kPerformance: return kPerformance;
        case PresetId::kHdrDemo:     return kHdrDemo;
    }
    return kDefaults;
}

[[nodiscard]] inline constexpr std::string_view preset_label(PresetId id) noexcept
{
    switch (id)
    {
        case PresetId::kDefaults:    return "Defaults";
        case PresetId::kCinematic:   return "Cinematic";
        case PresetId::kPerformance: return "Performance";
        case PresetId::kHdrDemo:     return "HDR Demo";
    }
    return "Defaults";
}

}  // namespace cd::editor_panel
