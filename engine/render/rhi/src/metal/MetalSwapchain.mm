// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalSwapchain.mm
// phase548 — Metal swapchain real impl (Sprint-1).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (Apple platform).
//
// Sprint-1 scope:
//   * CAMetalLayer is supplied by the caller via SwapchainDesc.window_handle
//     (already attached to an NSView / UIView by the host window layer).
//   * acquire_drawable() pulls the next drawable via [layer nextDrawable].
//   * Present is issued by MetalCommandBufferImpl::submit_internal on the
//     command buffer that recorded the frame (Metal pattern: present is a
//     scheduled operation on the cmd-buf, not a separate device call).
//
// Win11 build gate: this translation unit is excluded from the build by
// CMake (engine/render/rhi/CMakeLists.txt) when CD_RHI_METAL_ENABLED is
// not set, so the .mm file does not need to do anything for Windows.
//
// Future sprints (not in scope here):
//   * MTLPixelFormat selection from cd::rhi::Format (BGRA8Unorm vs sRGB
//     vs HDR10 PQ / scRGB).
//   * Explicit drawable retain cycle for triple-buffering with
//     semaphores (Metal already triple-buffers internally via the layer's
//     maximumDrawableCount).
//   * Resize / out-of-date recovery (CAMetalLayer auto-resizes when the
//     backing view changes, but we don't yet propagate kSwapchainOutOfDate).
// =============================================================================
#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>

#include "MetalInternal.hpp"

namespace cd::rhi::metal::detail
{

namespace
{

// Translate cd::rhi::Format → MTLPixelFormat for the Sprint-1 swapchain
// formats we promise to support. Anything else falls back to BGRA8 sRGB,
// which is the macOS default and what almost every sample uses.
[[nodiscard]] MTLPixelFormat to_mtl_format(Format f) noexcept
{
    switch (f)
    {
    case Format::kBGRA8Unorm: return MTLPixelFormatBGRA8Unorm;
    case Format::kBGRA8Srgb:  return MTLPixelFormatBGRA8Unorm_sRGB;
    case Format::kRGBA8Unorm: return MTLPixelFormatRGBA8Unorm;
    case Format::kRGBA8Srgb:  return MTLPixelFormatRGBA8Unorm_sRGB;
    case Format::kRGBA16Float: return MTLPixelFormatRGBA16Float;
    default:                  return MTLPixelFormatBGRA8Unorm_sRGB;
    }
}

}  // namespace

MetalSwapchainObj::MetalSwapchainObj(CAMetalLayer* layer, id<MTLDevice> device,
                                     const SwapchainDesc& desc) noexcept
    : layer_(layer)
{
    pixel_format_ = to_mtl_format(desc.format);
    layer_.device      = device;
    layer_.pixelFormat = pixel_format_;
    // framebufferOnly skips read-back paths — fine for Sprint-1 since the
    // swapchain image is only ever a render target.
    layer_.framebufferOnly = YES;

    // CGSize for the layer's drawableSize. The caller's Extent2D is in
    // logical pixels; macOS picks the backing-scale from the NSView the
    // layer is attached to. We pass through unchanged for Sprint-1; HiDPI
    // handling lives in the platform window layer.
    if (desc.extent.width > 0 && desc.extent.height > 0)
    {
        layer_.drawableSize = CGSizeMake(static_cast<CGFloat>(desc.extent.width),
                                         static_cast<CGFloat>(desc.extent.height));
    }

    // displaySyncEnabled gates vsync. macOS 10.13+; on iOS this property
    // exists but is read-only (always synced to the display refresh).
#if defined(__has_builtin) && __has_builtin(__builtin_available)
    if (@available(macOS 10.13, *))
    {
        layer_.displaySyncEnabled = desc.vsync ? YES : NO;
    }
#endif

    // Reflect the Metal default drawable count (3 on macOS as of 10.13.2).
    image_count_ = static_cast<std::uint32_t>(desc.image_count > 0 ? desc.image_count : 3);
}

id<CAMetalDrawable> MetalSwapchainObj::acquire_drawable() noexcept
{
    // nextDrawable blocks up to ~1 second when all drawables are in flight.
    // It can return nil under exceptional conditions (off-screen layer,
    // GPU stall); callers translate nil to kSwapchainOutOfDate.
    id<CAMetalDrawable> d = [layer_ nextDrawable];
    current_ = d;
    return d;
}

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
