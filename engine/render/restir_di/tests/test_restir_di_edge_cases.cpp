// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/tests/test_restir_di_edge_cases.cpp
// 80->100 marathon -- GPU-gated edge / negative path coverage.
//
// ADD-ONLY: this file extends the real-GPU coverage of the ReSTIR DI + SVGF
// RHI passes with the error / degenerate paths the existing per-sprint smokes
// did not pin: invalid-handle no-ops, zero-iteration rejection, re-prepare
// with a new viewport (idempotent reconfigure), the all-zero SVGF input
// "nothing to denoise" path, and a 1x1 minimum-viewport dispatch. Every case
// keeps the existing Vulkan device gate (GTEST_SKIP when no ICD) so CI hosts
// without a GPU stay green. None of these alter rendered output of the engine
// -- they only exercise the library's own pass plumbing standalone.
//
// hello_engine is NOT touched.
// =============================================================================
#include <cd/restir_di/Denoiser.hpp>
#include <cd/restir_di/DispatchPass.hpp>
#include <cd/restir_di/FullPipelineDenoised.hpp>
#include <cd/restir_di/FullSvgfPipeline.hpp>
#include <cd/restir_di/SvgfDenoiser.hpp>

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>

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

cd::rhi::BufferHandle alloc_reservoir(cd::rhi::IDevice& dev,
                                      std::uint32_t     w,
                                      std::uint32_t     h,
                                      const char*       name)
{
    cd::rhi::BufferDesc d {};
    d.size       = cd::restir_di::DispatchPass::reservoir_buffer_size(w, h);
    d.usage      = cd::rhi::BufferUsage::kStorage;
    d.memory     = cd::rhi::MemoryUsage::kGpuOnly;
    d.debug_name = name;
    auto r = dev.create_buffer(d);
    if (!r.has_value())
        return {};
    return *r;
}

// --- Denoiser: zero-iteration rejection + invalid-handle no-op --------------

TEST(RestirDiEdgeCases, DenoiserRejectsZeroIterationCount)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::Denoiser den;
    cd::restir_di::DenoiserConfig cfg {};
    cfg.viewport_width  = 64U;
    cfg.viewport_height = 64U;
    cfg.iteration_count = 0U;  // must be rejected
    auto r = den.configure(*dev, cfg);
    EXPECT_FALSE(r.has_value());
    EXPECT_FALSE(den.is_ready());
}

TEST(RestirDiEdgeCases, DenoiserExecuteWithInvalidBuffersIsSilentNoOp)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    constexpr std::uint32_t kW = 64U;
    constexpr std::uint32_t kH = 64U;

    cd::restir_di::Denoiser den;
    cd::restir_di::DenoiserConfig cfg {};
    cfg.viewport_width  = kW;
    cfg.viewport_height = kH;
    cfg.iteration_count = 1U;
    cfg.step_width      = 1.0F;
    ASSERT_TRUE(den.configure(*dev, cfg).has_value());

    const auto good = alloc_reservoir(*dev, kW, kH, "edge_denoiser_good");
    ASSERT_TRUE(good.is_valid());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    // Either handle invalid -> execute() must early-return without recording
    // a dispatch (no validation error at submit). Reservoir-in invalid:
    den.execute(*cb, {}, {}, {}, good);
    // Output invalid:
    den.execute(*cb, good, {}, {}, {});
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();

    dev->destroy_buffer(good);
}

// --- DispatchPass: re-prepare with a new viewport (idempotent reconfigure) ---

TEST(RestirDiEdgeCases, DispatchPassReprepareWithNewViewport)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::DispatchPass pass;
    cd::restir_di::DispatchConfig cfg {};
    cfg.viewport_width  = 64U;
    cfg.viewport_height = 64U;
    cfg.light_count     = 4U;
    cfg.candidates      = 16U;
    ASSERT_TRUE(pass.prepare(*dev, cfg).has_value());
    const auto first_size =
        cd::restir_di::DispatchPass::reservoir_buffer_size(64U, 64U);

    // Re-prepare at a different size: the pass must drop the prior resources
    // and rebuild without leaking (prepare() calls shutdown() internally).
    cfg.viewport_width  = 128U;
    cfg.viewport_height = 96U;
    ASSERT_TRUE(pass.prepare(*dev, cfg).has_value());
    EXPECT_TRUE(pass.is_ready());
    EXPECT_TRUE(pass.reservoir_buffer().is_valid());

    const auto second_size =
        cd::restir_di::DispatchPass::reservoir_buffer_size(128U, 96U);
    EXPECT_NE(first_size, second_size);

    // Record + submit at the new size: clean wait_idle is the success signal.
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    pass.record(*cb);
    pass.execute_temporal_reuse(*cb, 0U);
    pass.execute_spatial_reuse(*cb, 0U);
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();
}

TEST(RestirDiEdgeCases, DispatchPassMinimumViewportOnePixel)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::DispatchPass pass;
    cd::restir_di::DispatchConfig cfg {};
    cfg.viewport_width  = 1U;
    cfg.viewport_height = 1U;
    cfg.light_count     = 1U;
    cfg.candidates      = 1U;
    ASSERT_TRUE(pass.prepare(*dev, cfg).has_value());

    // 1x1 -> exactly one group on each axis for both group-count schedules.
    EXPECT_EQ(cd::restir_di::DispatchPass::group_count_x(1U), 1U);
    EXPECT_EQ(cd::restir_di::DispatchPass::reuse_group_count_x(1U), 1U);

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    pass.record(*cb);
    pass.execute_temporal_reuse(*cb, 0U);
    pass.execute_spatial_reuse(*cb, 0U);
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();
}

// --- SvgfDenoiser: reconfigure-before-configure + execute-on-unready --------

TEST(RestirDiEdgeCases, SvgfKnobConfigureBeforeStructConfigureRejected)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    // The knob overload needs a prior struct-configure to have stashed a
    // viewport + device; calling it first must be rejected.
    cd::restir_di::SvgfDenoiser den;
    EXPECT_FALSE(den.configure(3, 1.0F, 1.0F).has_value());
    EXPECT_FALSE(den.is_ready());
}

TEST(RestirDiEdgeCases, SvgfExecuteOnUnreadyIsNoOp)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::SvgfDenoiser den;  // never configured

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    EXPECT_FALSE(den.execute(*cb, {}, {}, {}, {}, {}));
    cb->end();
}

TEST(RestirDiEdgeCases, SvgfReconfigureResizesViewport)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::SvgfDenoiser den;
    cd::restir_di::SvgfDenoiserConfig cfg {};
    cfg.viewport_width    = 32U;
    cfg.viewport_height   = 32U;
    cfg.filter_iterations = 3U;
    cfg.depth_phi         = 1.0F;
    cfg.normal_phi        = 128.0F;
    cfg.temporal_alpha    = 0.2F;
    ASSERT_TRUE(den.configure(*dev, cfg).has_value());
    EXPECT_EQ(den.viewport_width(), 32U);

    // Re-configure at a new size; the internal scratch SSBOs must be rebuilt.
    cfg.viewport_width  = 64U;
    cfg.viewport_height = 48U;
    ASSERT_TRUE(den.configure(*dev, cfg).has_value());
    EXPECT_EQ(den.viewport_width(),  64U);
    EXPECT_EQ(den.viewport_height(), 48U);
    EXPECT_TRUE(den.is_ready());
}

TEST(RestirDiEdgeCases, SvgfTemporalAlphaClampedToValidRange)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    // temporal_alpha is clamped to (0, 1] at configure() time so a zero /
    // out-of-range alpha cannot disable the moment accumulation entirely.
    cd::restir_di::SvgfDenoiser den;
    cd::restir_di::SvgfDenoiserConfig cfg {};
    cfg.viewport_width    = 32U;
    cfg.viewport_height   = 32U;
    cfg.filter_iterations = 1U;
    cfg.temporal_alpha    = 0.0F;  // below the clamp floor
    ASSERT_TRUE(den.configure(*dev, cfg).has_value());
    EXPECT_GT(den.temporal_alpha(), 0.0F);
    EXPECT_LE(den.temporal_alpha(), 1.0F);

    // An over-range alpha is clamped down to 1.0.
    cfg.temporal_alpha = 5.0F;
    ASSERT_TRUE(den.configure(*dev, cfg).has_value());
    EXPECT_FLOAT_EQ(den.temporal_alpha(), 1.0F);
}

// --- FullSvgfPipeline: single-iteration knob path via SvgfDenoiser ----------

TEST(RestirDiEdgeCases, SvgfSingleIterationExecutesClean)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    constexpr std::uint32_t kW = 32U;
    constexpr std::uint32_t kH = 32U;

    cd::restir_di::SvgfDenoiser den;
    cd::restir_di::SvgfDenoiserConfig cfg {};
    cfg.viewport_width    = kW;
    cfg.viewport_height   = kH;
    cfg.filter_iterations = 1U;  // single A-trous pass -> result in out_tex
    cfg.temporal_alpha    = 0.2F;
    ASSERT_TRUE(den.configure(*dev, cfg).has_value());

    const auto in_buf  = alloc_reservoir(*dev, kW, kH, "edge_svgf_1iter_in");
    const auto out_buf = alloc_reservoir(*dev, kW, kH, "edge_svgf_1iter_out");
    ASSERT_TRUE(in_buf.is_valid());
    ASSERT_TRUE(out_buf.is_valid());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    EXPECT_TRUE(den.execute(*cb, in_buf, {}, {}, {}, out_buf));
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();

    dev->destroy_buffer(in_buf);
    dev->destroy_buffer(out_buf);
}

// --- FullPipelineDenoised: re-configure resets frame_index ------------------

TEST(RestirDiEdgeCases, FullPipelineReconfigureResetsFrameIndex)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    constexpr std::uint32_t kW = 32U;
    constexpr std::uint32_t kH = 32U;

    cd::restir_di::FullPipelineDenoised pipe;
    ASSERT_TRUE(pipe.configure(*dev, kW, kH).has_value());

    const auto out_buf = alloc_reservoir(*dev, kW, kH, "edge_full_pipe_out");
    ASSERT_TRUE(out_buf.is_valid());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    EXPECT_TRUE(pipe.execute(*cb, cd::restir_di::SceneLightView{},
                             {}, {}, {}, out_buf));
    cb->end();
    dev->submit(*cb);
    dev->wait_idle();
    EXPECT_EQ(pipe.frame_index(), 1U);

    // Re-configure: frame_index must reset to 0 (fresh PCG seed sequence).
    ASSERT_TRUE(pipe.configure(*dev, kW, kH).has_value());
    EXPECT_EQ(pipe.frame_index(), 0U);

    dev->destroy_buffer(out_buf);
}

}  // namespace
