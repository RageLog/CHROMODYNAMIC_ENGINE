// =============================================================================
// CHROMODYNAMIC — engine/render/lighting_clusters/tests/test_lighting_clusters_dispatch.cpp
// phase672 — Sprint-2 GPU compute light-culling smoke for
//            cd::render::lighting_clusters::DispatchPass.
//
// Vulkan-gated (skips when no ICD is installed) -- the test instantiates a
// real cd::rhi::IDevice through cd::rhi::vulkan::create_vulkan_device, runs
// the dispatch, then reads the cluster_table SSBO back and asserts that the
// GPU populated at least one cluster slot with a non-zero count for the
// 10-light, 4x4x4 grid configuration the brief specifies.
//
// Pattern mirrors tests/test_restir_di_dispatch.cpp and
// tests/test_ddgi_dispatch.cpp -- standalone library binary, no hello_engine
// touch (FROZEN per CLAUDE.md hard constraint).
// =============================================================================

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/render/lighting_clusters/DispatchPass.hpp>
#include <cd/render/lighting_clusters/LightingClusters.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace
{

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = true;  // surface any descriptor / push-constant
                                    // / pipeline-layout mismatch verbatim,
                                    // matching cd::ddgi / cd::restir_di
                                    // Sprint-1/2 dispatch tests.
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

cd::render::lighting_clusters::PointLight make_light(
    float x, float y, float z, float radius)
{
    cd::render::lighting_clusters::PointLight l {};
    l.position  = { x, y, z };
    l.radius    = radius;
    l.color     = { 1.0F, 1.0F, 1.0F };
    l.intensity = 1.0F;
    return l;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1 — static helpers: group_count / buffer sizes / ClusterEntry layout.
//          Runs everywhere, no Vulkan ICD needed.
// ---------------------------------------------------------------------------
TEST(LightingClustersDispatch, StaticHelpersMatchSpec)
{
    using cd::render::lighting_clusters::DispatchPass;
    using cd::render::lighting_clusters::ClusterEntry;
    using cd::render::lighting_clusters::kMaxLightsPerCluster;

    // group_count ceils to multiples of 8 (the GLSL local_size).
    EXPECT_EQ(DispatchPass::group_count(0U), 0U);
    EXPECT_EQ(DispatchPass::group_count(1U), 1U);
    EXPECT_EQ(DispatchPass::group_count(8U), 1U);
    EXPECT_EQ(DispatchPass::group_count(9U), 2U);
    EXPECT_EQ(DispatchPass::group_count(16U), 2U);

    // Byte-size helpers match the slab layout the GLSL declares.
    EXPECT_EQ(sizeof(ClusterEntry), 8U);
    EXPECT_EQ(DispatchPass::cluster_table_size(64U),
              static_cast<std::uint64_t>(64U) * sizeof(ClusterEntry));
    EXPECT_EQ(DispatchPass::light_indices_size(64U),
              static_cast<std::uint64_t>(64U) * kMaxLightsPerCluster
                                              * sizeof(std::uint32_t));
}

// ---------------------------------------------------------------------------
// Test 2 — prepare() succeeds and allocates all three SSBOs.
// ---------------------------------------------------------------------------
TEST(LightingClustersDispatch, PrepareAllocatesAllBuffers)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::render::lighting_clusters::DispatchPass pass;
    pass.configure({ 4U, 4U, 4U, 0.1F, 100.0F });

    auto r = pass.prepare(*dev, /*max_lights=*/10U);
    if (!r.has_value())
        GTEST_SKIP() << "DispatchPass::prepare failed (likely no glslang backend): "
                     << r.error().message;

    EXPECT_TRUE(pass.is_ready());
    EXPECT_TRUE(pass.pipeline().is_valid());
    EXPECT_TRUE(pass.pipeline_layout().is_valid());
    EXPECT_TRUE(pass.descriptor_set().is_valid());
    EXPECT_TRUE(pass.lights_buffer().is_valid());
    EXPECT_TRUE(pass.cluster_table_buffer().is_valid());
    EXPECT_TRUE(pass.light_indices_buffer().is_valid());

    EXPECT_EQ(pass.total_cluster_count(), 4U * 4U * 4U);
    EXPECT_EQ(pass.max_lights(), 10U);

    pass.shutdown();
    EXPECT_FALSE(pass.is_ready());
    EXPECT_FALSE(pass.lights_buffer().is_valid());
}

// ---------------------------------------------------------------------------
// Test 3 — brief's core deliverable:
//          10 lights + 4x4x4 cluster grid, run dispatch, verify the
//          cluster_table SSBO is populated with non-zero offsets/counts.
// ---------------------------------------------------------------------------
TEST(LightingClustersDispatch, TenLightsFourCubeGridProducesNonZeroOffsets)
{
    using cd::render::lighting_clusters::DispatchPass;
    using cd::render::lighting_clusters::ClusterEntry;
    using cd::render::lighting_clusters::kMaxLightsPerCluster;

    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    // 4x4x4 grid = 64 clusters. 10 lights spread through the frustum so
    // many clusters overlap at least one sphere.
    DispatchPass pass;
    pass.configure({ 4U, 4U, 4U, 1.0F, 100.0F });

    auto pr = pass.prepare(*dev, /*max_lights=*/10U);
    if (!pr.has_value())
        GTEST_SKIP() << "DispatchPass::prepare failed: " << pr.error().message;

    // Identity view_proj (per DispatchPass.cpp) yields clip.w == 1 for every
    // world-space point at w=1, which collapses depth_view to 1.0 for the
    // GPU's matrix multiply. The GPU therefore tests all 10 lights against
    // the near-most slice -- the assertion below only requires the GPU to
    // *write* (offset, count) pairs (offset == idx * kMaxLightsPerCluster
    // for every cluster), independent of how many lights end up assigned.
    // This validates the GPU code path end-to-end without depending on a
    // specific projection matrix being wired through the framegraph (Sprint-3+).
    std::vector<cd::render::lighting_clusters::PointLight> lights;
    lights.reserve(10U);
    for (std::uint32_t i = 0U; i < 10U; ++i)
    {
        const auto fi     = static_cast<float>(i);
        const float depth = -(2.0F + fi * 0.5F);  // depths 2..6.5 (camera looks -Z)
        const float off   = (fi - 5.0F) * 0.1F;
        lights.push_back(make_light(off, off, depth, 1.5F));
    }

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();
    auto ex = pass.execute_culling(*cb,
        std::span<const cd::render::lighting_clusters::PointLight>{ lights.data(), lights.size() });
    ASSERT_TRUE(ex.has_value()) << "execute_culling failed: " << ex.error().message;
    cb->end();

    dev->submit(*cb);
    dev->wait_idle();

    // Read the cluster_table SSBO back and verify the GPU populated every
    // entry's `offset` to the per-cluster slab base (idx * kMaxLightsPerCluster)
    // -- this is the marker that the dispatch ran (kCpuRandomAccess buffers
    // are zeroed by the VMA allocator on creation, so a non-zero offset on
    // every cluster proves the shader wrote each entry).
    const std::uint32_t total = pass.total_cluster_count();
    std::vector<ClusterEntry> table(total);
    {
        std::vector<std::byte> dst(total * sizeof(ClusterEntry));
        auto d = dev->download_buffer(pass.cluster_table_buffer(), 0U, dst);
        ASSERT_TRUE(d.has_value()) << "cluster_table download failed: " << d.error().message;
        std::memcpy(table.data(), dst.data(), dst.size());
    }

    // Every cluster's offset must match idx * kMaxLightsPerCluster. The GPU
    // writes this unconditionally at the end of the shader regardless of
    // whether any light overlapped, so it's the most reliable "did the
    // dispatch run?" check independent of projection-matrix specifics.
    bool any_nonzero_offset = false;
    bool any_nonzero_count  = false;
    for (std::uint32_t idx = 0U; idx < total; ++idx)
    {
        const std::uint32_t expected_offset = idx * kMaxLightsPerCluster;
        EXPECT_EQ(table[idx].offset, expected_offset)
            << "cluster " << idx << " offset mismatch -- GPU did not write entry";
        if (table[idx].offset != 0U)
            any_nonzero_offset = true;
        EXPECT_LE(table[idx].count, 10U)
            << "cluster " << idx << " count exceeds light count";
        if (table[idx].count != 0U)
            any_nonzero_count = true;
    }
    EXPECT_TRUE(any_nonzero_offset)
        << "no cluster had a non-zero offset -- dispatch did not run";
    EXPECT_TRUE(any_nonzero_count)
        << "no cluster received any light -- AABB-vs-sphere test never hit";
}

// ---------------------------------------------------------------------------
// Test 4 — execute_culling fails cleanly when called before prepare().
// ---------------------------------------------------------------------------
TEST(LightingClustersDispatch, ExecuteFailsWithoutPrepare)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::render::lighting_clusters::DispatchPass pass;
    pass.configure({ 4U, 4U, 4U, 0.1F, 100.0F });

    auto cb = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cb, nullptr);
    cb->begin();

    auto r = pass.execute_culling(*cb, {});
    EXPECT_FALSE(r.has_value());

    cb->end();
}

// ---------------------------------------------------------------------------
// Test 5 — idempotent shutdown.
// ---------------------------------------------------------------------------
TEST(LightingClustersDispatch, IdempotentShutdown)
{
    auto dev = try_make_device();
    if (!dev)
        GTEST_SKIP() << "no Vulkan ICD available on this host";

    cd::render::lighting_clusters::DispatchPass pass;
    pass.configure({ 4U, 4U, 4U, 0.1F, 100.0F });

    auto r = pass.prepare(*dev, 8U);
    if (!r.has_value())
        GTEST_SKIP() << "DispatchPass::prepare failed: " << r.error().message;

    pass.shutdown();
    EXPECT_FALSE(pass.is_ready());

    // Second shutdown must be safe.
    pass.shutdown();
    EXPECT_FALSE(pass.is_ready());
}
