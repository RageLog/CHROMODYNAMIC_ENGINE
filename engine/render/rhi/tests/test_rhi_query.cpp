// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_rhi_query.cpp
//
// A-QUERY (Backend-to-100 Wave 3a): GPU query subsystem (timestamp /
// pipeline-statistics / occlusion).
//
// The DeviceFeatures::timestamp_queries / pipeline_statistics_queries flags
// existed but had NO API to use them; this wave added QueryPoolHandle +
// QueryPoolDesc + IDevice::create_query_pool / destroy_query_pool /
// get_query_results and the ICommandBuffer record surface (write_timestamp /
// begin_query / end_query / reset_query_pool) plus per-backend overrides.
//
// WHAT THIS TEST PROVES (Vulkan lavapipe/RTX 3080 + D3D12 WARP):
//   * create a kTimestamp pool(2), reset it, write_timestamp before + after a
//     tiny copy (a real GPU op that takes nonzero time), submit + wait, read
//     results back and assert t1 >= t0 — and (Vulkan) that the ns delta is
//     finite. Honest-SKIP when the device lacks timestamp_queries (it won't on
//     the RTX 3080 / lavapipe / WARP).
//   * the Null reference returns kNotImplemented from create_query_pool (no GPU
//     query subsystem) — the documented base default.
//
// Pattern: Arrange / Act / Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace
{

// ---- Null reference ---------------------------------------------------------

TEST(RhiQuery, NullCreateQueryPoolNotImplemented)
{
    cd::rhi::NullDevice dev;
    cd::rhi::QueryPoolDesc qd {};
    qd.type  = cd::rhi::QueryType::kTimestamp;
    qd.count = 2;
    const auto r = dev.create_query_pool(qd);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kNotImplemented));
}

// ---- GPU timestamp round-trip (shared by Vulkan + D3D12) --------------------
//
// kTimestamp pool(2). reset → write_timestamp(0) → a real GPU copy → write_-
// timestamp(1) → submit + wait → get_query_results. The copy is between two
// GPU buffers so the second timestamp lands strictly after the first.
//
// `expect_finite_ns` is true on Vulkan (the ns scaling is exact via the
// device timestamp period); D3D12 also returns ns but the assertion focuses on
// the monotonic ordering which is the cross-backend invariant.

void run_timestamp_roundtrip(cd::rhi::IDevice& dev, bool expect_finite_ns)
{
    if (!dev.features().timestamp_queries)
        GTEST_SKIP() << "device does not expose timestamp_queries";

    // Arrange: a 2-slot timestamp pool.
    cd::rhi::QueryPoolDesc qd {};
    qd.type  = cd::rhi::QueryType::kTimestamp;
    qd.count = 2;
    auto pool_r = dev.create_query_pool(qd);
    ASSERT_TRUE(pool_r.has_value())
        << std::string(pool_r.error().message.begin(), pool_r.error().message.end());
    const auto pool = *pool_r;

    // Two GPU-local buffers for a copy that takes nonzero GPU time.
    constexpr std::uint64_t kCopyBytes = 4096;
    cd::rhi::BufferDesc bd {};
    bd.size   = kCopyBytes;
    bd.usage  = cd::rhi::BufferUsage::kTransferSrc | cd::rhi::BufferUsage::kTransferDst;
    bd.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto src_r = dev.create_buffer(bd);
    auto dst_r = dev.create_buffer(bd);
    ASSERT_TRUE(src_r.has_value());
    ASSERT_TRUE(dst_r.has_value());
    const auto src = *src_r;
    const auto dst = *dst_r;

    auto fence_r = dev.create_fence(false);
    ASSERT_TRUE(fence_r.has_value());
    const auto fence = *fence_r;

    // Act: reset → t0 → copy → t1.
    auto cmd = dev.create_command_buffer();
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    cmd->reset_query_pool(pool, 0, 2);
    cmd->write_timestamp(pool, 0);
    std::array<cd::rhi::BufferCopyRegion, 1> regions {
        cd::rhi::BufferCopyRegion { .src_offset = 0, .dst_offset = 0, .size = kCopyBytes }
    };
    cmd->copy_buffer(src, dst, regions);
    cmd->write_timestamp(pool, 1);
    cmd->end();

    cd::rhi::ICommandBuffer* cbs[] = { cmd.get() };
    cd::rhi::SubmitDesc sd {};
    sd.command_buffers = std::span<cd::rhi::ICommandBuffer* const>(cbs, 1);
    sd.signal_fence    = fence;
    const auto sub = dev.submit(sd);
    ASSERT_TRUE(sub.has_value())
        << std::string(sub.error().message.begin(), sub.error().message.end());
    const auto wait = dev.wait_for_fence(fence, ~std::uint64_t { 0 });
    ASSERT_TRUE(wait.has_value());
    dev.wait_idle();

    // Assert: t1 >= t0 (monotonic GPU clock), and the values are nonzero.
    std::array<std::uint64_t, 2> results { 0, 0 };
    const auto got = dev.get_query_results(pool, 0, 2, std::span<std::uint64_t> { results });
    ASSERT_TRUE(got.has_value())
        << std::string(got.error().message.begin(), got.error().message.end());

    EXPECT_GE(results[1], results[0]) << "timestamp t1 must be >= t0";
    // At least one timestamp should be nonzero — a real GPU clock advanced.
    EXPECT_TRUE(results[0] != 0u || results[1] != 0u) << "both timestamps zero";

    if (expect_finite_ns)
    {
        // Vulkan returns NANOSECONDS (period-scaled). The delta is a finite
        // (possibly zero on a very fast software rasterizer) nanosecond count;
        // assert it is representable (no overflow / garbage).
        const std::uint64_t delta_ns = results[1] - results[0];
        EXPECT_LT(delta_ns, std::uint64_t { 60 } * 1'000'000'000u)
            << "ns delta unreasonably large (" << delta_ns << " ns)";
    }

    dev.destroy_fence(fence);
    dev.destroy_buffer(dst);
    dev.destroy_buffer(src);
    dev.destroy_query_pool(pool);
}

// Negative: get_query_results on an unknown pool → kInvalidArgument.
void run_query_results_bad_handle(cd::rhi::IDevice& dev)
{
    if (!dev.features().timestamp_queries)
        GTEST_SKIP() << "device does not expose timestamp_queries";
    cd::rhi::QueryPoolHandle bad { 0xDEAD, 0 };
    std::array<std::uint64_t, 1> out { 0 };
    const auto r = dev.get_query_results(bad, 0, 1, std::span<std::uint64_t> { out });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));
}

TEST(RhiQuery, VulkanTimestampRoundTrip)
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    run_timestamp_roundtrip(**dev_r, /*expect_finite_ns=*/true);
}

TEST(RhiQuery, VulkanQueryResultsBadHandle)
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto dev_r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no Vulkan ICD available on this host";
    run_query_results_bad_handle(**dev_r);
}

#if defined(_WIN32)
TEST(RhiQuery, D3D12TimestampRoundTrip)
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto dev_r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_timestamp_roundtrip(**dev_r, /*expect_finite_ns=*/false);
}

TEST(RhiQuery, D3D12QueryResultsBadHandle)
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;
    auto dev_r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!dev_r.has_value())
        GTEST_SKIP() << "no D3D12 adapter available on this host";
    run_query_results_bad_handle(**dev_r);
}
#endif

}  // namespace
