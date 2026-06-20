// =============================================================================
// CHROMODYNAMIC — engine/render/cluster/tests/gpu/test_cluster_gpu.cpp
// 80->100 marathon — GPU-gated parity + negative coverage for the cluster
// compute pipeline (cd::cluster::gpu::GpuPipeline).
//
// ADD-ONLY. No compute-shader, froxel, or PBR math is modified. These tests
// drive the EXISTING two-pass count/write pipeline against the CPU reference
// simulator (cd::render::cluster::run_reference_compute) and pin the error
// channels.
//
// Every case keeps the Vulkan device gate: try_make_device() returns nullptr
// when no ICD is present and the test GTEST_SKIPs, so GPU-less CI stays green.
// None of these alter rendered engine output — they exercise the library's own
// compute pipeline standalone (golden-safe).
// =============================================================================

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/cluster/gpu/GpuPipeline.hpp>
#include <cd/render/cluster/ClusterGrid.hpp>
#include <cd/render/cluster/ReferenceCompute.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace
{

using cd::render::cluster::ClusterConfig;
using cd::render::cluster::LightSphere;

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = true;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

[[nodiscard]] std::vector<LightSphere> make_scatter_lights(int n)
{
    std::vector<LightSphere> lights;
    lights.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        LightSphere s;
        s.view_pos = { static_cast<float>((i * 13) % 7) - 3.0F,
                       static_cast<float>((i * 7) % 5) - 2.0F,
                       -2.0F - static_cast<float>(i) * 1.5F };
        s.radius = 0.5F + static_cast<float>(i % 4) * 0.3F;
        lights.push_back(s);
    }
    return lights;
}

}  // namespace

// --- Negative paths (still need a device for create()) ---------------------

TEST(ClusterGpuPipeline, CreateRejectsZeroMaxLights)
{
    auto device = try_make_device();
    if (!device)
        GTEST_SKIP() << "no Vulkan ICD";
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    auto pipe = cd::cluster::gpu::GpuPipeline::create(*device, cfg, 0U);
    ASSERT_FALSE(pipe.has_value());
    EXPECT_EQ(pipe.error().domain, cd::cluster::gpu::cluster_gpu_errors::kDomain);
    EXPECT_EQ(pipe.error().code,
              static_cast<std::uint32_t>(
                  cd::cluster::gpu::cluster_gpu_errors::Code::kInvalidArgument));
}

TEST(ClusterGpuPipeline, RunRejectsMoreLightsThanBudget)
{
    auto device = try_make_device();
    if (!device)
        GTEST_SKIP() << "no Vulkan ICD";
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    auto pipe = cd::cluster::gpu::GpuPipeline::create(*device, cfg, 4U);
    ASSERT_TRUE(pipe.has_value()) << "pipeline create failed";
    const auto lights = make_scatter_lights(5);  // 5 > max_lights 4
    auto out = (*pipe)->run(lights);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().code,
              static_cast<std::uint32_t>(
                  cd::cluster::gpu::cluster_gpu_errors::Code::kInvalidArgument));
}

// --- Config plumbing -------------------------------------------------------

TEST(ClusterGpuPipeline, ConfigAndMaxLightsRoundTrip)
{
    auto device = try_make_device();
    if (!device)
        GTEST_SKIP() << "no Vulkan ICD";
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    auto pipe = cd::cluster::gpu::GpuPipeline::create(*device, cfg, 16U);
    ASSERT_TRUE(pipe.has_value());
    EXPECT_EQ((*pipe)->max_lights(), 16U);
    EXPECT_EQ((*pipe)->config().cells_x, 8U);
    EXPECT_EQ((*pipe)->config().cells_y, 4U);
    EXPECT_EQ((*pipe)->config().cells_z, 8U);
}

// --- Two-pass GPU vs CPU-reference parity ----------------------------------

TEST(ClusterGpuPipeline, EmptyLightListProducesZeroCounts)
{
    auto device = try_make_device();
    if (!device)
        GTEST_SKIP() << "no Vulkan ICD";
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    auto pipe = cd::cluster::gpu::GpuPipeline::create(*device, cfg, 8U);
    ASSERT_TRUE(pipe.has_value());
    auto out = (*pipe)->run({});
    ASSERT_TRUE(out.has_value()) << "run failed";
    EXPECT_TRUE(out->light_indices.empty());
    EXPECT_EQ(out->cluster_offsets.back(), 0U);
    for (const auto c : out->cluster_counts)
        EXPECT_EQ(c, 0U);
}

TEST(ClusterGpuPipeline, SingleLightMatchesReference)
{
    auto device = try_make_device();
    if (!device)
        GTEST_SKIP() << "no Vulkan ICD";
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    auto pipe = cd::cluster::gpu::GpuPipeline::create(*device, cfg, 4U);
    ASSERT_TRUE(pipe.has_value());

    LightSphere s {};
    s.view_pos = { 0.5F, -0.5F, -8.0F };
    s.radius = 2.0F;
    const std::vector<LightSphere> lights { s };

    auto gpu = (*pipe)->run(lights);
    ASSERT_TRUE(gpu.has_value()) << "run failed";
    const auto ref = cd::render::cluster::run_reference_compute(cfg, lights);

    EXPECT_EQ(gpu->cluster_counts, ref.cluster_counts);
    EXPECT_EQ(gpu->cluster_offsets, ref.cluster_offsets);
    EXPECT_EQ(gpu->light_indices, ref.light_indices);
}

TEST(ClusterGpuPipeline, ManyLightsMatchReferenceBitForBit)
{
    auto device = try_make_device();
    if (!device)
        GTEST_SKIP() << "no Vulkan ICD";
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    const auto lights = make_scatter_lights(16);
    auto pipe = cd::cluster::gpu::GpuPipeline::create(
        *device, cfg, static_cast<std::uint32_t>(lights.size()));
    ASSERT_TRUE(pipe.has_value());

    auto gpu = (*pipe)->run(lights);
    ASSERT_TRUE(gpu.has_value()) << "run failed";
    const auto ref = cd::render::cluster::run_reference_compute(cfg, lights);

    // Two-pass count/write parity: counts, offsets, and the packed index
    // stream must match the CPU reference exactly (both iterate per-cluster
    // outer, ascending-light inner).
    EXPECT_EQ(gpu->cluster_counts, ref.cluster_counts);
    EXPECT_EQ(gpu->cluster_offsets, ref.cluster_offsets);
    EXPECT_EQ(gpu->light_indices, ref.light_indices);
}
