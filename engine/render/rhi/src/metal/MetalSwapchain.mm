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
    // FIX 3 (M7 — ADR-20260615): configure every CAMetalLayer property the
    // present/acquire path depends on, mirroring the Vulkan create_swapchain
    // (surface format + extent + present mode). device + pixelFormat + the
    // colorspace are required for [layer nextDrawable] to hand out a usable
    // drawable; framebufferOnly = YES lets Metal pick the most efficient
    // drawable storage (the swapchain image is only ever a render target);
    // drawableSize sizes the drawable pool.
    pixel_format_ = to_mtl_format(desc.format);
    layer_.device      = device;
    layer_.pixelFormat = pixel_format_;
    // framebufferOnly skips read-back paths — the swapchain image is only ever
    // a render target, so this is both correct and the fastest path.
    layer_.framebufferOnly = YES;

    // CGSize for the layer's drawableSize. The caller's Extent2D is in
    // logical pixels; macOS picks the backing-scale from the NSView the
    // layer is attached to. We pass through unchanged; HiDPI handling lives in
    // the platform window layer.
    if (desc.extent.width > 0 && desc.extent.height > 0)
    {
        width_  = desc.extent.width;
        height_ = desc.extent.height;
        layer_.drawableSize = CGSizeMake(static_cast<CGFloat>(width_),
                                         static_cast<CGFloat>(height_));
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
    // FIX 3 (M7): a zero-area drawableSize (window minimised / not yet sized)
    // makes nextDrawable spin or return nil; treat it as out-of-date up front
    // so the device surfaces kSwapchainOutOfDate WITHOUT blocking on a drawable
    // that will never arrive. Mirrors the Vulkan acquire returning
    // VK_ERROR_OUT_OF_DATE_KHR on a zero-extent surface.
    const CGSize ds = layer_.drawableSize;
    if (ds.width <= 0.0 || ds.height <= 0.0)
    {
        current_ = nil;
        return nil;
    }
    // nextDrawable blocks up to ~1 second when all drawables are in flight.
    // It can return nil under exceptional conditions (off-screen layer,
    // GPU stall); callers translate nil to kSwapchainOutOfDate.
    id<CAMetalDrawable> d = [layer_ nextDrawable];
    current_ = d;
    return d;
}

// FIX 3 (M7 — ADR-20260615): window-resize handler. Updates the layer's
// drawableSize so the next nextDrawable hands out correctly-sized drawables,
// mirroring the Vulkan swapchain-recreate. Rejects a degenerate (zero-area)
// extent so a minimise does not push a drawableSize that would make every
// subsequent acquire fail. Returns true when the size actually changed.
bool MetalSwapchainObj::resize(std::uint32_t width, std::uint32_t height) noexcept
{
    if (width == 0 || height == 0)
    {
        return false;
    }
    if (width == width_ && height == height_)
    {
        return false;
    }
    width_  = width;
    height_ = height;
    layer_.drawableSize = CGSizeMake(static_cast<CGFloat>(width),
                                     static_cast<CGFloat>(height));
    return true;
}

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
