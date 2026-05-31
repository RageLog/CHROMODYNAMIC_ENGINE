// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/tests/test_restir_di_svgf.cpp
// Phase 616 / Sprint-4 -- ReSTIR DI SVGF denoiser smoke.
//
// Vulkan-gated end-to-end smoke for cd::restir_di::SvgfDenoiser. Mirrors
// the Sprint-3 gating in test_restir_di_denoiser.cpp -- both cases
// GTEST_SKIP when no Vulkan ICD is available so CI machines without a GPU
// stay green.
//
// Cases:
//   1. Construct + configure + round-trip the (filter_iterations, depth_phi,
//      normal_phi) knob trio. Asserts the convenience overload updates the
//      stashed config without re-allocating pipelines.
//   2. configure(struct) + execute(...) with stub texture views (null
//      handles) -- the kernel must run the reservoir-luminance fallback
//      path and the descriptor wiring must record cleanly against a real
//      Vulkan device with validation enabled.
//
// hello_engine is NOT touched in this sprint; this test exercises the
// library standalone via the cd::rhi public surface.
// =============================================================================
#include <cd/restir_di/DispatchPass.hpp>
#include <cd/restir_di/SvgfDenoiser.hpp>

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
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

// --- Static math (always runs, no Vulkan needed) ----------------------------

TEST(RestirDiSvgfDenoiser, GroupCountRoundsUpToEight)
{
    EXPECT_EQ(cd::restir_di::SvgfDenoiser::group_count_x(1920U), 240U);
    EXPECT_EQ(cd::restir_di::SvgfDenoiser::group_count_y(1080U), 135U);
    EXPECT_EQ(cd::restir_di::SvgfDenoiser::group_count_x(0U),    0U);
    EXPECT_EQ(cd::restir_di::SvgfDenoiser::group_count_x(1U),    1U);
    EXPECT_EQ(cd::restir_di::SvgfDenoiser::group_count_y(9U),    2U);
}

TEST(RestirDiSvgfDenoiser, ShortHistoryThresholdIsFour)
{
    // Paper Section 4.1: 4-frame minimum before temporal variance is trusted.
    EXPECT_EQ(cd::restir_di::kSvgfShortHistoryThreshold, 4U);
}

// --- GPU end-to-end (skips on no-Vulkan hosts) ------------------------------

TEST(RestirDiSvgfDenoiser, ConfigureKnobRoundTripUpdatesStashedConfig)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    constexpr std::uint32_t kW = 64U;
    constexpr std::uint32_t kH = 48U;

    cd::restir_di::SvgfDenoiser den;
    cd::restir_di::SvgfDenoiserConfig cfg {};
    cfg.viewport_width    = kW;
    cfg.viewport_height   = kH;
    cfg.filter_iterations = 5U;
    cfg.depth_phi         = 1.0F;
    cfg.normal_phi        = 128.0F;
    cfg.temporal_alpha    = 0.2F;

    auto cr = den.configure(*dev, cfg);
    ASSERT_TRUE(cr.has_value()) << "configure failed: " << cr.error().message;
    EXPECT_TRUE(den.is_ready());
    EXPECT_EQ(den.filter_iterations(), 5U);
    EXPECT_FLOAT_EQ(den.depth_phi(),   1.0F);
    EXPECT_FLOAT_EQ(den.normal_phi(),  128.0F);
    EXPECT_FLOAT_EQ(den.temporal_alpha(), 0.2F);
    EXPECT_EQ(den.viewport_width(),  kW);
    EXPECT_EQ(den.viewport_height(), kH);

    // Convenience knob overload: should hot-patch the three knobs without
    // tearing down the pipelines.
    auto k = den.configure(3, 2.5F, 64.0F);
    ASSERT_TRUE(k.has_value()) << "knob configure failed: " << k.error().message;
    EXPECT_TRUE(den.is_ready());
    EXPECT_EQ(den.filter_iterations(), 3U);
    EXPECT_FLOAT_EQ(den.depth_phi(),  2.5F);
    EXPECT_FLOAT_EQ(den.normal_phi(), 64.0F);

    // Negative cases: zero iterations + zero viewport rejected.
    EXPECT_FALSE(den.configure(0, 1.0F, 1.0F).has_value());

    cd::restir_di::SvgfDenoiser bad;
    cd::restir_di::SvgfDenoiserConfig bad_cfg {};
    bad_cfg.viewport_width  = 0U;
    bad_cfg.viewport_height = 16U;
    EXPECT_FALSE(bad.configure(*dev, bad_cfg).has_value());

    den.shutdown();
    EXPECT_FALSE(den.is_ready());
    den.shutdown();  // idempotent
    EXPECT_FALSE(den.is_ready());
}

TEST(RestirDiSvgfDenoiser, ExecuteWithStubTexturesNoValidationErrors)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    constexpr std::uint32_t kW = 64U;
    constexpr std::uint32_t kH = 48U;

    cd::restir_di::SvgfDenoiser den;
    cd::restir_di::SvgfDenoiserConfig cfg {};
    cfg.viewport_width    = kW;
    cfg.viewport_height   = kH;
    cfg.filter_iterations = 3U;     // smaller chain to keep the smoke fast
    cfg.depth_phi         = 1.0F;
    cfg.normal_phi        = 128.0F;
    cfg.temporal_alpha    = 0.2F;

    auto cr = den.configure(*dev, cfg);
    ASSERT_TRUE(cr.has_value()) << "configure failed: " << cr.error().message;

    const auto in_buf  = alloc_reservoir(*dev, kW, kH, "svgf_in");
    const auto out_buf = alloc_reservoir(*dev, kW, kH, "svgf_out");
    ASSERT_TRUE(in_buf.is_valid());
    ASSERT_TRUE(out_buf.is_valid());

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();

    // Sprint-4 skeleton: depth_tex / normal_tex / mesh_id_tex are reserved
    // seam slots; pass null views and the kernel falls back to a
    // reservoir-luminance-only edge stop. The descriptor wiring stays
    // SSBO-only so the Vulkan validation layer raises no missing-binding
    // diagnostics.
    const bool ok = den.execute(*cb,
                                in_buf,
                                {},  // depth_tex
                                {},  // normal_tex
                                {},  // mesh_id_tex
                                out_buf);
    EXPECT_TRUE(ok);
    cb->end();

    dev->submit(*cb);
    dev->wait_idle();

    dev->destroy_buffer(in_buf);
    dev->destroy_buffer(out_buf);

    // No-op execute() once shut down.
    den.shutdown();
    cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    EXPECT_FALSE(den.execute(*cb, {}, {}, {}, {}, {}));
    cb->end();
}

}  // namespace
