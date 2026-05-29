// =============================================================================
// CHROMODYNAMIC -- cd/editor/panel/CompositePresets.hpp
// ADR-005 namespace + ADR-016 D1 replace-ready surface.
//
// Composite-pass post-fx preset data.  4 named presets package the
// 16-field knob bundle that the R3 Composite UI exposes: exposure,
// bloom, AO, DOF, light shafts, SSR, motion blur, TAA, clouds, fog,
// aerial perspective, chromatic aberration, film grain, vignette,
// saturation boost, tonemap operator.
//
// Each preset field is std::optional<T>.  An engaged value means
// "this preset assigns the field on apply"; an empty value means
// "leave the field as the consumer had it before".  This mirrors the
// hello_engine W6-E button semantics where the HDR Demo button does
// NOT touch dof / motion_blur / taa / aerial / film_grain, but the
// Cinematic + Performance buttons reset every field.
//
// Pure CPU + standard library; no ImGui, no RHI.  Safe to include
// from any TU and regression-testable without a graphics device.
//
// Marathon Run 23 phase N5A: extracted from hello_engine main.cpp
// draw_r_showcase_panel into a real library so editor binaries can
// drive the same post-fx surface without copying the preset table.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

namespace cd::editor::panel
{

// ---- Tonemap operator enum ------------------------------------------------
// Matches the integer code in the composite pass uniform tonemap_op:
//   0 = Narkowicz ACES (cheapest), 1 = Hill ACES, 2 = Hable, 3 = AGX.
enum class TonemapOp : std::int32_t
{
    kNarkowicz = 0,
    kHillAces  = 1,
    kHable     = 2,
    kAgx       = 3,
};

// ---- CompositePreset ------------------------------------------------------
// Flat bundle of optional knob values.  Engaged optional means apply()
// writes the value to the bound field.  Empty optional means apply()
// leaves the consumer field unchanged.
//
// Each field maps to a single GPU uniform / push-constant scalar in
// the composite pass.  See engine/render/post_composite/include/cd/
// post_composite/CompositePush.hpp for the GPU-side struct.
struct CompositePreset
{
    std::optional<float>     exposure {};
    std::optional<float>     saturation_boost {};
    std::optional<float>     bloom_post {};
    std::optional<float>     ao_strength {};
    std::optional<float>     dof_strength {};
    std::optional<float>     shafts_strength {};
    std::optional<float>     ssr_strength {};
    std::optional<float>     motion_blur {};
    std::optional<float>     taa_amount {};
    std::optional<float>     clouds_coverage {};
    std::optional<float>     fog_density {};
    std::optional<float>     aerial_perspective {};
    std::optional<float>     chromab_strength {};
    std::optional<float>     film_grain {};
    std::optional<float>     vignette_strength {};
    std::optional<TonemapOp> tonemap_op {};
};

// ---- Named preset table ---------------------------------------------------
// 4 presets lifted verbatim from the W6-E button block in
// draw_r_showcase_panel (samples/engine/hello_engine/main.cpp lines
// 1979-2054 in Run 22).
//
// Field engagement mirrors the source assignments: Cinematic +
// Performance fully populate every knob; Defaults intentionally
// skips tonemap_op (W6-E does not override the operator on reset);
// HDR Demo skips dof / ssr / motion_blur / taa / aerial / film_grain
// (those are not part of the wide-DR showcase intent).

/// Conservative defaults; matches the engine boot state.  Does NOT
/// touch tonemap_op -- the user tonemap survives the reset.
inline const CompositePreset kDefaults {
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
};

/// Filmic preset: deeper saturation, DOF on, atmospheric fog dialed
/// up.  Hable tonemap.  Touches every field.
inline const CompositePreset kCinematic {
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

/// All post-fx off.  Cheapest tonemap (Narkowicz).  Touches every
/// field so the A/B vs Cinematic is symmetric.
inline const CompositePreset kPerformance {
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

/// AGX tonemap + wide DR knobs.  Intentionally does NOT touch
/// dof_strength / ssr_strength / motion_blur / taa_amount /
/// aerial_perspective / film_grain -- those are not part of the HDR
/// showcase intent and the user-set values survive.
inline const CompositePreset kHdrDemo {
    .exposure            = 1.0F,
    .saturation_boost    = 1.40F,
    .bloom_post          = 0.12F,
    .ao_strength         = 0.55F,
    .shafts_strength     = 0.90F,
    .clouds_coverage     = 0.30F,
    .fog_density         = 0.0F,
    .chromab_strength    = 0.15F,
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

/// Number of preset slots in the table.  Keep this in sync with
/// PresetId so the button loop and the table index stay aligned.
inline constexpr int kPresetCount = 4;

/// Resolve a PresetId to the matching preset struct.
[[nodiscard]] inline const CompositePreset& preset_by_id(PresetId id) noexcept
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

/// Human-readable label for a preset.  Used as the ImGui button
/// caption + the log line emitted on click.
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

}  // namespace cd::editor::panel
