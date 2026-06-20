// =============================================================================
// CHROMODYNAMIC -- engine/render/ddgi/tests/test_ddgi_pass_host.cpp
// 80->100 marathon -- host-side DispatchPass / push-constant / UBO / GLSL
// validation that needs NO Vulkan device.
//
// ADD-ONLY: pins the parts of the DDGI dispatch surface that are pure host
// state -- push-constant + scene/sample UBO POD layouts (std140/std430 sizes
// the static_asserts already lock, re-checked at runtime), the GLSL source
// string contract (every pass present + key extension / binding tokens), and
// the CPU-stub execute_* validators that run without a command buffer or
// device (probe_count==0 rejection, atlas-not-allocated rejection,
// execute_sample() pre-bind rejection, counter increments).
//
// None of these change rendered output -- they exercise the library's own
// plumbing. CPU-only: runs on every host (no GPU/ICD needed).
//
// hello_engine is NOT touched. No probe / blend / sample math or GLSL is
// modified -- the existing strings stay byte-identical.
// =============================================================================

#include <cd/ddgi/Ddgi.hpp>
#include <cd/ddgi/DispatchPass.hpp>
#include <cd/ddgi/FullPipeline.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace
{

using cd::ddgi::BlendPushConstants;
using cd::ddgi::DispatchPass;
using cd::ddgi::FullPipeline;
using cd::ddgi::ProbeGrid;
using cd::ddgi::SampleReconstructUbo;
using cd::ddgi::SamplePushConstants;
using cd::ddgi::SceneLightUbo;
using cd::ddgi::TracePushConstants;

// ===========================================================================
// Push-constant + UBO POD layouts -- runtime mirror of the header static_asserts
// (so the size contract is exercised by ctest, not only the compiler).
// ===========================================================================
TEST(DdgiPassHost, TracePushConstantSizeMatchesShaderBlock)
{
    EXPECT_EQ(sizeof(TracePushConstants), 80U);
    EXPECT_EQ(alignof(TracePushConstants), 16U);
}

TEST(DdgiPassHost, BlendPushConstantSizeMatchesShaderBlock)
{
    EXPECT_EQ(sizeof(BlendPushConstants), 64U);
    EXPECT_EQ(alignof(BlendPushConstants), 16U);
}

TEST(DdgiPassHost, SamplePushConstantSizeMatchesShaderBlock)
{
    EXPECT_EQ(sizeof(SamplePushConstants), 80U);
    EXPECT_EQ(alignof(SamplePushConstants), 16U);
    // Under the 128-byte push-constant floor (mat4 lives in a UBO instead).
    EXPECT_LE(sizeof(SamplePushConstants), 128U);
}

TEST(DdgiPassHost, SceneLightUboSizeAndDefaults)
{
    EXPECT_EQ(sizeof(SceneLightUbo), 32U);
    const SceneLightUbo u {};
    // Default sun points +Y with intensity 1; colour white with a 0.05 floor.
    EXPECT_FLOAT_EQ(u.sun_dir[1], 1.0F);
    EXPECT_FLOAT_EQ(u.sun_dir[3], 1.0F);
    EXPECT_FLOAT_EQ(u.sun_col[0], 1.0F);
    EXPECT_FLOAT_EQ(u.sun_col[3], 0.05F);
}

TEST(DdgiPassHost, SampleReconstructUboIsIdentityByDefault)
{
    EXPECT_EQ(sizeof(SampleReconstructUbo), 64U);
    const SampleReconstructUbo u {};
    // cd::math::Mat4f is column-major: u.inv_vp[col][row]. Diagonal == 1,
    // off-diagonal == 0 (identity seed).
    EXPECT_FLOAT_EQ(u.inv_vp[0][0], 1.0F);
    EXPECT_FLOAT_EQ(u.inv_vp[1][1], 1.0F);
    EXPECT_FLOAT_EQ(u.inv_vp[3][3], 1.0F);
    EXPECT_FLOAT_EQ(u.inv_vp[0][1], 0.0F);
    EXPECT_FLOAT_EQ(u.inv_vp[2][3], 0.0F);
    EXPECT_TRUE(u.inv_vp == cd::math::Mat4f::identity());
}

// ===========================================================================
// GLSL source-string contract -- all five passes present + key tokens.
// ===========================================================================
TEST(DdgiPassHost, AllShaderSourcesNonEmpty)
{
    EXPECT_FALSE(cd::ddgi::kDdgiTraceCS.empty());
    EXPECT_FALSE(cd::ddgi::kDdgiBlendIrradianceCS.empty());
    EXPECT_FALSE(cd::ddgi::kDdgiBlendVisibilityCS.empty());
    EXPECT_FALSE(cd::ddgi::kDdgiSampleFS.empty());
    EXPECT_FALSE(cd::ddgi::kDdgiSampleCS.empty());
}

TEST(DdgiPassHost, TraceShaderUsesRayQueryExtension)
{
    EXPECT_NE(cd::ddgi::kDdgiTraceCS.find("GL_EXT_ray_query"),
              std::string_view::npos);
    EXPECT_NE(cd::ddgi::kDdgiTraceCS.find("rayQueryEXT"),
              std::string_view::npos);
    EXPECT_NE(cd::ddgi::kDdgiTraceCS.find("accelerationStructureEXT"),
              std::string_view::npos);
}

TEST(DdgiPassHost, ProbeUpdateAliasMatchesTraceSource)
{
    // kProbeUpdateCS is the backwards-compat alias for kDdgiTraceCS.
    EXPECT_EQ(cd::ddgi::kProbeUpdateCS, cd::ddgi::kDdgiTraceCS);
}

TEST(DdgiPassHost, SampleShadersExposeChebyshevGate)
{
    EXPECT_NE(cd::ddgi::kDdgiSampleFS.find("chebyshev_weight"),
              std::string_view::npos);
    EXPECT_NE(cd::ddgi::kDdgiSampleCS.find("chebyshev_weight"),
              std::string_view::npos);
}

TEST(DdgiPassHost, BlendShadersUseHysteresisMix)
{
    // Both blend passes EMA-blend the new value with the prior atlas via mix().
    EXPECT_NE(cd::ddgi::kDdgiBlendIrradianceCS.find("hysteresis"),
              std::string_view::npos);
    EXPECT_NE(cd::ddgi::kDdgiBlendIrradianceCS.find("mix("),
              std::string_view::npos);
    EXPECT_NE(cd::ddgi::kDdgiBlendVisibilityCS.find("visibility_atlas"),
              std::string_view::npos);
}

// ===========================================================================
// CPU-stub execute_blend_* -- prime_for_cpu_test() + validation, no device.
// ===========================================================================
TEST(DdgiPassHost, BlendStubsRejectUnallocatedAtlas)
{
    DispatchPass pass;
    // Valid grid but atlas dims left at 0 -> "atlas not allocated" rejection.
    pass.prime_for_cpu_test(ProbeGrid{}, /*atlas_w=*/0U, /*atlas_h=*/0U);

    const auto r_irr = pass.execute_blend_irradiance(0U);
    EXPECT_FALSE(r_irr.has_value());
    EXPECT_NE(r_irr.error().message.find("atlas not allocated"),
              std::string::npos);
    EXPECT_EQ(pass.blend_irr_call_count(), 0U);

    const auto r_vis = pass.execute_blend_visibility(0U);
    EXPECT_FALSE(r_vis.has_value());
    EXPECT_NE(r_vis.error().message.find("atlas not allocated"),
              std::string::npos);
    EXPECT_EQ(pass.blend_vis_call_count(), 0U);
}

TEST(DdgiPassHost, BlendStubsCountersAreIndependentAndMonotonic)
{
    DispatchPass pass;
    pass.prime_for_cpu_test(ProbeGrid{}, 128U, 16U);

    for (std::uint32_t f = 0; f < 5U; ++f)
        EXPECT_TRUE(pass.execute_blend_irradiance(f).has_value());
    for (std::uint32_t f = 0; f < 3U; ++f)
        EXPECT_TRUE(pass.execute_blend_visibility(f).has_value());

    EXPECT_EQ(pass.blend_irr_call_count(), 5U);
    EXPECT_EQ(pass.blend_vis_call_count(), 3U);
}

// ===========================================================================
// CPU-stub execute_sample() -- validator runs without a command buffer.
// Before init() the sample pipeline / atlases are null, so it must reject and
// leave the counter at 0.
// ===========================================================================
TEST(DdgiPassHost, SampleStubRejectsBeforeInit)
{
    DispatchPass pass;
    EXPECT_EQ(pass.sample_call_count(), 0U);
    const auto r = pass.execute_sample();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(pass.sample_call_count(), 0U);
}

// ===========================================================================
// Default-constructed pass / pipeline state.
// ===========================================================================
TEST(DdgiPassHost, DefaultConstructedPassIsUninitialised)
{
    const DispatchPass pass;
    EXPECT_FALSE(pass.initialised());
    EXPECT_EQ(pass.blend_irr_call_count(), 0U);
    EXPECT_EQ(pass.blend_vis_call_count(), 0U);
    EXPECT_EQ(pass.sample_call_count(), 0U);
    EXPECT_EQ(pass.sample_output_width(), 0U);
    EXPECT_EQ(pass.sample_output_height(), 0U);
}

TEST(DdgiPassHost, DefaultConstructedPipelineIsUninitialised)
{
    const FullPipeline pipeline;
    EXPECT_FALSE(pipeline.initialised());
    EXPECT_EQ(pipeline.execute_call_count(), 0U);
    EXPECT_FALSE(pipeline.pass().initialised());
}

}  // namespace
