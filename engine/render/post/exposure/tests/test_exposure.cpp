// =============================================================================
// CHROMODYNAMIC — cd::post::exposure tests
//
// Phase 454. Validates the EV math + EMA smoothing in isolation. The
// GPU log-luminance reduction is covered by the composite-pass GPU
// tests; this suite covers the pure-CPU kernel.
// =============================================================================
#include <cd/post/exposure/Exposure.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace pex = cd::post::exposure;

TEST(Exposure, LogAvgLuminanceOnUniformGreyMatchesLog2)
{
    const std::uint32_t pixels = 4U;
    std::vector<float> rgba(pixels * 4U, 0.18F);  // 18% grey, fully opaque
    for (std::uint32_t i = 0; i < pixels; ++i) rgba[i * 4U + 3U] = 1.0F;

    const float la = pex::log_avg_luminance(rgba.data(), pixels);
    // log2(0.18) ≈ -2.474
    EXPECT_NEAR(la, std::log2(0.18F), 0.001F);
}

TEST(Exposure, EmptyDataReturnsSkipSentinel)
{
    EXPECT_EQ(pex::log_avg_luminance(nullptr, 0U), pex::Settings::kSkipThreshold);
}

TEST(Exposure, ComputeTargetEvOnGreySceneIsZero)
{
    pex::Settings s {};
    const float la = std::log2(0.18F);   // grey scene
    const float ev = pex::compute_target_ev(la, /*prev_ev=*/ 0.0F, s);
    // EV target = log2(key) - log_avg = log2(0.18) - log2(0.18) = 0
    EXPECT_NEAR(ev, 0.0F, 0.001F);
}

TEST(Exposure, ComputeTargetEvOnBrightSceneIsNegative)
{
    pex::Settings s {};
    // Bright scene: avg luma 0.72 (4x reference). Need to EV-down by 2 stops.
    const float la = std::log2(0.72F);
    const float ev = pex::compute_target_ev(la, /*prev_ev=*/ 0.0F, s);
    EXPECT_NEAR(ev, -2.0F, 0.05F);
}

TEST(Exposure, ComputeTargetEvOnDarkSceneIsPositive)
{
    pex::Settings s {};
    // Dark scene: avg luma 0.045 (1/4 reference). Need to EV-up by 2 stops.
    const float la = std::log2(0.045F);
    const float ev = pex::compute_target_ev(la, /*prev_ev=*/ 0.0F, s);
    EXPECT_NEAR(ev, 2.0F, 0.05F);
}

TEST(Exposure, ComputeTargetEvWithSkipReturnsPrev)
{
    pex::Settings s {};
    const float ev = pex::compute_target_ev(pex::Settings::kSkipThreshold,
                                            /*prev_ev=*/ 1.5F, s);
    EXPECT_FLOAT_EQ(ev, 1.5F);
}

TEST(Exposure, SmoothingClampsToMinMax)
{
    pex::Settings s {};
    s.min_ev = -2.0F;
    s.max_ev =  2.0F;

    // Even with huge dt and target far past max, smoothing clamps.
    const float ev = pex::apply_smoothing(0.0F, /*target=*/ 10.0F, /*dt=*/ 100.0F, s);
    EXPECT_LE(ev, 2.0F);
    EXPECT_NEAR(ev, 2.0F, 0.001F);
}

TEST(Exposure, SmoothingZeroDtKeepsPrev)
{
    pex::Settings s {};
    const float ev = pex::apply_smoothing(1.5F, /*target=*/ -1.5F, /*dt=*/ 0.0F, s);
    EXPECT_FLOAT_EQ(ev, 1.5F);
}

TEST(Exposure, SmoothingApproachesTargetOverTime)
{
    pex::Settings s {};
    float ev = 0.0F;
    const float target = 2.0F;
    // speed_down = 1.0 -> time-constant τ = 1.0s. After 5 τ = 5s the
    // EMA has reached >99% of the target.
    // 300 frames @ 60 fps = 5s
    for (int i = 0; i < 300; ++i)
    {
        ev = pex::apply_smoothing(ev, target, /*dt=*/ 1.0F / 60.0F, s);
    }
    EXPECT_NEAR(ev, target, 0.05F);   // within 2.5% of target after 5τ
    EXPECT_LT(ev, target);            // monotonically approaches from below
}

TEST(Exposure, ExposureMultiplierAtKey)
{
    pex::Settings s {};
    // EV 0 + key 0.18 -> multiplier = 1/0.18 ≈ 5.56
    EXPECT_NEAR(pex::compute_exposure_multiplier(0.0F, s), 1.0F / 0.18F, 0.01F);
}

TEST(Exposure, ExposureMultiplierOneStopBrighter)
{
    pex::Settings s {};
    const float m_at_0 = pex::compute_exposure_multiplier(0.0F, s);
    const float m_at_1 = pex::compute_exposure_multiplier(1.0F, s);
    // +1 EV = 2x multiplier
    EXPECT_NEAR(m_at_1 / m_at_0, 2.0F, 0.001F);
}

TEST(Exposure, UpdateEvComposesPipeline)
{
    pex::Settings s {};
    // Grey scene + prev_ev = 0 + 1s adaptation -> EV stays at 0.
    const float la = std::log2(0.18F);
    const float ev = pex::update_ev(la, /*prev=*/ 0.0F, /*dt=*/ 1.0F, s);
    EXPECT_NEAR(ev, 0.0F, 0.01F);
}
