// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/tests/test_restir_di_full_svgf.cpp
// Phase 669 / Sprint-5 -- ReSTIR DI FullSvgfPipeline (chained) smoke.
//
// Vulkan-gated end-to-end smoke for cd::restir_di::FullSvgfPipeline -- the
// SVGF (moment + variance + 3x A-trous filter) full pipeline wrapper.
// Mirrors the Sprint-4 SvgfDenoiser gating: GTEST_SKIPs when no Vulkan ICD
// is available so CI machines without a GPU stay green.
//
// Cases:
//   1. configure(...) round-trip: zero viewport rejected; valid viewport
//      produces a ready pipeline with the fixed 3-iteration count.
//   2. End-to-end variance-reduction smoke: a 32x32 input reservoir buffer
//      is filled with a randomly-noisy luminance pattern (well-defined seed
//      so the smoke is deterministic). The chain runs (null G-buffer
//      views -- reservoir-luminance fallback path). The output reservoir's
//      per-pixel luminance variance must be lower than the input's. This
//      is the *moment* this sprint enables: a graphics dev sees indirect
//      shadows look STABLE (no jitter, no fireflies) when the chain runs.
//
// hello_engine is NOT touched (per FROZEN constraint).
// =============================================================================
#include <cd/restir_di/DispatchPass.hpp>
#include <cd/restir_di/FullSvgfPipeline.hpp>

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
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

// Host-visible, upload/download-capable SSBO. The SVGF kernels see this as
// a `std430 buffer DiReservoir[]` -- the byte layout mirrors
// `cd::restir_di::GpuReservoir` (32B per pixel).
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

// Compute the per-pixel reservoir "final" luminance the GLSL kernels use:
//   luma = dot(radiance, vec3(0.2126, 0.7152, 0.0722)) * weight_sum / (M * target_pdf)
// when M > 0 && target_pdf > 0, else 0. Mirrors `reservoir_final_weight()`
// in kRestirSvgfFilterCS.
double sample_final_luma(const cd::restir_di::GpuReservoir& r) noexcept
{
    if (r.M == 0U || r.target_pdf <= 0.0F)
        return 0.0;
    const double luma = 0.2126 * static_cast<double>(r.radiance_x)
                      + 0.7152 * static_cast<double>(r.radiance_y)
                      + 0.0722 * static_cast<double>(r.radiance_z);
    const double final_w =
        static_cast<double>(r.weight_sum)
      / (static_cast<double>(r.M) * static_cast<double>(r.target_pdf));
    return luma * final_w;
}

double luminance_variance(const std::vector<cd::restir_di::GpuReservoir>& r)
{
    if (r.empty())
        return 0.0;
    double sum  = 0.0;
    double sum2 = 0.0;
    for (const auto& s : r)
    {
        const double l = sample_final_luma(s);
        sum  += l;
        sum2 += l * l;
    }
    const double n   = static_cast<double>(r.size());
    const double mu  = sum / n;
    const double var = std::max(sum2 / n - mu * mu, 0.0);
    return var;
}

// Fill an SSBO with a noisy reservoir pattern: each pixel gets a randomly
// jittered radiance around a baseline (so the per-pixel luma varies but
// has a smooth global mean -- exactly the case SVGF is designed to
// denoise). Deterministic via fixed seed so the smoke is reproducible.
std::vector<cd::restir_di::GpuReservoir>
make_noisy_input(std::uint32_t w, std::uint32_t h, std::uint32_t seed)
{
    std::vector<cd::restir_di::GpuReservoir> out;
    out.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));

    std::mt19937                          rng { seed };
    std::uniform_real_distribution<float> jitter { 0.05F, 1.95F };

    for (std::uint32_t y = 0; y < h; ++y)
    {
        for (std::uint32_t x = 0; x < w; ++x)
        {
            const std::size_t idx =
                static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x);

            cd::restir_di::GpuReservoir r {};
            r.light_index = 0U;
            r.target_pdf  = 1.0F;
            // Baseline radiance ~ (0.5, 0.5, 0.5) so per-channel luma is
            // bounded; multiply by a heavily-jittered scalar so neighbours
            // diverge by up to ~40x -- a classic firefly distribution.
            const float k = jitter(rng);
            r.radiance_x  = 0.5F * k;
            r.radiance_y  = 0.5F * k;
            r.radiance_z  = 0.5F * k;
            r.weight_sum  = 1.0F;
            r.M           = 1U;
            r.age         = 0U;
            out[idx] = r;
        }
    }
    return out;
}

// --- Static math (always runs, no Vulkan needed) ----------------------------

TEST(RestirDiFullSvgfPipeline, FilterIterationsIsThree)
{
    // Production default per Schied 2017 Section 5 with separate temporal
    // reuse (which ReSTIR DI provides). Frozen at 3 for the chain.
    EXPECT_EQ(cd::restir_di::FullSvgfPipeline::filter_iterations(), 3U);
    EXPECT_EQ(cd::restir_di::kFullSvgfFilterIterations, 3U);
}

// --- GPU end-to-end (skips on no-Vulkan hosts) ------------------------------

TEST(RestirDiFullSvgfPipeline, ConfigureRoundTripStashesViewport)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::restir_di::FullSvgfPipeline pipe;
    EXPECT_FALSE(pipe.is_ready());

    // Negative: zero viewport rejected.
    EXPECT_FALSE(pipe.configure(*dev, 0U, 32U).has_value());
    EXPECT_FALSE(pipe.configure(*dev, 32U, 0U).has_value());
    EXPECT_FALSE(pipe.is_ready());

    // Positive: 32x32 round trip.
    constexpr std::uint32_t kW = 32U;
    constexpr std::uint32_t kH = 32U;
    auto cr = pipe.configure(*dev, kW, kH);
    ASSERT_TRUE(cr.has_value()) << "configure failed: " << cr.error().message;
    EXPECT_TRUE(pipe.is_ready());
    EXPECT_EQ(pipe.viewport_width(),  kW);
    EXPECT_EQ(pipe.viewport_height(), kH);

    // Idempotent shutdown.
    pipe.shutdown();
    EXPECT_FALSE(pipe.is_ready());
    pipe.shutdown();
    EXPECT_FALSE(pipe.is_ready());

    // No-op execute() once shut down -- still safe to call on a fresh
    // command buffer.
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    EXPECT_FALSE(pipe.execute(*cb, {}, {}, {}, {}, {}));
    cb->end();
}

TEST(RestirDiFullSvgfPipeline, ExecuteSmoothsNoisyInput)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    constexpr std::uint32_t kW = 32U;
    constexpr std::uint32_t kH = 32U;

    cd::restir_di::FullSvgfPipeline pipe;
    auto cr = pipe.configure(*dev, kW, kH);
    ASSERT_TRUE(cr.has_value()) << "configure failed: " << cr.error().message;
    ASSERT_TRUE(pipe.is_ready());

    // Allocate host-visible storage for both ends of the chain. The chain
    // ping-pongs `reservoir_buf` <-> `out_buf`; with 3 iterations the
    // final write lands on `out_buf` (iterations 0+2 -> out, iteration 1
    // -> reservoir). The test reads `out_buf`.
    const auto in_buf  = alloc_host_visible_reservoir(*dev, kW, kH, "full_svgf_in");
    const auto out_buf = alloc_host_visible_reservoir(*dev, kW, kH, "full_svgf_out");
    ASSERT_TRUE(in_buf.is_valid());
    ASSERT_TRUE(out_buf.is_valid());

    // ---- Stage noisy input + zero-clear output --------------------------
    const auto noisy = make_noisy_input(kW, kH, /*seed=*/0xC0FFEEU);
    {
        std::vector<std::byte> bytes(noisy.size()
            * sizeof(cd::restir_di::GpuReservoir));
        std::memcpy(bytes.data(), noisy.data(), bytes.size());
        auto u = dev->upload_buffer(in_buf, 0U, bytes);
        ASSERT_TRUE(u.has_value()) << "input upload failed: " << u.error().message;
    }
    {
        std::vector<std::byte> zeros(noisy.size()
            * sizeof(cd::restir_di::GpuReservoir), std::byte { 0 });
        auto u = dev->upload_buffer(out_buf, 0U, zeros);
        ASSERT_TRUE(u.has_value()) << "output zero-clear failed: " << u.error().message;
    }

    // ---- Run the chain ---------------------------------------------------
    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    const bool ok = pipe.execute(*cb,
                                 in_buf,
                                 /*normal_tex=*/{},
                                 /*depth_tex=*/{},
                                 /*mesh_id_tex=*/{},
                                 out_buf);
    EXPECT_TRUE(ok);
    cb->end();

    dev->submit(*cb);
    dev->wait_idle();

    // ---- Read the output back + compute luminance variance ---------------
    std::vector<cd::restir_di::GpuReservoir> output;
    output.resize(noisy.size());
    {
        std::vector<std::byte> dst(noisy.size()
            * sizeof(cd::restir_di::GpuReservoir));
        auto d = dev->download_buffer(out_buf, 0U, dst);
        ASSERT_TRUE(d.has_value()) << "output download failed: " << d.error().message;
        std::memcpy(output.data(), dst.data(), dst.size());
    }

    const double var_in  = luminance_variance(noisy);
    const double var_out = luminance_variance(output);

    // Sanity: the synthetic input MUST be noisy enough to give the kernel
    // something to smooth; if not, the test setup is broken.
    EXPECT_GT(var_in, 1e-4)
        << "synthetic input has too little variance to exercise SVGF";

    // The core SVGF invariant: the chained moment + variance + 3x A-trous
    // filter MUST reduce per-pixel luminance variance on a noisy input.
    // This is the "no jitter, no fireflies" moment the brief calls out.
    EXPECT_LT(var_out, var_in)
        << "FullSvgfPipeline failed to reduce luma variance:"
        << " in = " << var_in << ", out = " << var_out;

    // ---- Teardown --------------------------------------------------------
    dev->destroy_buffer(in_buf);
    dev->destroy_buffer(out_buf);
}

}  // namespace
