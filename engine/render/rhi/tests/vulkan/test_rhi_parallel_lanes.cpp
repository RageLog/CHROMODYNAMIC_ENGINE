// =============================================================================
// CHROMODYNAMIC — test_rhi_parallel_lanes.cpp
// phase1116 (X1-FU-F step 2) — Vulkan parallel-lane recording on a real
// device: secondary command buffers over dynamic rendering, joined via
// vkCmdExecuteCommands, executed on the GPU.
//
// Skips gracefully when no Vulkan ICD is present (same pattern as
// test_rhi_vulkan.cpp).
// =============================================================================
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

std::unique_ptr<cd::rhi::IDevice> try_make_device()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    if (!r.has_value())
        return nullptr;
    return std::move(*r);
}

#define SKIP_IF_NO_VULKAN(dev_var)    \
    auto dev_var = try_make_device(); \
    if (!dev_var)                     \
    GTEST_SKIP() << "no Vulkan ICD available on this host"

/// Offscreen colour target + the begun pass info for it.
struct Offscreen
{
    cd::rhi::TextureHandle tex {};
    cd::rhi::TextureViewHandle view {};
    cd::rhi::ColorAttachmentInfo attachment {};
    cd::rhi::RenderPassBeginInfo pass {};
};

Offscreen make_offscreen(cd::rhi::IDevice& dev, std::uint32_t size)
{
    Offscreen o;
    cd::rhi::TextureDesc td {};
    td.type = cd::rhi::TextureType::k2D;
    td.format = cd::rhi::Format::kRGBA8Unorm;
    td.extent = { size, size, 1 };
    td.usage = cd::rhi::TextureUsage::kColorAttachment |
               cd::rhi::TextureUsage::kTransferSrc;
    o.tex = dev.create_texture(td).value_or(cd::rhi::TextureHandle {});
    cd::rhi::TextureViewDesc vd {};
    vd.texture = o.tex;
    vd.type = cd::rhi::TextureType::k2D;
    o.view = dev.create_texture_view(vd).value_or(cd::rhi::TextureViewHandle {});
    o.attachment.view = o.view;
    o.attachment.load_op = cd::rhi::LoadOp::kClear;
    o.attachment.store_op = cd::rhi::StoreOp::kStore;
    o.pass.color_attachments = { &o.attachment, 1 };
    o.pass.render_area.extent = { size, size };
    return o;
}

// ---------------------------------------------------------------------------
// Lanes record CONCURRENTLY (viewport/scissor/debug groups — legal
// without a bound pipeline), finish() joins them, the primary submits
// and the GPU executes. Success criterion: clean submit + wait_idle
// (validation layers would flag inheritance/contents violations).
// ---------------------------------------------------------------------------
TEST(VulkanParallelLanes, ConcurrentLaneRecordingExecutesOnGpu)
{
    SKIP_IF_NO_VULKAN(dev);
    auto o = make_offscreen(*dev, 64);
    ASSERT_TRUE(o.tex.is_valid());
    ASSERT_TRUE(o.view.is_valid());

    auto cmd = dev->create_command_buffer();
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    // UNDEFINED -> COLOR_ATTACHMENT before the pass.
    cd::rhi::TextureBarrier tb {};
    tb.texture = o.tex;
    tb.from = cd::rhi::ResourceState::kUndefined;
    tb.to   = cd::rhi::ResourceState::kColorAttachment;
    cmd->barrier({}, { &tb, 1 });

    constexpr std::uint32_t kLanes = 3;
    auto rec = cmd->begin_parallel_render_pass(o.pass, kLanes);
    ASSERT_NE(rec, nullptr) << "Vulkan backend must support parallel lanes";
    ASSERT_EQ(rec->lane_count(), kLanes);

    {
        std::vector<std::jthread> workers;
        workers.reserve(kLanes);
        for (std::uint32_t i = 0; i < kLanes; ++i)
        {
            workers.emplace_back(
                [&rec, i]
                {
                    auto& lane = rec->lane(i);
                    lane.push_debug_group("lane" + std::to_string(i));
                    cd::rhi::Viewport vp {};
                    vp.width  = 64.0F;
                    vp.height = 64.0F;
                    lane.set_viewport(vp);
                    cd::rhi::Rect2D sc {};
                    sc.extent = { 64, 64 };
                    lane.set_scissor(sc);
                    lane.pop_debug_group();
                });
        }
    }  // joined

    rec->finish();
    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    // Reuse across frames: a second parallel pass on the same primary
    // must work after begin() resets it.
    cmd->begin();
    cmd->barrier({}, { &tb, 1 });
    auto rec2 = cmd->begin_parallel_render_pass(o.pass, 2);
    ASSERT_NE(rec2, nullptr);
    rec2->lane(0).push_debug_group("second-frame");
    rec2->lane(0).pop_debug_group();
    rec2->finish();
    cmd->end();
    dev->submit(*cmd);
    dev->wait_idle();

    dev->destroy_texture_view(o.view);
    dev->destroy_texture(o.tex);
}

// Serial fallback contract: an unknown attachment view (no format
// bookkeeping) must yield nullptr, NOT a broken recorder.
TEST(VulkanParallelLanes, UnknownAttachmentFallsBackToSerial)
{
    SKIP_IF_NO_VULKAN(dev);
    auto cmd = dev->create_command_buffer();
    ASSERT_NE(cmd, nullptr);
    cmd->begin();

    cd::rhi::ColorAttachmentInfo bad {};
    bad.view = cd::rhi::TextureViewHandle { 0xDEADBEEFu, 1u };
    cd::rhi::RenderPassBeginInfo pass {};
    pass.color_attachments = { &bad, 1 };
    pass.render_area.extent = { 4, 4 };

    auto rec = cmd->begin_parallel_render_pass(pass, 2);
    EXPECT_EQ(rec, nullptr);
    cmd->end();
}

}  // namespace
