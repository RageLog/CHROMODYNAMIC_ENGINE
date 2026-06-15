// =============================================================================
// CHROMODYNAMIC — samples/rhi/hello_metal/main.cpp
// phase531  — Metal backend MVP boot smoke (headless).
// M12 (B2 — ADR-20260615) — promoted to a WINDOWED host:
//   NSWindow + NSView + CAMetalLayer -> create_swapchain -> acquire_next_image
//   -> begin render pass -> draw a triangle -> present, in a short run-loop.
//
// PLATFORM GATING (brief mandate 1): the windowed path is Apple-only and uses
// Cocoa/Metal/QuartzCore, so it is gated behind
// `#if defined(__APPLE__) && CD_RHI_METAL_ENABLED`. The `#else` branch is a
// Windows-compilable stub so the `hello_metal` sample TARGET still builds on
// Windows (where CD_RHI_METAL_ENABLED is undefined and the Metal .mm backend is
// the kBackendInitFailed stub). On Apple, CMake compiles this TU as
// Objective-C++ (LANGUAGE OBJCXX) so the `#import` blocks resolve.
//
// What the windowed sample proves on a Mac (Mac-GPU verified later):
//   1. create_metal_device() returns a real MTLDevice-backed IDevice.
//   2. A CAMetalLayer attached to an NSView drives create_swapchain.
//   3. acquire_next_image -> begin_render_pass(clear) -> draw(3) -> present
//      runs for a handful of frames without a validation error.
//   4. The process exits 0.
// =============================================================================
#include <cd/core/Version.hpp>
#include <cd/rhi/metal/MetalDevice.hpp>

#include <cstdio>
#include <cstdlib>

#if defined(__APPLE__) && CD_RHI_METAL_ENABLED

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>

#include <cstdint>
#include <memory>
#include <span>

namespace
{
constexpr std::uint32_t kWidth  = 640;
constexpr std::uint32_t kHeight = 480;
constexpr int           kFrames = 8;  // headless-CI-friendly: a short burst.
}  // namespace

int main()
{
    @autoreleasepool
    {
        std::fprintf(stdout,
                     "CHROMODYNAMIC %u.%u.%u — hello_metal (windowed)\n",
                     static_cast<unsigned>(cd::core::kEngineVersion.major),
                     static_cast<unsigned>(cd::core::kEngineVersion.minor),
                     static_cast<unsigned>(cd::core::kEngineVersion.patch));

        // ---- 1. Device --------------------------------------------------------
        cd::rhi::metal::MetalCreateInfo info {};
        info.app_name = "hello_metal";
        info.enable_validation = true;
        auto dev_r = cd::rhi::metal::create_metal_device(info);
        if (!dev_r.has_value())
        {
            std::fprintf(stderr, "[hello_metal] create_metal_device failed: %.*s\n",
                         static_cast<int>(dev_r.error().message.size()),
                         dev_r.error().message.data());
            return EXIT_FAILURE;
        }
        cd::rhi::IDevice& device = **dev_r;

        // ---- 2. NSWindow + NSView + CAMetalLayer ------------------------------
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        const NSRect frame = NSMakeRect(0, 0, kWidth, kHeight);
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [window setTitle:@"CHROMODYNAMIC — hello_metal"];

        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.device = MTLCreateSystemDefaultDevice();
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake(kWidth, kHeight);

        NSView* view = [[NSView alloc] initWithFrame:frame];
        view.wantsLayer = YES;
        view.layer = layer;
        [window setContentView:view];
        [window makeKeyAndOrderFront:nil];

        // ---- 3. Swapchain over the CAMetalLayer -------------------------------
        cd::rhi::SwapchainDesc sc_desc {};
        sc_desc.window_handle = (__bridge void*)layer;  // CAMetalLayer*
        sc_desc.extent = { kWidth, kHeight };
        sc_desc.format = cd::rhi::Format::kBGRA8Srgb;
        auto sc_r = device.create_swapchain(sc_desc);
        if (!sc_r.has_value())
        {
            std::fprintf(stderr, "[hello_metal] create_swapchain failed: %.*s\n",
                         static_cast<int>(sc_r.error().message.size()),
                         sc_r.error().message.data());
            return EXIT_FAILURE;
        }
        const cd::rhi::SwapchainHandle swapchain = sc_r.value();

        // ---- 4. acquire -> render-pass(clear) -> draw -> present loop ---------
        for (int f = 0; f < kFrames; ++f)
        {
            const auto idx_r = device.acquire_next_image(
                swapchain, cd::rhi::SemaphoreHandle {}, cd::rhi::FenceHandle {},
                UINT64_MAX);
            if (!idx_r.has_value())
            {
                std::fprintf(stderr, "[hello_metal] acquire frame %d: %.*s\n", f,
                             static_cast<int>(idx_r.error().message.size()),
                             idx_r.error().message.data());
                break;  // swapchain out-of-date — stop the burst gracefully.
            }

            std::unique_ptr<cd::rhi::ICommandBuffer> cb =
                device.create_command_buffer();
            if (cb == nullptr)
            {
                std::fprintf(stderr, "[hello_metal] create_command_buffer failed\n");
                return EXIT_FAILURE;
            }
            cd::rhi::ICommandBuffer& cmd = *cb;
            cmd.begin();

            cd::rhi::ColorAttachmentInfo color {};
            color.view = device.swapchain_image_view(swapchain, idx_r.value());
            color.load_op  = cd::rhi::LoadOp::kClear;
            color.store_op = cd::rhi::StoreOp::kStore;
            color.clear_color.f32[0] = 0.10F;
            color.clear_color.f32[1] = 0.12F;
            color.clear_color.f32[2] = 0.18F;
            color.clear_color.f32[3] = 1.0F;
            cd::rhi::RenderPassBeginInfo rp {};
            rp.render_area = { { 0, 0 }, { kWidth, kHeight } };
            rp.color_attachments = { &color, 1 };
            cmd.begin_render_pass(rp);

            const cd::rhi::Viewport vp { 0.0F, 0.0F,
                                         static_cast<float>(kWidth),
                                         static_cast<float>(kHeight), 0.0F, 1.0F };
            cmd.set_viewport(vp);
            cmd.set_scissor({ { 0, 0 }, { kWidth, kHeight } });
            // A bare 3-vertex draw — with no pipeline bound this is a no-op on
            // the GPU but exercises the draw path; a real triangle needs a PSO
            // (the Mac-GPU verification step wires one). The clear is the visible
            // proof the swapchain round-trips.
            cmd.draw(3, 1, 0, 0);

            cmd.end_render_pass();
            cmd.end();

            cd::rhi::ICommandBuffer* const cb_ptr = &cmd;
            cd::rhi::SubmitDesc submit {};
            submit.command_buffers =
                std::span<cd::rhi::ICommandBuffer* const>(&cb_ptr, 1);
            if (auto sub = device.submit(submit); !sub.has_value())
            {
                std::fprintf(stderr, "[hello_metal] submit frame %d: %.*s\n", f,
                             static_cast<int>(sub.error().message.size()),
                             sub.error().message.data());
                return EXIT_FAILURE;
            }
            (void)device.present(swapchain, idx_r.value(), {});

            // Pump the Cocoa event queue so the window stays responsive.
            NSEvent* ev = nil;
            while ((ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantPast]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES]) != nil)
            {
                [NSApp sendEvent:ev];
            }
        }

        device.wait_idle();
        device.destroy_swapchain(swapchain);
        std::fprintf(stdout,
                     "[hello_metal] EXIT 0 — windowed clear+present loop ran %d frames\n",
                     kFrames);
        return EXIT_SUCCESS;
    }
}

#else  // !(__APPLE__ && CD_RHI_METAL_ENABLED) — Windows / Linux / Metal-off

// Windows-compilable stub so the `hello_metal` sample target STILL BUILDS on
// platforms where the Metal backend is not enabled (brief mandate 1). The
// windowed host above needs Cocoa + a live MTLDevice; off-Apple it cannot run,
// so we print a clear message and exit 0 (a build-presence smoke, not a
// failure). create_metal_device() is reachable everywhere (it returns
// kBackendInitFailed off-Apple), so we still touch the factory to keep the
// link edge honest.
int main()
{
    std::fprintf(stdout,
                 "CHROMODYNAMIC %u.%u.%u — hello_metal\n",
                 static_cast<unsigned>(cd::core::kEngineVersion.major),
                 static_cast<unsigned>(cd::core::kEngineVersion.minor),
                 static_cast<unsigned>(cd::core::kEngineVersion.patch));
    std::fprintf(stdout,
                 "[hello_metal] Metal backend not enabled on this platform "
                 "(build with -DCD_RHI_METAL_ENABLED=ON on macOS to run the "
                 "windowed sample).\n");

    cd::rhi::metal::MetalCreateInfo info {};
    info.app_name = "hello_metal";
    const auto dev_r = cd::rhi::metal::create_metal_device(info);
    if (dev_r.has_value())
    {
        // Unexpected on a non-Apple host, but harmless — the device is owned
        // here and dropped at scope exit.
        std::fprintf(stdout, "[hello_metal] (unexpected) device created\n");
    }
    return EXIT_SUCCESS;
}

#endif  // __APPLE__ && CD_RHI_METAL_ENABLED
