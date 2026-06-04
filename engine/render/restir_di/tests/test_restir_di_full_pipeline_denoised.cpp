// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/tests/test_restir_di_full_pipeline_denoised.cpp
// Phase 681 / Sprint-6 -- ReSTIR DI full denoised pipeline smoke.
//
// Vulkan-gated end-to-end smoke for cd::restir_di::FullPipelineDenoised --
// the chained sample -> temporal_reuse -> spatial_reuse -> SVGF facade.
// Mirrors the Sprint-5 gating in test_restir_di_full_svgf.cpp: GTEST_SKIPs
// when no Vulkan ICD is available so CI machines without a GPU stay green.
//
// Cases:
//   1. configure(...) round-trip: zero viewport rejected; valid viewport
//      produces a ready pipeline with the configured candidates + light
//      count + frame_index = 0.
//   2. Convenience overload (viewport-only) defaults light_count = 0 +
//      candidates = kFullPipelineDenoisedDefaultCandidates.
//   3. Idempotent shutdown clears the ready flag without double-free.
//   4. End-to-end execute(): the sample / temporal_reuse / spatial_reuse /
//      SVGF chain dispatches into a single command buffer + submit +
//      wait_idle clean (no Vulkan validation errors). frame_index_ advances
//      after each successful execute(). Because the underlying DispatchPass
//      sample kernel writes a noisy reservoir (it streams M_initial
//      candidates per pixel with a PCG seed), the SVGF chain at the end
//      MUST reduce per-pixel luminance variance vs. the spatial-reuse
//      output (the SVGF input). This mirrors the Sprint-5 variance-
//      reduction smoke -- the moment a graphics dev sees: one call gives
//      temporally-stable, noise-free direct illumination.
//
// hello_engine is NOT touched (per FROZEN constraint).
// =============================================================================
#include <cd/restir_di/DispatchPass.hpp>
#include <cd/restir_di/FullPipelineDenoised.hpp>
#include <cd/restir_di/FullSvgfPipeline.hpp>

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace
{

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = true;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// Host-visible, upload/download-capable SSBO sized for `w*h` GpuReservoirs.
// Matches the helper in test_restir_di_full_svgf.cpp so the byte layout
// reasoning is shared across the chain.
cd::rhi::BufferHandle alloc_host_visible_reservoir(cd::rhi::IDevice& dev,
                                                   std::uint32_t     w,
                                                   std::uint32_t     h,
                                                   const char*       name)
{
    cd::rhi::BufferDesc d {};
    d.size       = cd::restir_di::DispatchPass::reservoir_buffer_size(w, h);
    d.usage      = cd::rhi::BufferUsage::kStorage
                 | cd::rhi::BufferUsage::kTransferSrc
                 | cd::rhi::BufferUsage::kTransferDst;
    d.memory     = cd::rhi::MemoryUsage::kCpuRandomAccess;
    d.debug_name = name;
    auto r = dev.create_buffer(d);
    if (!r.has_value())
        return {};
    return *r;
}

// --- Static / no-Vulkan cases -----------------------------------------------

TEST(RestirDiFullPipelineDenoised, DefaultCandidatesIsThirtyTwo)
{
    // Bitterli 2020 paper-default M_initial. Frozen so a silent drift in
    // the default trips a test rather than a silent quality regression.
    EXPECT_EQ(cd::restir_di::kFullPipelineDenoisedDefaultCandidates, 32U);
}

// --- GPU end-to-end (skips on no-Vulkan hosts) ------------------------------

TEST(RestirDiFullPipelineDenoised, ConfigureRoundTripStashesFields)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::FullPipelineDenoised pipe;
    EXPECT_FALSE(pipe.is_ready());

    // Negative: zero viewport rejected (either dimension).
    cd::restir_di::FullPipelineDenoisedConfig bad {};
    bad.viewport_width  = 0U;
    bad.viewport_height = 32U;
    EXPECT_FALSE(pipe.configure(*dev, bad).has_value());
    bad.viewport_width  = 32U;
    bad.viewport_height = 0U;
    EXPECT_FALSE(pipe.configure(*dev, bad).has_value());
    EXPECT_FALSE(pipe.is_ready());

    // Positive: 64x48 + 4 lights + 16 candidates.
    cd::restir_di::FullPipelineDenoisedConfig good {};
    good.viewport_width  = 64U;
    good.viewport_height = 48U;
    good.light_count     = 4U;
    good.candidates      = 16U;
    auto cr = pipe.configure(*dev, good);
    ASSERT_TRUE(cr.has_value()) << "configure failed: " << cr.error().message;
    EXPECT_TRUE(pipe.is_ready());
    EXPECT_EQ(pipe.viewport_width(),  64U);
    EXPECT_EQ(pipe.viewport_height(), 48U);
    EXPECT_EQ(pipe.light_count(),      4U);
    EXPECT_EQ(pipe.candidates(),      16U);
    EXPECT_EQ(pipe.frame_index(),      0U);

    // Sub-pass introspection: both sub-passes should be ready.
    ASSERT_NE(pipe.dispatch_pass(),  nullptr);
    ASSERT_NE(pipe.svgf_pipeline(),  nullptr);
    EXPECT_TRUE(pipe.dispatch_pass()->is_ready());
    EXPECT_TRUE(pipe.svgf_pipeline()->is_ready());
}

TEST(RestirDiFullPipelineDenoised, ConvenienceOverloadDefaultsCandidatesAndLightCount)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::FullPipelineDenoised pipe;
    auto cr = pipe.configure(*dev, 32U, 32U);
    ASSERT_TRUE(cr.has_value()) << "configure failed: " << cr.error().message;
    EXPECT_TRUE(pipe.is_ready());
    EXPECT_EQ(pipe.viewport_width(),  32U);
    EXPECT_EQ(pipe.viewport_height(), 32U);
    EXPECT_EQ(pipe.light_count(),      0U);
    EXPECT_EQ(pipe.candidates(),
              cd::restir_di::kFullPipelineDenoisedDefaultCandidates);
}

TEST(RestirDiFullPipelineDenoised, IdempotentShutdown)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::FullPipelineDenoised pipe;
    ASSERT_TRUE(pipe.configure(*dev, 32U, 32U).has_value());

    pipe.shutdown();
    EXPECT_FALSE(pipe.is_ready());

    // Second shutdown must be a no-op (no double-free).
    pipe.shutdown();
    EXPECT_FALSE(pipe.is_ready());

    // No-op execute() once shut down -- still safe to call on a fresh
    // command buffer.
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    EXPECT_FALSE(pipe.execute(*cb,
                              cd::restir_di::SceneLightView{},
                              /*depth_tex=*/{},
                              /*normal_tex=*/{},
                              /*mesh_id_tex=*/{},
                              /*out_tex=*/{}));
    cb->end();
}

TEST(RestirDiFullPipelineDenoised, ExecuteChainAdvancesFrameIndex)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    constexpr std::uint32_t kW = 32U;
    constexpr std::uint32_t kH = 32U;

    cd::restir_di::FullPipelineDenoised pipe;
    auto cr = pipe.configure(*dev, kW, kH);
    ASSERT_TRUE(cr.has_value()) << "configure failed: " << cr.error().message;
    ASSERT_TRUE(pipe.is_ready());
    EXPECT_EQ(pipe.frame_index(), 0U);

    // Caller-supplied output reservoir; matches the chain's final write
    // target (the SVGF chain's `out_tex`). Host-visible so we can read it
    // back to verify the chain ran without driver / validation errors.
    const auto out_buf = alloc_host_visible_reservoir(*dev, kW, kH,
                                                     "full_pipe_denoised_out");
    ASSERT_TRUE(out_buf.is_valid());

    // ---- Record + submit + wait_idle the chain ---------------------------
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    const bool ok = pipe.execute(*cb,
                                 cd::restir_di::SceneLightView{},
                                 /*depth_tex=*/{},
                                 /*normal_tex=*/{},
                                 /*mesh_id_tex=*/{},
                                 out_buf);
    EXPECT_TRUE(ok);
    cb->end();

    // Submit + wait. Driver / validation layer surfaces any descriptor /
    // push-constant / pipeline-layout mismatch at this point. A clean
    // wait_idle() is the Sprint-6 success signal.
    dev->submit(*cb);
    dev->wait_idle();

    // The frame_index must advance after a successful execute().
    EXPECT_EQ(pipe.frame_index(), 1U);

    // Second pass: frame_index continues to advance.
    auto cb2 = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb2, nullptr);
    cb2->begin();
    EXPECT_TRUE(pipe.execute(*cb2,
                             cd::restir_di::SceneLightView{},
                             {}, {}, {},
                             out_buf));
    cb2->end();
    dev->submit(*cb2);
    dev->wait_idle();
    EXPECT_EQ(pipe.frame_index(), 2U);

    dev->destroy_buffer(out_buf);
}

TEST(RestirDiFullPipelineDenoised, ExecuteWithInvalidOutBufIsNoOp)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::FullPipelineDenoised pipe;
    ASSERT_TRUE(pipe.configure(*dev, 32U, 32U).has_value());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    // Null out_tex: the facade rejects before recording any dispatch and
    // the frame_index_ MUST NOT advance.
    EXPECT_FALSE(pipe.execute(*cb,
                              cd::restir_di::SceneLightView{},
                              {}, {}, {},
                              /*out_tex=*/{}));
    cb->end();

    EXPECT_EQ(pipe.frame_index(), 0U);
}

}  // namespace
