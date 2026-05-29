// =============================================================================
// CHROMODYNAMIC -- engine/ui/editor_panel/tests/test_composite_presets.cpp
//
// Unit test for the headless cd::editor_panel surface
// (CompositePresets.hpp + CompositeFxBinding.hpp).  No ImGui, no RHI.
//
// Marathon Run 23 phase N5A.
// =============================================================================
#include <cd/editor_panel/CompositeFxBinding.hpp>
#include <cd/editor_panel/CompositePresets.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace ep = cd::editor_panel;

// ---- Preset values round-trip through the table ---------------------------
TEST(CompositePresets, PresetByIdReturnsMatchingStruct)
{
    EXPECT_FLOAT_EQ(ep::preset_by_id(ep::PresetId::kDefaults).exposure,
                    ep::kDefaults.exposure);
    EXPECT_FLOAT_EQ(ep::preset_by_id(ep::PresetId::kCinematic).bloom_post,
                    ep::kCinematic.bloom_post);
    EXPECT_FLOAT_EQ(ep::preset_by_id(ep::PresetId::kPerformance).ao_strength,
                    0.0F);
    EXPECT_FLOAT_EQ(ep::preset_by_id(ep::PresetId::kHdrDemo).shafts_strength,
                    0.90F);
}

// ---- Labels are non-empty + stable ----------------------------------------
TEST(CompositePresets, LabelsArePresentForEveryId)
{
    EXPECT_EQ(ep::preset_label(ep::PresetId::kDefaults), "Defaults");
    EXPECT_EQ(ep::preset_label(ep::PresetId::kCinematic), "Cinematic");
    EXPECT_EQ(ep::preset_label(ep::PresetId::kPerformance), "Performance");
    EXPECT_EQ(ep::preset_label(ep::PresetId::kHdrDemo), "HDR Demo");
}

// ---- Defaults preset matches the W6-E baseline values ---------------------
// Guards against accidental edit to the preset table that would shift
// the hello_engine boot state.
TEST(CompositePresets, DefaultsMatchHelloEngineBaseline)
{
    EXPECT_FLOAT_EQ(ep::kDefaults.exposure, 3.0F);
    EXPECT_FLOAT_EQ(ep::kDefaults.saturation_boost, 1.50F);
    EXPECT_FLOAT_EQ(ep::kDefaults.ao_strength, 0.55F);
    EXPECT_FLOAT_EQ(ep::kDefaults.shafts_strength, 0.75F);
    EXPECT_FLOAT_EQ(ep::kDefaults.ssr_strength, 0.5F);
    EXPECT_EQ(ep::kDefaults.tonemap_op, ep::TonemapOp::kHable);
}

// ---- Cinematic preset matches the W6-E baseline values --------------------
TEST(CompositePresets, CinematicMatchesBaseline)
{
    EXPECT_FLOAT_EQ(ep::kCinematic.exposure, 2.5F);
    EXPECT_FLOAT_EQ(ep::kCinematic.dof_strength, 0.35F);
    EXPECT_FLOAT_EQ(ep::kCinematic.taa_amount, 0.80F);
    EXPECT_FLOAT_EQ(ep::kCinematic.fog_density, 0.20F);
    EXPECT_FLOAT_EQ(ep::kCinematic.vignette_strength, 0.40F);
    EXPECT_EQ(ep::kCinematic.tonemap_op, ep::TonemapOp::kHable);
}

// ---- Performance preset: every effect off ---------------------------------
TEST(CompositePresets, PerformanceClearsAllEffects)
{
    EXPECT_FLOAT_EQ(ep::kPerformance.bloom_post, 0.0F);
    EXPECT_FLOAT_EQ(ep::kPerformance.ao_strength, 0.0F);
    EXPECT_FLOAT_EQ(ep::kPerformance.dof_strength, 0.0F);
    EXPECT_FLOAT_EQ(ep::kPerformance.shafts_strength, 0.0F);
    EXPECT_FLOAT_EQ(ep::kPerformance.motion_blur, 0.0F);
    EXPECT_FLOAT_EQ(ep::kPerformance.taa_amount, 0.0F);
    EXPECT_FLOAT_EQ(ep::kPerformance.clouds_coverage, 0.0F);
    EXPECT_FLOAT_EQ(ep::kPerformance.fog_density, 0.0F);
    EXPECT_FLOAT_EQ(ep::kPerformance.vignette_strength, 0.0F);
    EXPECT_EQ(ep::kPerformance.tonemap_op, ep::TonemapOp::kNarkowicz);
}

// ---- HDR Demo preset: AGX + wide-DR knobs ---------------------------------
TEST(CompositePresets, HdrDemoSelectsAgxTonemap)
{
    EXPECT_FLOAT_EQ(ep::kHdrDemo.exposure, 1.0F);
    EXPECT_FLOAT_EQ(ep::kHdrDemo.bloom_post, 0.12F);
    EXPECT_FLOAT_EQ(ep::kHdrDemo.shafts_strength, 0.90F);
    EXPECT_FLOAT_EQ(ep::kHdrDemo.clouds_coverage, 0.30F);
    EXPECT_FLOAT_EQ(ep::kHdrDemo.chromab_strength, 0.15F);
    EXPECT_EQ(ep::kHdrDemo.tonemap_op, ep::TonemapOp::kAgx);
}

// ---- apply(): full binding round-trips the preset values -----------------
TEST(CompositeFxBinding, ApplyFullyBoundPresetRoundTripsAllFields)
{
    float exposure { 0 };
    float sat { 0 };
    float bloom { 0 };
    float ao { 0 };
    float dof { 0 };
    float shafts { 0 };
    float ssr { 0 };
    float mblur { 0 };
    float taa { 0 };
    float clouds { 0 };
    float fog { 0 };
    float aerial { 0 };
    float chromab { 0 };
    float grain { 0 };
    float vign { 0 };
    std::int32_t tonemap { -1 };

    const ep::CompositeFxBinding b {
        .exposure           = &exposure,
        .saturation_boost   = &sat,
        .bloom_post         = &bloom,
        .ao_strength        = &ao,
        .dof_strength       = &dof,
        .shafts_strength    = &shafts,
        .ssr_strength       = &ssr,
        .motion_blur        = &mblur,
        .taa_amount         = &taa,
        .clouds_coverage    = &clouds,
        .fog_density        = &fog,
        .aerial_perspective = &aerial,
        .chromab_strength   = &chromab,
        .film_grain         = &grain,
        .vignette_strength  = &vign,
        .tonemap_op         = &tonemap,
    };

    ep::apply(b, ep::kCinematic);

    EXPECT_FLOAT_EQ(exposure, 2.5F);
    EXPECT_FLOAT_EQ(sat, 1.65F);
    EXPECT_FLOAT_EQ(bloom, 0.08F);
    EXPECT_FLOAT_EQ(ao, 0.65F);
    EXPECT_FLOAT_EQ(dof, 0.35F);
    EXPECT_FLOAT_EQ(shafts, 0.85F);
    EXPECT_FLOAT_EQ(ssr, 0.55F);
    EXPECT_FLOAT_EQ(mblur, 0.30F);
    EXPECT_FLOAT_EQ(taa, 0.80F);
    EXPECT_FLOAT_EQ(clouds, 0.45F);
    EXPECT_FLOAT_EQ(fog, 0.20F);
    EXPECT_FLOAT_EQ(aerial, 0.50F);
    EXPECT_FLOAT_EQ(chromab, 0.25F);
    EXPECT_FLOAT_EQ(grain, 0.15F);
    EXPECT_FLOAT_EQ(vign, 0.40F);
    EXPECT_EQ(tonemap, static_cast<std::int32_t>(ep::TonemapOp::kHable));
}

// ---- apply(): nulled slots are skipped without crashing ------------------
TEST(CompositeFxBinding, ApplyPartialBindingSkipsNullPointers)
{
    float exposure { 99.0F };
    float bloom { 99.0F };
    std::int32_t tonemap { -1 };

    const ep::CompositeFxBinding b {
        .exposure   = &exposure,
        // saturation_boost intentionally null
        .bloom_post = &bloom,
        // ao_strength + ... omitted (null)
        .tonemap_op = &tonemap,
    };

    ep::apply(b, ep::kDefaults);

    EXPECT_FLOAT_EQ(exposure, 3.0F);
    EXPECT_FLOAT_EQ(bloom, 0.04F);
    EXPECT_EQ(tonemap, static_cast<std::int32_t>(ep::TonemapOp::kHable));
}

// ---- apply(): id overload forwards to preset overload --------------------
TEST(CompositeFxBinding, ApplyByIdSelectsCorrectPreset)
{
    float exposure { 0 };
    std::int32_t tonemap { -1 };

    const ep::CompositeFxBinding b {
        .exposure   = &exposure,
        .tonemap_op = &tonemap,
    };

    ep::apply(b, ep::PresetId::kPerformance);
    EXPECT_FLOAT_EQ(exposure, 1.5F);
    EXPECT_EQ(tonemap, static_cast<std::int32_t>(ep::TonemapOp::kNarkowicz));

    ep::apply(b, ep::PresetId::kHdrDemo);
    EXPECT_FLOAT_EQ(exposure, 1.0F);
    EXPECT_EQ(tonemap, static_cast<std::int32_t>(ep::TonemapOp::kAgx));
}
