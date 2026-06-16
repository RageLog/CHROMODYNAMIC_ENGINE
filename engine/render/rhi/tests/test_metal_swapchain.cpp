// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/tests/test_metal_swapchain.cpp
//
// Backend-to-100 Wave 4b / C-METAL-TIER2 (docs/METAL_MAC_TESTING.md §3 Tier-2):
//   cd_test_metal_swapchain — offscreen CAMetalLayer create -> acquire_next_image
//   -> clear (render pass on the swapchain image) -> present (M7 nextDrawable
//   lifetime, §5.4). The drawable must survive the full command-buffer lifetime;
//   a missing retain shows up as a completion error or over-release across
//   repeated acquire/present cycles.
//
// PLATFORM GATE (see test_metal_device.cpp for the full rationale): real
// MTLDevice on Apple; skip-stub everywhere else.
//
// A headless test harness has no NSWindow/NSView; the swapchain is created with
// a null window handle (an offscreen CAMetalLayer). When the Metal backend
// requires a real surface for create_swapchain it returns an error and the test
// GTEST_SKIPs honestly rather than failing — the windowed path is covered by the
// §3 windowed-hello_metal Mac sign-off, this test pins the acquire/present LOOP.
//
// Anti-flake: acquire uses a finite timeout (never sleep_for). Pattern:
// Arrange / Act / Assert.
// =============================================================================
#if defined(__APPLE__) && defined(CD_RHI_METAL_ENABLED)

    #include <cd/rhi/Descriptors.hpp>
    #include <cd/rhi/Enums.hpp>
    #include <cd/rhi/Format.hpp>
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

constexpr std::uint32_t kW = 64;
constexpr std::uint32_t kH = 64;
constexpr std::uint64_t kAcquireTimeoutNs = 1'000'000'000ULL;

[[nodiscard]] std::unique_ptr<cd::rhi::IDevice> make_metal_device_or_null()
{
    cd::rhi::metal::MetalCreateInfo ci {};
    ci.enable_validation = false;
    auto r = cd::rhi::metal::create_metal_device(ci);
    return r.has_value() ? std::move(*r) : nullptr;
}

// ---- M7: offscreen acquire -> clear -> present loop -------------------------
TEST(MetalSwapchain, AcquireClearPresentLoop)
{
    auto dev = make_metal_device_or_null();
    if (dev == nullptr)
        GTEST_SKIP() << "no Metal device on this Mac";
    auto& d = *dev;

    // Arrange: an offscreen swapchain (null window -> headless CAMetalLayer).
    cd::rhi::SwapchainDesc scd {};
    scd.window_handle = nullptr;
    scd.extent        = { kW, kH };
    scd.image_count   = 2;
    scd.format        = cd::rhi::Format::kBGRA8Unorm;
    scd.vsync         = false;
    auto sc_r = d.create_swapchain(scd);
    if (!sc_r.has_value())
        GTEST_SKIP() << "Metal backend needs a real surface for create_swapchain "
                        "on this host; the windowed path is the Mac sign-off gate";
    const auto sc = *sc_r;
    ASSERT_GT(d.swapchain_image_count(sc), 0u);

    // Act: run several acquire -> clear -> present cycles. A drawable-lifetime
    // bug (missing retain) surfaces as a failed acquire/present across the loop.
    for (int frame = 0; frame < 4; ++frame)
    {
        auto acquire_r = d.acquire_next_image(sc, /*signal=*/{}, /*fence=*/{},
                                              kAcquireTimeoutNs);
        ASSERT_TRUE(acquire_r.has_value())
            << "acquire_next_image must yield a drawable index on frame " << frame;
        const std::uint32_t idx = *acquire_r;

        const auto view = d.swapchain_image_view(sc, idx);
        ASSERT_TRUE(view.is_valid());

        auto cmd = d.create_command_buffer(cd::rhi::QueueType::kGraphics);
        ASSERT_NE(cmd, nullptr);
        cmd->begin();
        cd::rhi::ColorAttachmentInfo catt {};
        catt.view        = view;
        catt.load_op     = cd::rhi::LoadOp::kClear;
        catt.store_op    = cd::rhi::StoreOp::kStore;
        catt.clear_color = { .f32 = { 0.1F, 0.2F, 0.3F, 1.0F } };
        cd::rhi::RenderPassBeginInfo rp {};
        rp.color_attachments  = std::span<const cd::rhi::ColorAttachmentInfo>(&catt, 1);
        rp.render_area.extent = { kW, kH };
        cmd->begin_render_pass(rp);
        cmd->end_render_pass();
        cmd->end();
        d.submit(*cmd);

        const std::array<cd::rhi::SemaphoreHandle, 0> no_wait {};
        const auto present_r = d.present(sc, idx,
                                         std::span<const cd::rhi::SemaphoreHandle>(no_wait));
        EXPECT_TRUE(present_r.has_value())
            << "present must succeed on frame " << frame
            << " (drawable must be retained for the command-buffer lifetime)";
        d.wait_idle();
    }

    d.destroy_swapchain(sc);
}

}  // namespace

#else  // not (Apple && CD_RHI_METAL_ENABLED)

    #include <gtest/gtest.h>

TEST(MetalSwapchain, SkippedOffApple)
{
    GTEST_SKIP() << "Metal backend disabled on this platform (Apple-only)";
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
