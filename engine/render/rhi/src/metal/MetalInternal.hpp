// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalInternal.hpp
// phase548 — Metal backend Sprint-1 shared internals.
//
// This header is INTERNAL to the cd_rhi_metal target. It is only included
// from the four .mm translation units (MetalDevice.mm, MetalCommandBuffer.mm,
// MetalSwapchain.mm, MetalPipeline.mm) so it can rely on Objective-C++
// (`id<...>`) types and ARC. Never expose this header to consumers.
//
// Sprint-1 surface (phase548) — exactly enough to draw 1 clear+triangle
// frame end-to-end on macOS:
//   * MetalSwapchainObj         — CAMetalLayer + drawable acquire/present
//   * MetalGraphicsPipelineObj  — MTLRenderPipelineState (inline MSL)
//   * MetalCommandBufferImpl    — ICommandBuffer wrapper around
//                                 id<MTLCommandBuffer> + render encoder
//
// Everything else (descriptor sets, depth, multi-pass, compute, RT) lives
// in subsequent sprints; the remaining ~22 kNotImpl calls in MetalDevice.mm
// stay untouched.
// =============================================================================
#pragma once

#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace cd::rhi::metal::detail
{

// Forward declaration — MetalCommandBufferImpl holds a non-owning pointer
// to a MetalDeviceCtx supplied by the device that created it. The concrete
// definition appears further down this header.
class MetalDeviceCtx;

// ---------------------------------------------------------------------------
// MetalSwapchainObj — CAMetalLayer-backed swapchain.
//
// Sprint-1 contract:
//   * window_handle in SwapchainDesc must be a `CAMetalLayer*` (already
//     attached to an NSView / UIView by the platform window layer).
//   * acquire_drawable() returns the current frame's drawable via
//     [layer nextDrawable]. Metal owns the drawable lifecycle; we hold one
//     reference for the lifetime of the frame.
//   * present() schedules [cmdbuf presentDrawable:].
//
// Image count is informational: Metal manages its own pool internally. We
// report `maximumDrawableCount` as the image_count.
// ---------------------------------------------------------------------------
class MetalSwapchainObj final
{
public:
    MetalSwapchainObj(CAMetalLayer* layer, id<MTLDevice> device,
                      const SwapchainDesc& desc) noexcept;
    ~MetalSwapchainObj() = default;
    MetalSwapchainObj(const MetalSwapchainObj&) = delete;
    MetalSwapchainObj& operator=(const MetalSwapchainObj&) = delete;
    MetalSwapchainObj(MetalSwapchainObj&&) = delete;
    MetalSwapchainObj& operator=(MetalSwapchainObj&&) = delete;

    // Acquire the next frame's drawable. Returns nil if the layer cannot
    // provide a drawable (off-screen, minimised, GPU stalled). Caller
    // owns the returned reference under ARC.
    [[nodiscard]] id<CAMetalDrawable> acquire_drawable() noexcept;

    // Bookkeeping for the most recently acquired drawable, so present()
    // can locate it from the swapchain handle alone.
    void set_current_drawable(id<CAMetalDrawable> d) noexcept { current_ = d; }
    [[nodiscard]] id<CAMetalDrawable> current_drawable() const noexcept { return current_; }

    [[nodiscard]] CAMetalLayer* layer() const noexcept { return layer_; }
    [[nodiscard]] std::uint32_t image_count() const noexcept { return image_count_; }
    [[nodiscard]] MTLPixelFormat pixel_format() const noexcept { return pixel_format_; }

private:
    CAMetalLayer*       layer_ { nil };
    id<CAMetalDrawable> current_ { nil };
    std::uint32_t       image_count_ { 0 };
    MTLPixelFormat      pixel_format_ { MTLPixelFormatBGRA8Unorm_sRGB };
};

// ---------------------------------------------------------------------------
// MetalGraphicsPipelineObj — MTLRenderPipelineState wrapper.
//
// Sprint-1 contract:
//   * The pipeline is built from an inline-MSL one-triangle vertex+fragment
//     program (see MetalPipeline.mm). Real shader-module compilation lands
//     in Sprint 2 alongside SPIRV-Cross MSL translation.
//   * Colour attachment 0 format is taken from
//     desc.color_attachment_formats[0] when provided, otherwise it
//     defaults to MTLPixelFormatBGRA8Unorm_sRGB to match the swapchain.
// ---------------------------------------------------------------------------
class MetalGraphicsPipelineObj final
{
public:
    explicit MetalGraphicsPipelineObj(id<MTLRenderPipelineState> pso) noexcept
        : pso_(pso) {}
    ~MetalGraphicsPipelineObj() = default;
    MetalGraphicsPipelineObj(const MetalGraphicsPipelineObj&) = delete;
    MetalGraphicsPipelineObj& operator=(const MetalGraphicsPipelineObj&) = delete;
    MetalGraphicsPipelineObj(MetalGraphicsPipelineObj&&) = delete;
    MetalGraphicsPipelineObj& operator=(MetalGraphicsPipelineObj&&) = delete;

    [[nodiscard]] id<MTLRenderPipelineState> pso() const noexcept { return pso_; }

private:
    id<MTLRenderPipelineState> pso_ { nil };
};

// Build a 1-triangle inline-MSL graphics pipeline (Sprint-1 only).
// Returns nil on failure (caller maps to kResourceCreationFailed).
[[nodiscard]] id<MTLRenderPipelineState>
build_sprint1_triangle_pipeline(id<MTLDevice> device, MTLPixelFormat color_format,
                                std::string* error_out) noexcept;

// ---------------------------------------------------------------------------
// MetalCommandBufferImpl — ICommandBuffer wrapper around
// id<MTLCommandBuffer> + the active id<MTLRenderCommandEncoder>.
//
// Sprint-1 surface:
//   * begin/end                — allocate / commit (commit happens on submit)
//   * begin_render_pass        — build MTLRenderPassDescriptor, open encoder
//   * end_render_pass          — [encoder endEncoding]
//   * bind_graphics_pipeline   — [encoder setRenderPipelineState:]
//   * draw                     — [encoder drawPrimitives:]
//   * set_viewport / set_scissor — direct MTLViewport / MTLScissorRect calls
//   * push_debug_group / pop_debug_group — [encoder pushDebugGroup:]
// Every other ICB method falls back to a silent no-op for Sprint-1; later
// sprints fill those in.
// ---------------------------------------------------------------------------
class MetalCommandBufferImpl final : public ICommandBuffer
{
public:
    explicit MetalCommandBufferImpl(id<MTLCommandQueue> queue,
                                    MetalDeviceCtx* ctx) noexcept;
    ~MetalCommandBufferImpl() override = default;

    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassBeginInfo& info) override;
    void end_render_pass() override;

    void bind_graphics_pipeline(GraphicsPipelineHandle pipeline) override;
    void bind_compute_pipeline(ComputePipelineHandle /*pipeline*/) override {}
    void bind_descriptor_set(std::uint32_t /*set_index*/, DescriptorSetHandle /*set*/) override {}

    void bind_vertex_buffer(std::uint32_t /*binding*/, BufferHandle /*buffer*/,
                            std::uint64_t /*offset*/) override {}
    void bind_index_buffer(BufferHandle /*buffer*/, std::uint64_t /*offset*/,
                           IndexType /*type*/) override {}

    void push_constants(PipelineLayoutHandle /*layout*/, ShaderStage /*stages*/,
                        std::uint32_t /*offset*/, std::uint32_t /*size*/,
                        const void* /*data*/) override {}

    void set_viewport(const Viewport& vp) override;
    void set_scissor(const Rect2D& rect) override;

    void draw(std::uint32_t vertex_count, std::uint32_t instance_count,
              std::uint32_t first_vertex, std::uint32_t first_instance) override;
    void draw_indexed(std::uint32_t /*index_count*/, std::uint32_t /*instance_count*/,
                      std::uint32_t /*first_index*/, std::int32_t /*vertex_offset*/,
                      std::uint32_t /*first_instance*/) override {}
    void dispatch(std::uint32_t /*x*/, std::uint32_t /*y*/, std::uint32_t /*z*/) override {}

    void copy_buffer(BufferHandle /*src*/, BufferHandle /*dst*/,
                     std::span<const BufferCopyRegion> /*regions*/) override {}
    void copy_buffer_to_image(BufferHandle /*src*/, TextureHandle /*dst*/,
                              std::span<const BufferImageCopyRegion> /*regions*/) override {}
    void copy_image_to_buffer(TextureHandle /*src*/, BufferHandle /*dst*/,
                              std::span<const BufferImageCopyRegion> /*regions*/) override {}

    void barrier(std::span<const BufferBarrier> /*bb*/,
                 std::span<const TextureBarrier> /*tb*/) override {}

    void push_debug_group(std::string_view name) override;
    void pop_debug_group() override;

    // Sprint-1 commit/submit wiring. submit() is called by MetalDevice::submit;
    // it commits the underlying MTLCommandBuffer to the queue. If a swapchain
    // drawable was bound (via the active render pass), it is presented here.
    void submit_internal(id<CAMetalDrawable> drawable_to_present) noexcept;

    [[nodiscard]] id<MTLCommandBuffer> mtl_cmd_buf() const noexcept { return cmd_; }

private:
    id<MTLCommandQueue>          queue_ { nil };
    id<MTLCommandBuffer>         cmd_ { nil };
    id<MTLRenderCommandEncoder>  encoder_ { nil };
    MetalDeviceCtx*              ctx_ { nullptr };  // observer, not owning
    // Cached attachment view for the active render pass — used to derive
    // the colour-target texture when no real TextureViewHandle registry
    // exists yet (Sprint-1 ties views to swapchain drawables only).
};

// ---------------------------------------------------------------------------
// MetalDeviceCtx — opaque interface from MetalCommandBuffer back into the
// device for swapchain-image and pipeline-state lookup. Concrete definition
// lives in MetalDevice.mm; here we forward-declare so other TUs see only
// what they need.
// ---------------------------------------------------------------------------
class MetalDeviceCtx
{
public:
    virtual ~MetalDeviceCtx() = default;

    // Look up the pipeline-state object bound by GraphicsPipelineHandle.
    // Returns nil for unknown / invalid handles.
    [[nodiscard]] virtual id<MTLRenderPipelineState>
    lookup_pipeline(GraphicsPipelineHandle h) const noexcept = 0;

    // Look up the drawable's MTLTexture for the colour attachment named by
    // a TextureViewHandle that came from swapchain_image_view(). Returns nil
    // for unknown handles. The drawable itself is set on the swapchain
    // object and must be presented by the cmd-buffer at submit time.
    [[nodiscard]] virtual id<MTLTexture>
    lookup_swapchain_view_texture(TextureViewHandle h) const noexcept = 0;

    // Look up the swapchain that owns view `h`, so the command buffer can
    // present the drawable on submit. nullptr when `h` is not a swapchain
    // view.
    [[nodiscard]] virtual MetalSwapchainObj*
    lookup_swapchain_for_view(TextureViewHandle h) const noexcept = 0;
};

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
