// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_barrier.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_barrier — barrier(BufferBarrier) records without a crash (M5
//   memoryBarrierWithScope / MTLFence path, §5.2) + a fence signal/wait
//   round-trip (the CPU-side completion signal). A submit carrying a buffer
//   barrier must complete and signal its fence within a bounded wait.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): real
// MTLDevice on Apple; skip-stub everywhere else. Compiles + registers on
// Windows, runs on a Mac.
//
// Anti-flake: synchronisation is via wait_for_fence with a finite timeout (never
// sleep_for); a stuck submit fails the test instead of hanging it.
//
// Pattern: Arrange / Act / Assert.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Barriers.hpp>
    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Handles.hpp>
    #include <cd/rhi/ICommandBuffer.hpp>
    #include <cd/rhi/IDevice.hpp>
    #include <cd/rhi/metal/MetalDevice.hpp>

    #include <gtest/gtest.h>

    #include <array>
    #include <cstdint>
    #include <memory>
    #include <span>

namespace
{

// One second is comfortably above any legitimate empty-submit latency; a hung
// submit trips this instead of an INFINITE wait (anti-flake).
constexpr std::uint64_t kFenceTimeoutNs = 1'000'000'000ULL;

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_metal_device_or_null()
{
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}

// ---- M5: a buffer barrier records + the submit fence signals ----------------
TEST(MetalBarrier, BufferBarrierRecordsAndFenceSignals)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    // Arrange: a storage buffer to barrier (UAV -> shader-resource).
    cd::rhi::BufferDesc bd {};
    bd.size   = 256;
    bd.usage  = cd::rhi::BufferUsage::kStorage;
    bd.memory = cd::rhi::MemoryUsage::kGpuOnly;
    auto buf_r = d.create_buffer(bd);
    ASSERT_TRUE(buf_r.has_value());
    const auto buf = *buf_r;

    auto fence_r = d.create_fence(/*signaled=*/false);
    ASSERT_TRUE(fence_r.has_value());
    const auto fence = *fence_r;

    // Act: record a buffer barrier inside a command buffer + submit with a fence.
    auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
    ASSERT_NE(cmd, nullptr);
    cmd->begin();
    cd::rhi::BufferBarrier bb {};
    bb.buffer = buf;
    bb.from   = cd::rhi::ResourceState::kUnorderedAccess;
    bb.to     = cd::rhi::ResourceState::kShaderResource;
    bb.size   = 0;  // whole-buffer
    cmd->barrier(std::span<const cd::rhi::BufferBarrier>(&bb, 1), {});
    cmd->end();

    std::array<cd::rhi::ICommandBuffer*, 1> cmds { cmd.get() };
    cd::rhi::SubmitDesc sd {};
    sd.command_buffers = std::span<cd::rhi::ICommandBuffer* const>(cmds.data(), 1);
    sd.signal_fence    = fence;
    const auto submit_r = d.submit(sd);
    ASSERT_TRUE(submit_r.has_value())
        << "submit carrying a buffer barrier must not fault the device";

    // Assert: the fence signals within a bounded wait (never sleep_for).
    const auto wait_r = d.wait_for_fence(fence, kFenceTimeoutNs);
    EXPECT_TRUE(wait_r.has_value())
        << "the submit fence must signal — a hung barrier submit must not "
           "hang the test (finite timeout)";
    EXPECT_TRUE(d.is_fence_signaled(fence));

    d.destroy_fence(fence);
    d.destroy_buffer(buf);
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalBarrier, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
