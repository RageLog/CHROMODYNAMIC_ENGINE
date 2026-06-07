// =============================================================================
// CHROMODYNAMIC — samples/engine/hello_engine/tests/test_hello_engine_fx_defaults.cpp
//
// phase882 — first-boot HelloEngineFx default lock. Phase 875 + 876
// converged on a clean-sky / no-haze first-boot look after the user
// reported persistent fog on every prior default tuning. This test
// codifies the "fog OFF + aerial OFF" baseline so a future tuning
// pass can't silently flip the engine back into haze-by-default.
//
// The struct lives in HelloEngineFx.hpp (header-only POD) so the
// test compiles with only cd::core in the deps list.
// =============================================================================
#include <gtest/gtest.h>

#include "../HelloEngineFx.hpp"

namespace {

TEST(HelloEngineFxDefaults, FogAndAerialAreOffOnFirstBoot)
{
    // Phase 875 root cause: the prior defaults
    // (fog_density 0.03, aerial_perspective 0.10) painted a constant
    // haze on every distant pixel even when the user expected
    // "fog off". The fog slider became indistinguishable from the
    // analytical sky's pale horizon. These literals lock the
    // post-875 state so a future "let's add subtle atmosphere by
    // default" PR has to justify itself in a peer review.
    cd_sample::HelloEngineFx fx {};
    EXPECT_FLOAT_EQ(fx.fog_density,        0.0F);
    EXPECT_FLOAT_EQ(fx.aerial_perspective, 0.0F);
}

TEST(HelloEngineFxDefaults, CloudsCoverageStaysSubtleByDefault)
{
    // Phase 876 dropped clouds_coverage 0.45 → 0.20 — clouds visible
    // as light wisps without overpowering the analytical sky's new
    // saturated horizon. Lock the 0.20 baseline so a tuning bump
    // back toward 0.45 (cloud overlay dominates the sky again)
    // requires explicit review.
    cd_sample::HelloEngineFx fx {};
    EXPECT_FLOAT_EQ(fx.clouds_coverage, 0.20F);
    // Anything between 0.10 and 0.35 reads as "subtle wisps"; below
    // is barely-visible, above starts hiding the sky.
    EXPECT_GE(fx.clouds_coverage, 0.10F);
    EXPECT_LE(fx.clouds_coverage, 0.35F);
}

TEST(HelloEngineFxDefaults, TaaAmountIsOnByDefault)
{
    // Phase 857 turned TAA on by default because phase 855's
    // velocity-based history decay eliminates the move-blur the
    // constant-alpha TAA used to produce. Lock the 0.85 default —
    // a regression to 0.0 would re-introduce the aliasing the
    // cinematic-defaults pass solved.
    cd_sample::HelloEngineFx fx {};
    EXPECT_NEAR(fx.taa_amount, 0.85F, 0.05F);
    EXPECT_GT(fx.taa_amount, 0.50F);
}

TEST(HelloEngineFxDefaults, ExposureAndTonemapAreCanonical)
{
    // Phase 450 + 857: exposure = 1.0 (no boost, scene reads in the
    // tonemap's mid-range), tonemap_op = 2 (Hable / Uncharted 2).
    // These have settled across many user-supervised iterations;
    // lock them.
    cd_sample::HelloEngineFx fx {};
    EXPECT_FLOAT_EQ(fx.exposure, 1.0F);
    EXPECT_EQ(fx.tonemap_op, 2);
}

}  // anonymous
