// =============================================================================
// CHROMODYNAMIC — cd::post::exposure tests
//
// Phase 454 — CPU kernel: EV math + EMA smoothing.
// Phase 461 — GpuReduction skeleton: API-surface tests against
//             cd::rhi::NullDevice. The Null backend fakes pipeline /
//             descriptor / buffer creation and returns zero-filled
//             readback bytes; the tests therefore validate the API
//             surface (handles allocated, lifetimes clean) without
//             asserting on a live GPU reduction.
// =============================================================================
#include <cd/post/exposure/Exposure.hpp>
#include <cd/rhi/NullDevice.hpp>

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

// =============================================================================
// Phase 461 — GpuReduction skeleton API tests
// =============================================================================

TEST(ExposureGpu, ReductionShaderStringIsValidGlsl)
{
    // Sanity-check the embedded compute shader header. Catches accidental
    // truncation of the constexpr string at build time.
    EXPECT_NE(pex::kExposureReductionCS.find("#version 460"), std::string_view::npos);
    EXPECT_NE(pex::kExposureReductionCS.find("local_size_x = 8"), std::string_view::npos);
    EXPECT_NE(pex::kExposureReductionCS.find("cd_hdr"), std::string_view::npos);
    EXPECT_NE(pex::kExposureReductionCS.find("partial"), std::string_view::npos);
}

TEST(ExposureGpu, PartialSlotCountIsPowerOfTwo)
{
    // 256 partial slots = 1 KB SSBO + each slot covers an 8x8 tile,
    // so the helper supports up to 128x128 HDR targets without an
    // intermediate downsample. Bumping kPartialSlotCount needs a
    // matching grid-fit check in create().
    EXPECT_EQ(pex::kPartialSlotCount, 256U);
    EXPECT_EQ(pex::kPartialSlotCount & (pex::kPartialSlotCount - 1U), 0U);
}

TEST(ExposureGpu, CreateReturnsErrorOnZeroExtent)
{
    cd::rhi::NullDevice dev;
    auto r = pex::GpuReduction::create(dev, cd::rhi::Extent2D { 0U, 0U });
    EXPECT_FALSE(r.has_value());
}

TEST(ExposureGpu, CreateReturnsErrorWhenGridExceedsBudget)
{
    cd::rhi::NullDevice dev;
    // 1024x1024 -> 128x128 = 16384 tiles, far past kPartialSlotCount (256).
    auto r = pex::GpuReduction::create(dev, cd::rhi::Extent2D { 1024U, 1024U });
    EXPECT_FALSE(r.has_value());
}

TEST(ExposureGpu, CreateSucceedsAtTypicalDownsampledExtent)
{
    cd::rhi::NullDevice dev;
    // Engine integration plan: downsample HDR to <= 128x128 before
    // reduction. 128x128 -> 16x16 = 256 tiles == kPartialSlotCount.
    auto r = pex::GpuReduction::create(dev, cd::rhi::Extent2D { 128U, 128U });
    ASSERT_TRUE(r.has_value());
    auto gpu = std::move(*r);

    EXPECT_EQ(gpu.grid_x, 16U);
    EXPECT_EQ(gpu.grid_y, 16U);
    EXPECT_TRUE(gpu.shader_module.is_valid());
    EXPECT_TRUE(gpu.set_layout.is_valid());
    EXPECT_TRUE(gpu.pipeline_layout.is_valid());
    EXPECT_TRUE(gpu.pipeline.is_valid());
    EXPECT_TRUE(gpu.descriptor_set.is_valid());
    EXPECT_TRUE(gpu.linear_sampler.is_valid());
    EXPECT_TRUE(gpu.partial_buffer.is_valid());

    gpu.destroy();
    // Post-destroy: every owned handle is invalidated.
    EXPECT_FALSE(gpu.shader_module.is_valid());
    EXPECT_FALSE(gpu.pipeline.is_valid());
    EXPECT_FALSE(gpu.partial_buffer.is_valid());
    EXPECT_EQ(gpu.device, nullptr);
}

TEST(ExposureGpu, CreateRoundsUpToTileBoundary)
{
    cd::rhi::NullDevice dev;
    // 100x60 should round up to 13x8 tiles = 104 — within budget.
    auto r = pex::GpuReduction::create(dev, cd::rhi::Extent2D { 100U, 60U });
    ASSERT_TRUE(r.has_value());
    auto gpu = std::move(*r);
    EXPECT_EQ(gpu.grid_x, 13U);   // ceil(100/8) = 13
    EXPECT_EQ(gpu.grid_y, 8U);    // ceil(60/8)  = 8
    EXPECT_LE(gpu.grid_x * gpu.grid_y, pex::kPartialSlotCount);
    gpu.destroy();
}

TEST(ExposureGpu, DispatchAndReadbackOnNullDeviceReturnsSkipSentinel)
{
    cd::rhi::NullDevice dev;
    auto r = pex::GpuReduction::create(dev, cd::rhi::Extent2D { 64U, 64U });
    ASSERT_TRUE(r.has_value());
    auto gpu = std::move(*r);

    auto cmd = dev.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    // Pass a default-constructed view: NullDevice doesn't dereference it;
    // a real backend would consume the descriptor write the integrator
    // performs before recording.
    const float result = gpu.dispatch_and_readback(*cmd, cd::rhi::TextureViewHandle {});
    cmd->end();

    // NullDevice readback yields zero-filled bytes -> all-invalid tiles
    // -> kSkipThreshold so the caller falls back to prev_ev.
    EXPECT_FLOAT_EQ(result, pex::Settings::kSkipThreshold);
    gpu.destroy();
}

TEST(ExposureGpu, DestroyIsIdempotentOnDefaultConstructed)
{
    pex::GpuReduction gpu {};
    gpu.destroy();  // No device, no handles — must be a clean no-op.
    SUCCEED();
}

TEST(ExposureGpu, DoubleDestroyIsSafe)
{
    cd::rhi::NullDevice dev;
    auto r = pex::GpuReduction::create(dev, cd::rhi::Extent2D { 64U, 64U });
    ASSERT_TRUE(r.has_value());
    auto gpu = std::move(*r);
    gpu.destroy();
    gpu.destroy();  // After the first destroy, device is nullptr -> no-op.
    SUCCEED();
}
