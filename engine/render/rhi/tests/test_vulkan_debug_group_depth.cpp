// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_vulkan_debug_group_depth.cpp
//
// V-DBGDEPTH (Backend-to-100 Wave 0a): the Vulkan analog of
// test_d3d12_limits_debug_group.cpp's debug-group half. Until this wave the
// Vulkan command buffer inherited the ICommandBuffer base
// `debug_group_depth() -> 0` while D3D12 tracked the real nesting depth +
// owned a test. This locks the new VulkanCommandBuffer override:
//
//   (a) DEPTH-TRACKING — push/pop move a balanced nesting counter; a nested
//       group reads 2; the matched pops bring it back to 0. An unmatched pop
//       is a safe no-op (the counter clamps at 0). Verifies the getter is no
//       longer the base 0-stub.
//
//   (b) RECYCLE — begin() resets a stale depth left by an unbalanced previous
//       recording (push 2, pop 1) so it cannot leak into the next recording.
//
// Real Vulkan backend; honest GTEST_SKIP when no ICD can build a command
// buffer (a software lavapipe ICD or any hardware adapter suffices — no
// shaders / queue submit needed, the depth counter is recorded host-side and
// is independent of VK_EXT_debug_utils presence). Pattern: Arrange/Act/Assert.
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
#endif

#include <cd/rhi/Enums.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_vulkan_device_or_null()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;  // avoid validation-layer dependency in CI
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

}  // namespace

// ---- (a) Depth tracks balanced push/pop + clamps on unmatched pop -----------
TEST(VulkanDebugGroupDepth, PushPopTracksDepthAndClampsAtZero)
{
    auto dev = make_vulkan_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No Vulkan ICD on this host";

    auto cmd = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);

    cmd->begin();

    // Fresh recording starts at zero (no longer the base 0-stub by accident:
    // the subsequent push/pop assertions prove the counter is live).
    EXPECT_EQ(cmd->debug_group_depth(), 0u);

    cmd->push_debug_group("OuterGroup");
    EXPECT_EQ(cmd->debug_group_depth(), 1u);

    cmd->push_debug_group("InnerGroup");
    EXPECT_EQ(cmd->debug_group_depth(), 2u);

    cmd->pop_debug_group();  // closes InnerGroup
    EXPECT_EQ(cmd->debug_group_depth(), 1u);

    cmd->pop_debug_group();  // closes OuterGroup
    EXPECT_EQ(cmd->debug_group_depth(), 0u);

    // An extra unmatched pop must be a safe no-op — the depth clamps at 0 and
    // no vkCmdEndDebugUtilsLabelEXT is emitted for a label this buffer never
    // opened (otherwise the begin/end pairing would be unbalanced).
    cmd->pop_debug_group();
    EXPECT_EQ(cmd->debug_group_depth(), 0u);

    // An empty (no-name) group must also track normally.
    cmd->push_debug_group("");
    EXPECT_EQ(cmd->debug_group_depth(), 1u);
    cmd->pop_debug_group();
    EXPECT_EQ(cmd->debug_group_depth(), 0u);

    cmd->end();
}

// ---- (b) begin() resets a stale depth from a prior unbalanced recording -----
//
// First recording pushes TWO groups but pops ONE, leaving depth == 1 after
// end(). The command buffer is then recycled (begin() called again). The
// override must reset the counter to 0 so the stale value cannot leak.
TEST(VulkanDebugGroupDepth, RecycledCommandBufferDepthReset)
{
    auto dev = make_vulkan_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "No Vulkan ICD on this host";

    auto cmd = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);

    // ---- First recording: intentionally unbalanced (push 2, pop 1) ----------
    cmd->begin();
    cmd->push_debug_group("Recording1_Outer");
    cmd->push_debug_group("Recording1_Inner");
    cmd->pop_debug_group();
    EXPECT_EQ(cmd->debug_group_depth(), 1u);  // one group still open
    // Intentionally omit the second pop — depth stays 1 through end().
    cmd->end();
    EXPECT_EQ(cmd->debug_group_depth(), 1u);

    // ---- Second recording: begin() MUST reset depth to 0 --------------------
    cmd->begin();
    EXPECT_EQ(cmd->debug_group_depth(), 0u)
        << "begin() did not reset debug_group_depth_ — stale depth from the "
           "previous recording leaks into the new one";

    // A balanced pair in the second recording.
    cmd->push_debug_group("Recording2_Only");
    EXPECT_EQ(cmd->debug_group_depth(), 1u);
    cmd->pop_debug_group();
    EXPECT_EQ(cmd->debug_group_depth(), 0u);

    cmd->end();
}
