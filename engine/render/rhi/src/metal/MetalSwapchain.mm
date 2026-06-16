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
#import <CoreGraphics/CGColorSpace.h>
#include <TargetConditionals.h>

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

// M-HDR-EDR (Backend-to-100 Wave 4a): does this colour space request HDR
// (extended-dynamic-range) presentation? Both HDR10 PQ and scRGB-linear drive
// the layer's EDR path; SDR sRGB stays on the standard 0..1 path.
[[nodiscard]] bool is_hdr_colour_space(ColorSpace cs) noexcept
{
    return cs == ColorSpace::kHdr10St2084 || cs == ColorSpace::kScrgbLinear;
}

// M-HDR-EDR (Backend-to-100 Wave 4a): map the engine ColorSpace to the
// CGColorSpace the CAMetalLayer.colorspace property understands. Mirrors the
// Vulkan VkColorSpaceKHR / D3D12 DXGI_COLOR_SPACE_TYPE selection 1:1:
//   kSrgbNonlinear → kCGColorSpaceSRGB                (sRGB / SDR, BT.709 g2.2)
//   kHdr10St2084   → kCGColorSpaceITUR_2100_PQ        (HDR10 PQ; ST.2084, BT.2020)
//   kScrgbLinear   → kCGColorSpaceExtendedLinearSRGB  (scRGB FP16 linear, BT.709)
//
// OWNERSHIP: CGColorSpaceCreateWithName returns a +1-retained CGColorSpaceRef
// (Core Foundation create-rule; ARC does NOT manage CF types). The caller MUST
// CGColorSpaceRelease the returned reference after handing it to the layer
// (CAMetalLayer.colorspace retains its own copy). Returns nullptr if the OS
// lacks the requested colour-space name (caller falls back to SDR).
//
// @available-gated by the CALLER: kCGColorSpaceITUR_2100_PQ is macOS 10.15.4+ /
// iOS 13.4+; kCGColorSpaceExtendedLinearSRGB is macOS 10.12+ / iOS 10+. The
// colorspace PROPERTY itself is macOS 10.12+ (no iOS equivalent — CAMetalLayer
// on iOS infers the colour space from the pixel format + EDR flags), so the
// whole helper is compiled only on macOS (TARGET_OS_OSX) — the lone call site
// lives under the same guard.
#if TARGET_OS_OSX
[[nodiscard]] CGColorSpaceRef
create_cg_color_space(ColorSpace cs) noexcept API_AVAILABLE(macos(10.15.4))
{
    CFStringRef name = kCGColorSpaceSRGB;
    switch (cs)
    {
    case ColorSpace::kHdr10St2084: name = kCGColorSpaceITUR_2100_PQ;       break;
    case ColorSpace::kScrgbLinear: name = kCGColorSpaceExtendedLinearSRGB; break;
    case ColorSpace::kSrgbNonlinear:
    default:                       name = kCGColorSpaceSRGB;               break;
    }
    return CGColorSpaceCreateWithName(name);
}
#endif  // TARGET_OS_OSX

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

    // M-HDR-EDR (Backend-to-100 Wave 4a): honour desc.colour_space. The
    // historical Sprint-1 path created the layer and NEVER read colour_space nor
    // touched wantsExtendedDynamicRange/colorspace — an HDR10 / scRGB request was
    // silently presented as SDR (Vulkan picks a matching VkColorSpaceKHR at
    // surface-format time; D3D12 calls SetColorSpace1 — D-HDR-SWAPCHAIN Wave 1).
    // Mirror that here:
    //   * wantsExtendedDynamicRange = YES for an HDR colour space lets Metal
    //     present values > 1.0 to an EDR-capable display (macOS 10.11+);
    //   * colorspace tags the drawable contents so the compositor tone-maps PQ /
    //     scRGB correctly (macOS 10.12+).
    // SDR sRGB takes the fallback branch: EDR off + the sRGB CGColorSpace, which
    // is byte-identical in presented output to the pre-Wave-4a path (the layer's
    // default colour space already matches an sRGB pixel format).
    //
    // NOTE: an HDR colour space additionally requires a float swapchain pixel
    // format (RGBA16Float) to actually carry > 1.0 / wide-gamut values — the
    // caller selects that via SwapchainDesc.format (to_mtl_format already maps
    // kRGBA16Float). We do NOT silently override the caller's format here, to
    // keep the colour-space and pixel-format choices independent + explicit,
    // matching the Vulkan/D3D12 backends which also leave format selection to
    // the caller's desc.format.
    is_hdr_ = is_hdr_colour_space(desc.colour_space);
    // wantsExtendedDynamicRange + colorspace are macOS-ONLY CAMetalLayer
    // properties (the iOS layer infers HDR from the pixel format + the display's
    // current EDR headroom, so there is nothing to set there). TARGET_OS_OSX
    // is the compile-time platform gate; @available is the runtime OS-version
    // gate nested inside it. Both are required: the property must EXIST in the
    // SDK (TARGET_OS_OSX) AND the running OS must be new enough (@available).
#if TARGET_OS_OSX
#if defined(__has_builtin) && __has_builtin(__builtin_available)
    if (@available(macOS 10.11, *))
    {
        layer_.wantsExtendedDynamicRange = is_hdr_ ? YES : NO;
    }
    if (@available(macOS 10.15.4, *))
    {
        // create_cg_color_space returns a +1 CGColorSpaceRef (CF create-rule,
        // NOT ARC-managed). Assigning to layer_.colorspace makes the layer
        // retain its own reference; we must release ours afterwards to avoid a
        // leak. nullptr (unsupported name) leaves the layer's default colour
        // space, the safe SDR fallback.
        CGColorSpaceRef cs = create_cg_color_space(desc.colour_space);
        if (cs != nullptr)
        {
            layer_.colorspace = cs;
            CGColorSpaceRelease(cs);
        }
    }
#endif
#endif  // TARGET_OS_OSX

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
