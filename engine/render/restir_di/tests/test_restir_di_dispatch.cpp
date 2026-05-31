// =============================================================================
// CHROMODYNAMIC — engine/render/restir_di/tests/test_restir_di_dispatch.cpp
// Phase 550 / Sprint-1 — standalone GPU dispatch smoke for cd::restir_di.
//
// Tests:
//   1. Static helpers (group_count_x/y + reservoir_buffer_size) compile and
//      return the expected values for representative viewport sizes — runs
//      everywhere, no Vulkan ICD needed.
//   2. End-to-end prepare()+record()+submit() against a real Vulkan device
//      when one is available; gracefully GTEST_SKIPs otherwise (CI without
//      a GPU stays green, mirroring tests/vulkan/test_rhi_vulkan.cpp).
//
// hello_engine is NOT touched in this sprint; this test exercises the
// library standalone via the cd::rhi public surface.
// =============================================================================
#include <cd/restir_di/DispatchPass.hpp>

#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>

namespace
{

// --- Helpers -----------------------------------------------------------------

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = true;  // surface any descriptor / push-constant
                                    // / layout mismatch during the dispatch
                                    // — mirrors the DDGI Sprint-1 setup.
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

// Reservoir layout assumption shared with the GLSL side. Locked here so a
// silent reordering / size change of GpuReservoir trips a test rather than
// a runtime descriptor-stride mismatch.
TEST(RestirDiDispatch, GpuReservoirIsThirtyTwoBytes)
{
    static_assert(sizeof(cd::restir_di::GpuReservoir) == 32U,
                  "DiReservoir GLSL layout expects 8 x 4B fields");
    EXPECT_EQ(sizeof(cd::restir_di::GpuReservoir), 32U);
}

// --- Static math (always runs) ----------------------------------------------

TEST(RestirDiDispatch, GroupCountRoundsUpToSixteen)
{
    // 1920×1080: 1920/16 = 120 (exact), 1080/16 = 67.5 → 68 (ceiling).
    EXPECT_EQ(cd::restir_di::DispatchPass::group_count_x(1920U), 120U);
    EXPECT_EQ(cd::restir_di::DispatchPass::group_count_y(1080U), 68U);

    // Boundary: zero viewport → zero groups (caller wouldn't dispatch but
    // the helper must still be total).
    EXPECT_EQ(cd::restir_di::DispatchPass::group_count_x(0U), 0U);
    EXPECT_EQ(cd::restir_di::DispatchPass::group_count_y(0U), 0U);

    // Boundary: 1 pixel → 1 group (ceiling kicks in).
    EXPECT_EQ(cd::restir_di::DispatchPass::group_count_x(1U), 1U);
    EXPECT_EQ(cd::restir_di::DispatchPass::group_count_y(1U), 1U);
}

TEST(RestirDiDispatch, ReservoirBufferSizeMatchesGpuLayout)
{
    // The buffer size is w*h*sizeof(GpuReservoir) — 32 bytes per reservoir.
    EXPECT_EQ(cd::restir_di::DispatchPass::reservoir_buffer_size(1920U, 1080U),
              static_cast<std::uint64_t>(1920U) * 1080U * 32U);
    EXPECT_EQ(cd::restir_di::DispatchPass::reservoir_buffer_size(0U, 0U), 0U);
}

// --- GPU end-to-end (skips on no-Vulkan hosts) ------------------------------

TEST(RestirDiDispatch, RejectsZeroViewport)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::DispatchPass pass;
    cd::restir_di::DispatchConfig cfg {};
    cfg.viewport_width  = 0U;
    cfg.viewport_height = 0U;
    auto r = pass.prepare(*dev, cfg);
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(pass.is_ready());
}

TEST(RestirDiDispatch, PrepareAllocatesReservoirBuffer)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::DispatchPass pass;
    cd::restir_di::DispatchConfig cfg {};
    cfg.viewport_width  = 256U;
    cfg.viewport_height = 192U;
    cfg.light_count     = 8U;
    cfg.candidates      = 32U;

    auto r = pass.prepare(*dev, cfg);
    ASSERT_TRUE(r.has_value()) << "prepare failed: " << r.error().message;
    EXPECT_TRUE(pass.is_ready());
    EXPECT_TRUE(pass.reservoir_buffer().is_valid());
}

TEST(RestirDiDispatch, RecordAndSubmitNoValidationErrors)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::DispatchPass pass;
    cd::restir_di::DispatchConfig cfg {};
    cfg.viewport_width  = 128U;
    cfg.viewport_height = 96U;
    cfg.light_count     = 4U;
    cfg.candidates      = 16U;

    auto pr = pass.prepare(*dev, cfg);
    ASSERT_TRUE(pr.has_value()) << pr.error().message;

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    pass.record(*cb);
    cb->end();

    // Submit and wait — the driver / validation layer surfaces any
    // descriptor / push-constant / pipeline-layout mismatch at this
    // point. A clean wait_idle() is the Sprint-1 success signal.
    dev->submit(*cb);
    dev->wait_idle();
}

TEST(RestirDiDispatch, IdempotentShutdown)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::DispatchPass pass;
    cd::restir_di::DispatchConfig cfg {};
    cfg.viewport_width  = 64U;
    cfg.viewport_height = 64U;
    ASSERT_TRUE(pass.prepare(*dev, cfg).has_value());

    pass.shutdown();
    EXPECT_FALSE(pass.is_ready());
    EXPECT_FALSE(pass.reservoir_buffer().is_valid());

    // Second shutdown must be a no-op (not double-free).
    pass.shutdown();
    EXPECT_FALSE(pass.is_ready());
}

}  // namespace
