// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_rhi_vk_timeline.cpp
//
// C-VK-TIMELINE (Backend-to-100 Wave 3d).
//
// A timeline-semaphore-gated CROSS-QUEUE overlap completes correctly: an
// async-COMPUTE submission is gated on a GRAPHICS submission via a single
// timeline-semaphore value, and the whole chain finishes with the expected
// data + the timeline at its final value.
//
// THE SCENARIO (the SOTA submit-dependency primitive):
//   timeline T starts at 0.
//   submit A (GRAPHICS queue): copy a CPU-seeded source buffer -> an
//       intermediate GPU buffer, then SIGNAL T = 1.
//   submit B (COMPUTE queue):  WAIT T >= 1, then copy the intermediate buffer
//       -> a readback buffer, then SIGNAL T = 2.
//   host: WAIT T >= 2, download the readback buffer, assert it equals the seed.
//
// Because B's queue submission carries a timeline WAIT on the value A signals,
// the driver serialises B after A across the queue boundary WITHOUT a host
// round-trip — exactly the async-compute-gated-on-graphics pattern. If the
// timeline gate were broken (B ran before A's copy landed) the readback would be
// garbage / zero; the data-equality assert catches that. The final
// timeline_semaphore_value() == 2 confirms BOTH signals fired in order.
//
// On a single-universal-queue adapter the COMPUTE queue ALIASES the graphics
// queue (VulkanDevice queue_for_type_ falls back) — the timeline gate is STILL
// the real synchronisation primitive being exercised, so the test stays valid
// (it proves the timeline wait/signal plumbing, not that two physical queues
// exist).
//
// NO sleep_for — the host blocks on wait_timeline_semaphore (event-based).
// Real Vulkan backend (lavapipe/RTX). GTEST_SKIP when no ICD. No shaders.
// Pattern: Arrange / Act / Assert.
// =============================================================================
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/vulkan/VulkanDevice.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_vulkan_or_null()
{
    cd::rhi::vulkan::VulkanCreateInfo info {};
    info.enable_validation = false;
    auto r = cd::rhi::vulkan::create_vulkan_device(info);
    return r.has_value() ? std::move(*r) : nullptr;
}

[[nodiscard]] cd::rhi::BufferHandle
make_buffer(cd::rhi::IDevice& dev, std::uint64_t size, cd::rhi::BufferUsage usage,
            cd::rhi::MemoryUsage mem)
{
    cd::rhi::BufferDesc bd {};
    bd.size = size; bd.usage = usage; bd.memory = mem;
    auto r = dev.create_buffer(bd);
    return r.has_value() ? *r : cd::rhi::BufferHandle {};
}

}  // namespace

TEST(VulkanTimeline, AsyncComputeGatedOnGraphicsCompletesInOrder)
{
    auto dev = make_vulkan_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";

    // ---- Arrange ----
    constexpr std::uint64_t kBytes = 1024;

    // Seed data the chain must transport intact end-to-end.
    std::vector<std::byte> seed(static_cast<std::size_t>(kBytes));
    for (std::size_t i = 0; i < seed.size(); ++i)
        seed[i] = static_cast<std::byte>((i * 7u + 13u) & 0xFFu);

    const auto src = make_buffer(*dev, kBytes,
        cd::rhi::BufferUsage::kTransferSrc, cd::rhi::MemoryUsage::kCpuToGpu);
    const auto mid = make_buffer(*dev, kBytes,
        cd::rhi::BufferUsage::kTransferSrc | cd::rhi::BufferUsage::kTransferDst,
        cd::rhi::MemoryUsage::kGpuOnly);
    const auto dst = make_buffer(*dev, kBytes,
        cd::rhi::BufferUsage::kTransferDst, cd::rhi::MemoryUsage::kGpuToCpu);
    ASSERT_TRUE(src.is_valid() && mid.is_valid() && dst.is_valid());

    {
        const auto up = dev->upload_buffer(src, 0, std::span<const std::byte> { seed });
        ASSERT_TRUE(up.has_value());
    }

    auto tl_r = dev->create_timeline_semaphore(0);
    ASSERT_TRUE(tl_r.has_value());
    const auto timeline = *tl_r;
    ASSERT_EQ(dev->timeline_semaphore_value(timeline), 0u);

    auto fence_r = dev->create_fence(false);
    ASSERT_TRUE(fence_r.has_value());
    const auto fence = *fence_r;

    // ---- Act: submit A (graphics) signals T=1 -----------------------------
    auto cmd_a = dev->create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd_a, nullptr);
    cmd_a->begin();
    {
        const std::array<cd::rhi::BufferCopyRegion, 1> regions {
            cd::rhi::BufferCopyRegion { .src_offset = 0, .dst_offset = 0, .size = kBytes } };
        cmd_a->copy_buffer(src, mid, regions);
    }
    cmd_a->end();

    auto cmd_b = dev->create_command_buffer(cd::rhi::QueueType::kCompute);
    ASSERT_NE(cmd_b, nullptr);
    cmd_b->begin();
    {
        const std::array<cd::rhi::BufferCopyRegion, 1> regions {
            cd::rhi::BufferCopyRegion { .src_offset = 0, .dst_offset = 0, .size = kBytes } };
        cmd_b->copy_buffer(mid, dst, regions);
    }
    cmd_b->end();

    // Submit A first (graphics): one command buffer, signal timeline -> 1.
    {
        cd::rhi::ICommandBuffer* cbs[] = { cmd_a.get() };
        const std::array<cd::rhi::TimelineSemaphoreSubmit, 1> sig {
            cd::rhi::TimelineSemaphoreSubmit { .semaphore = timeline, .value = 1 } };
        cd::rhi::SubmitDesc sd {};
        sd.command_buffers = std::span<cd::rhi::ICommandBuffer* const>(cbs, 1);
        sd.signal_timeline_semaphores = sig;
        const auto r = dev->submit(sd);
        ASSERT_TRUE(r.has_value())
            << std::string(r.error().message.begin(), r.error().message.end());
    }

    // Submit B (compute): WAIT timeline >= 1, do the second copy, SIGNAL -> 2,
    // and signal the CPU fence so we can also prove completion via the fence.
    {
        cd::rhi::ICommandBuffer* cbs[] = { cmd_b.get() };
        const std::array<cd::rhi::TimelineSemaphoreSubmit, 1> wait {
            cd::rhi::TimelineSemaphoreSubmit { .semaphore = timeline, .value = 1 } };
        const std::array<cd::rhi::TimelineSemaphoreSubmit, 1> sig {
            cd::rhi::TimelineSemaphoreSubmit { .semaphore = timeline, .value = 2 } };
        cd::rhi::SubmitDesc sd {};
        sd.command_buffers = std::span<cd::rhi::ICommandBuffer* const>(cbs, 1);
        sd.wait_timeline_semaphores   = wait;
        sd.signal_timeline_semaphores = sig;
        sd.signal_fence               = fence;
        const auto r = dev->submit(sd);
        ASSERT_TRUE(r.has_value())
            << std::string(r.error().message.begin(), r.error().message.end());
    }

    // ---- Host: wait for the timeline to reach 2 (event-based, no sleep) ----
    {
        const auto w = dev->wait_timeline_semaphore(timeline, 2, ~std::uint64_t { 0 });
        ASSERT_TRUE(w.has_value())
            << std::string(w.error().message.begin(), w.error().message.end());
    }
    {
        const auto w = dev->wait_for_fence(fence, ~std::uint64_t { 0 });
        ASSERT_TRUE(w.has_value());
    }
    dev->wait_idle();

    // ---- Assert: timeline reached 2 AND the data transported intact --------
    EXPECT_GE(dev->timeline_semaphore_value(timeline), 2u)
        << "timeline did not reach the final signalled value — the gated chain "
           "did not complete in order.";

    std::vector<std::byte> got(static_cast<std::size_t>(kBytes));
    const auto dl = dev->download_buffer(dst, 0, std::span<std::byte> { got });
    ASSERT_TRUE(dl.has_value());
    EXPECT_EQ(got, seed)
        << "the readback buffer does NOT equal the seed — the compute submit ran "
           "before the graphics copy landed, i.e. the timeline gate failed to "
           "serialise the cross-queue dependency.";

    // ---- Teardown ----
    dev->destroy_fence(fence);
    dev->destroy_timeline_semaphore(timeline);
    dev->destroy_buffer(dst);
    dev->destroy_buffer(mid);
    dev->destroy_buffer(src);
}

// Negative: signalling a timeline to a value <= its current value is a monotonic
// violation -> kInvalidArgument (host-side signal path). Locks the contract that
// the gate values must strictly increase.
TEST(VulkanTimeline, HostSignalMustStrictlyIncrease)
{
    auto dev = make_vulkan_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Vulkan ICD on this host";

    auto tl_r = dev->create_timeline_semaphore(5);
    ASSERT_TRUE(tl_r.has_value());
    const auto tl = *tl_r;

    // Signalling backwards (3 < 5) must be rejected.
    const auto bad = dev->signal_timeline_semaphore(tl, 3);
    EXPECT_FALSE(bad.has_value());
    if (!bad.has_value())
        EXPECT_EQ(bad.error().code,
                  static_cast<std::uint32_t>(cd::rhi::rhi_errors::Code::kInvalidArgument));

    // Forward (7 > 5) is accepted.
    const auto good = dev->signal_timeline_semaphore(tl, 7);
    EXPECT_TRUE(good.has_value());
    EXPECT_GE(dev->timeline_semaphore_value(tl), 7u);

    dev->destroy_timeline_semaphore(tl);
}
