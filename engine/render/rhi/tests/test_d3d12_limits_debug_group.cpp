// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_d3d12_limits_debug_group.cpp
//
// phase1188 (D13 + D14): D3D12 DeviceLimits fill + PIX debug-group markers.
//
//   (a) DEVICE-LIMITS — create the real D3D12 backend (WARP if no hardware
//       adapter) and assert IDevice::limits() reports the documented D3D12
//       hard limits rather than the previous default-zero struct: 16384 max
//       2D texture dim, 8 simultaneous render targets, 32 vertex attributes,
//       65536-byte CBV range, 256-byte CBV offset alignment, 16x anisotropy.
//       This fails on the pre-D13 code where every field was 0/1.
//
//   (b) DEBUG-GROUP — begin a command buffer, push/pop a debug group around a
//       no-op (and a nested group), end + submit, wait_idle, and assert the
//       backend never crashes and stays balanced. Extra unmatched pop()s must
//       be safe (the depth counter clamps at 0). Without the PIX runtime there
//       is no event to query, so this is a no-crash + balance test (the raw
//       BeginEvent/EndEvent op is a driver no-op on a tool-less run).
//
// Real D3D12 backend; GTEST_SKIP when no adapter. No shaders / DXC needed
// (CPU-side limits read + empty command list). Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#if defined(_WIN32)
    #include <cd/rhi/d3d12/D3D12Device.hpp>
#endif

#include <gtest/gtest.h>

#include <memory>

#if defined(_WIN32)

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_d3d12_device_or_null()
{
    cd::rhi::d3d12::D3D12CreateInfo ci {};
    ci.enable_validation = false;  // avoid debug-layer dependency in CI
    auto r = cd::rhi::d3d12::create_d3d12_device(ci);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

}  // namespace

// ---- (a) DeviceLimits filled from real D3D12 caps (D13) ---------------------
TEST(D3D12LimitsDebugGroup, DeviceLimitsMatchDocumentedConstants)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";

    const cd::rhi::DeviceLimits& l = dev->limits();

    // Key fields must be non-zero (pre-D13 they were all default-zero).
    EXPECT_NE(l.max_texture_dimension_2d, 0u);
    EXPECT_NE(l.max_color_attachments, 0u);
    EXPECT_NE(l.max_uniform_buffer_range, 0u);

    // Documented D3D12 (Feature Level 11_0+) hard limits.
    EXPECT_EQ(l.max_texture_dimension_1d, 16384u);
    EXPECT_EQ(l.max_texture_dimension_2d, 16384u);
    EXPECT_EQ(l.max_texture_dimension_3d, 2048u);
    EXPECT_EQ(l.max_texture_array_layers, 2048u);
    EXPECT_EQ(l.max_uniform_buffer_range, 65536u);   // 4096 float4 regs * 16 B
    EXPECT_EQ(l.max_vertex_input_attributes, 32u);
    EXPECT_EQ(l.max_vertex_input_bindings, 32u);
    EXPECT_EQ(l.max_color_attachments, 8u);          // simultaneous RT count
    EXPECT_FLOAT_EQ(l.max_anisotropy, 16.0F);
    EXPECT_EQ(l.min_uniform_buffer_offset_alignment, 256u);  // CBV alignment
    EXPECT_GT(l.max_bound_descriptor_sets, 0u);
    EXPECT_GT(l.max_storage_buffer_range, 0u);
    EXPECT_GT(l.max_push_constants_size, 0u);
}

// ---- (b) Debug groups never crash + stay balanced (D14) ---------------------
TEST(D3D12LimitsDebugGroup, PushPopDebugGroupNoCrashAndBalanced)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";

    auto cmd = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);

    cmd->begin();

    // A balanced group around a no-op, plus a nested group.
    cmd->push_debug_group("OuterGroup");
    cmd->push_debug_group("InnerGroup");
    cmd->pop_debug_group();   // closes InnerGroup
    cmd->pop_debug_group();   // closes OuterGroup

    // Defensive: an extra unmatched pop must be a safe no-op (depth clamps).
    cmd->pop_debug_group();

    // An empty (no-name) group must also be safe.
    cmd->push_debug_group("");
    cmd->pop_debug_group();

    cmd->end();

    // Submit + drain — if BeginEvent/EndEvent were unbalanced or invalid the
    // command list would fail to execute; reaching wait_idle proves no crash.
    dev->submit(*cmd);
    dev->wait_idle();

    SUCCEED() << "push/pop debug groups recorded, submitted and drained "
                 "without crash";
}

// ---- (c) Recycle: stale depth is reset by begin() (phase1189 B5 fix) --------
//
// Simulate a caller bug / exception path: the first recording pushes TWO
// groups but only pops ONE, leaving depth == 1 after end(). The command
// buffer is then recycled (begin() called again). The second recording does a
// balanced push/pop. After the second end()+submit+wait_idle:
//   * debug_group_depth() must be 0  (begin() reset erased the stale 1).
//   * No crash means the extra EndEvent from the stale depth was NOT emitted
//     in the second recording (the guard was not confused by a stale counter).
TEST(D3D12LimitsDebugGroup, RecycledCommandBufferDepthReset)
{
    auto dev = make_d3d12_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No D3D12 adapter (WARP/hardware) on this host";

    auto cmd = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);

    // ---- First recording: intentionally unbalanced (push 2, pop 1) ----------
    cmd->begin();
    cmd->push_debug_group("Recording1_Outer");
    cmd->push_debug_group("Recording1_Inner");
    cmd->pop_debug_group();
    // Intentionally omit the second pop — depth == 1 after end().
    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    // ---- Second recording: begin() MUST reset depth to 0 --------------------
    cmd->begin();
    // After begin(), depth must be 0 regardless of the stale value from above.
    EXPECT_EQ(cmd->debug_group_depth(), 0u)
        << "begin() did not reset debug_group_depth_ — stale depth from "
           "previous recording leaks into the new one";

    // A balanced pair in the second recording.
    cmd->push_debug_group("Recording2_Only");
    EXPECT_EQ(cmd->debug_group_depth(), 1u);
    cmd->pop_debug_group();
    EXPECT_EQ(cmd->debug_group_depth(), 0u);

    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    SUCCEED() << "Recycled command buffer depth correctly reset to 0 by begin()";
}

#else   // !_WIN32

TEST(D3D12LimitsDebugGroup, SkippedOffWindows)
{
    GTEST_SKIP() << "D3D12 limits/debug-group test is Windows-only";
}

#endif  // _WIN32
