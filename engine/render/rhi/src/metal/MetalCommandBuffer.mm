// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalCommandBuffer.mm
// phase548 — Metal command-buffer real impl (Sprint-1).
// phase559 — Sprint-2: push_constants + copy_buffer + copy_buffer_to_image
//            + copy_image_to_buffer (MTLBlitCommandEncoder path).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (Apple platform).
//
// Sprint-1 scope (minimum to draw one clear + triangle frame):
//   * begin()                  — lazy-allocate the underlying MTLCommandBuffer.
//   * end()                    — no-op; commit happens at submit time so we
//                                can chain present on the same cmd-buf.
//   * begin_render_pass / end_render_pass — build a MTLRenderPassDescriptor
//                                from the supplied ColorAttachmentInfo[0],
//                                resolve the swapchain drawable texture via
//                                the device context, open a render encoder.
//   * bind_graphics_pipeline   — [encoder setRenderPipelineState:] via the
//                                pipeline registry on the device context.
//   * draw                     — [encoder drawPrimitives:triangles ...].
//   * set_viewport / set_scissor — direct MTLViewport / MTLScissorRect calls.
//   * push_debug_group / pop_debug_group — pushes onto the active encoder
//                                or the command buffer when no encoder is
//                                open. Both APIs exist since macOS 10.13.
//
// Sprint-2 additions:
//   * push_constants           — setVertexBytes / setFragmentBytes inline-
//                                arg fast path on the active render encoder.
//   * copy_buffer              — MTLBlitCommandEncoder copyFromBuffer:..:.
//   * copy_buffer_to_image     — MTLBlitCommandEncoder copyFromBuffer:..:
//                                toTexture:.. (texture upload path).
//   * copy_image_to_buffer     — Symmetric image -> buffer readback path.
// Encoder transitions: Metal disallows nested encoders on a single cmd-buf,
// so the blit encoder is closed before any render encoder opens (and vice
// versa). Both encoder kinds are closed on submit_internal.
//
// Buffer / texture resolution goes through MetalDeviceCtx::lookup_buffer
// + lookup_texture. In Sprint-2 those return nil because create_buffer /
// create_texture still hand out unbacked stub handles; copy paths
// gracefully skip when the resolution misses, matching Sprint-1's
// render-pass fallback. Sprint 3 lights up real allocation and the
// copies fire end-to-end with no further code changes here.
// =============================================================================
#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include "MetalInternal.hpp"

#include <cd/rhi/Descriptors.hpp>

namespace cd::rhi::metal::detail
{

MetalCommandBufferImpl::MetalCommandBufferImpl(id<MTLCommandQueue> queue,
                                               MetalDeviceCtx* ctx) noexcept
    : queue_(queue), ctx_(ctx)
{
}

void MetalCommandBufferImpl::begin()
{
    // Each begin() opens a fresh cmd-buf; recordings are not re-used.
    cmd_ = [queue_ commandBuffer];
    cmd_.label = @"cd::rhi::metal::MetalCommandBuffer";
}

void MetalCommandBufferImpl::end()
{
    // Metal does not require an explicit end() call; the matching commit
    // happens in submit_internal so present can ride on the same cmd-buf.
}

void MetalCommandBufferImpl::begin_render_pass(const RenderPassBeginInfo& info)
{
    // Sprint-1 supports exactly one colour attachment and no depth.
    // Multi-target and depth land in Sprint 2 once we have a real
    // TextureView registry beyond swapchain-drawable views.
    if (info.color_attachments.empty())
    {
        return;
    }
    // phase559: a blit encoder open from a prior copy_* call must be
    // closed before the render encoder opens — Metal forbids nested
    // encoders on a single cmd-buf.
    close_blit_encoder_if_open();
    const ColorAttachmentInfo& att = info.color_attachments[0];

    id<MTLTexture> tex = (ctx_ != nullptr)
        ? ctx_->lookup_swapchain_view_texture(att.view)
        : nil;
    if (tex == nil)
    {
        // Cannot resolve a render target — skip the pass rather than crash.
        // Sprint-1 only knows how to render into swapchain drawables.
        return;
    }

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = tex;
    rpd.colorAttachments[0].loadAction  =
        (att.load_op == LoadOp::kClear) ? MTLLoadActionClear
      : (att.load_op == LoadOp::kLoad ) ? MTLLoadActionLoad
                                        : MTLLoadActionDontCare;
    rpd.colorAttachments[0].storeAction =
        (att.store_op == StoreOp::kStore) ? MTLStoreActionStore
                                          : MTLStoreActionDontCare;
    rpd.colorAttachments[0].clearColor  = MTLClearColorMake(
        static_cast<double>(att.clear_color.f32[0]),
        static_cast<double>(att.clear_color.f32[1]),
        static_cast<double>(att.clear_color.f32[2]),
        static_cast<double>(att.clear_color.f32[3]));

    encoder_ = [cmd_ renderCommandEncoderWithDescriptor:rpd];
    encoder_.label = @"cd::rhi::metal::RenderEncoder";

    // Default viewport / scissor to the render area so the encoder is
    // immediately usable without an explicit set_viewport call. Callers
    // that need a custom region overwrite this via set_viewport().
    const auto& ext = info.render_area.extent;
    if (ext.width > 0 && ext.height > 0)
    {
        MTLViewport vp {
            .originX = static_cast<double>(info.render_area.offset.x),
            .originY = static_cast<double>(info.render_area.offset.y),
            .width   = static_cast<double>(ext.width),
            .height  = static_cast<double>(ext.height),
            .znear   = 0.0,
            .zfar    = 1.0
        };
        [encoder_ setViewport:vp];

        MTLScissorRect sc {
            .x      = static_cast<NSUInteger>(info.render_area.offset.x < 0 ? 0
                                            : info.render_area.offset.x),
            .y      = static_cast<NSUInteger>(info.render_area.offset.y < 0 ? 0
                                            : info.render_area.offset.y),
            .width  = static_cast<NSUInteger>(ext.width),
            .height = static_cast<NSUInteger>(ext.height)
        };
        [encoder_ setScissorRect:sc];
    }
}

void MetalCommandBufferImpl::end_render_pass()
{
    if (encoder_ != nil)
    {
        [encoder_ endEncoding];
        encoder_ = nil;
    }
}

void MetalCommandBufferImpl::bind_graphics_pipeline(GraphicsPipelineHandle pipeline)
{
    if (encoder_ == nil || ctx_ == nullptr)
    {
        return;
    }
    id<MTLRenderPipelineState> pso = ctx_->lookup_pipeline(pipeline);
    if (pso == nil)
    {
        return;
    }
    [encoder_ setRenderPipelineState:pso];
}

void MetalCommandBufferImpl::set_viewport(const Viewport& vp)
{
    if (encoder_ == nil)
    {
        return;
    }
    MTLViewport mvp {
        .originX = static_cast<double>(vp.x),
        .originY = static_cast<double>(vp.y),
        .width   = static_cast<double>(vp.width),
        .height  = static_cast<double>(vp.height),
        .znear   = static_cast<double>(vp.min_depth),
        .zfar    = static_cast<double>(vp.max_depth)
    };
    [encoder_ setViewport:mvp];
}

void MetalCommandBufferImpl::set_scissor(const Rect2D& rect)
{
    if (encoder_ == nil)
    {
        return;
    }
    MTLScissorRect sc {
        .x      = static_cast<NSUInteger>(rect.offset.x < 0 ? 0 : rect.offset.x),
        .y      = static_cast<NSUInteger>(rect.offset.y < 0 ? 0 : rect.offset.y),
        .width  = static_cast<NSUInteger>(rect.extent.width),
        .height = static_cast<NSUInteger>(rect.extent.height)
    };
    [encoder_ setScissorRect:sc];
}

void MetalCommandBufferImpl::draw(std::uint32_t vertex_count,
                                  std::uint32_t instance_count,
                                  std::uint32_t first_vertex,
                                  std::uint32_t /*first_instance*/)
{
    if (encoder_ == nil || vertex_count == 0)
    {
        return;
    }
    // Sprint-1: only TriangleList topology is supported; the pipeline is
    // hardcoded that way. Sprint 2 reads topology from the bound pipeline.
    [encoder_ drawPrimitives:MTLPrimitiveTypeTriangle
                 vertexStart:static_cast<NSUInteger>(first_vertex)
                 vertexCount:static_cast<NSUInteger>(vertex_count)
               instanceCount:static_cast<NSUInteger>(instance_count == 0 ? 1
                                                                          : instance_count)];
}

void MetalCommandBufferImpl::push_debug_group(std::string_view name)
{
    NSString* ns = [[NSString alloc] initWithBytes:name.data()
                                            length:name.size()
                                          encoding:NSUTF8StringEncoding];
    if (ns == nil)
    {
        return;
    }
    if (encoder_ != nil)
    {
        [encoder_ pushDebugGroup:ns];
    }
    else if (blit_ != nil)
    {
        // phase559: debug groups while a blit-encoder is open ride on it.
        [blit_ pushDebugGroup:ns];
    }
    else if (cmd_ != nil)
    {
        [cmd_ pushDebugGroup:ns];
    }
}

void MetalCommandBufferImpl::pop_debug_group()
{
    if (encoder_ != nil)
    {
        [encoder_ popDebugGroup];
    }
    else if (blit_ != nil)
    {
        [blit_ popDebugGroup];
    }
    else if (cmd_ != nil)
    {
        [cmd_ popDebugGroup];
    }
}

void MetalCommandBufferImpl::submit_internal(id<CAMetalDrawable> drawable_to_present) noexcept
{
    if (cmd_ == nil)
    {
        return;
    }
    // Make sure any in-flight encoders are closed before we commit.
    if (encoder_ != nil)
    {
        [encoder_ endEncoding];
        encoder_ = nil;
    }
    // phase559: also close any pending blit encoder.
    close_blit_encoder_if_open();
    if (drawable_to_present != nil)
    {
        [cmd_ presentDrawable:drawable_to_present];
    }
    [cmd_ commit];
    cmd_ = nil;
}

// ---------------------------------------------------------------------------
// phase559 (Sprint-2) — blit-encoder helpers.
//
// MTLBlitCommandEncoder is opened lazily by ensure_blit_encoder_open() on
// the first copy_* call after begin(). It is closed in three places:
//   1. before begin_render_pass opens a render encoder (Metal forbids
//      nested encoders on a single cmd-buf).
//   2. before submit_internal commits the cmd-buf.
//   3. explicitly via close_blit_encoder_if_open() anywhere we need to
//      transition encoder kinds in the future (compute encoder, RT
//      encoder, …).
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::ensure_blit_encoder_open()
{
    if (cmd_ == nil)
    {
        // No cmd-buf yet — begin() was not called. Sprint-2 silently skips
        // matching Sprint-1's defensive behaviour; a future debug-validation
        // layer can flag this.
        return;
    }
    if (encoder_ != nil)
    {
        // Render encoder is active — close it first. Real callers should
        // do this themselves via end_render_pass(); we defensively unwind
        // so out-of-order calls don't deadlock the cmd-buf.
        [encoder_ endEncoding];
        encoder_ = nil;
    }
    if (blit_ != nil)
    {
        // Already open — nothing to do.
        return;
    }
    blit_ = [cmd_ blitCommandEncoder];
    blit_.label = @"cd::rhi::metal::BlitEncoder";
}

void MetalCommandBufferImpl::close_blit_encoder_if_open() noexcept
{
    if (blit_ != nil)
    {
        [blit_ endEncoding];
        blit_ = nil;
    }
}

// ---------------------------------------------------------------------------
// push_constants — Metal inline-byte fast path.
//
// Metal does not have a native "push constants" concept; the canonical
// replacement for the small (≤4 KB) per-draw constants is
// setVertexBytes:length:atIndex: / setFragmentBytes:length:atIndex:.
// SPIRV-Cross mlsls Vulkan push-constant blocks to `[[buffer(n)]]` where
// `n` is the buffer-argument index — we use `offset` for that index so the
// engine can pre-compute it from the pipeline layout.
//
// `size` is clamped at 4 KB per Metal's documented inline-arg cap; larger
// constants must go through a regular buffer write, which is Sprint-3 work.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::push_constants(PipelineLayoutHandle /*layout*/,
                                            ShaderStage stages,
                                            std::uint32_t offset,
                                            std::uint32_t size,
                                            const void* data)
{
    if (encoder_ == nil || data == nullptr || size == 0)
    {
        return;
    }
    constexpr std::uint32_t kMetalInlineByteCap = 4096u;
    const NSUInteger byte_len =
        (size > kMetalInlineByteCap) ? kMetalInlineByteCap : size;
    const NSUInteger arg_index = static_cast<NSUInteger>(offset);

    if (has(stages, ShaderStage::kVertex))
    {
        [encoder_ setVertexBytes:data
                          length:byte_len
                         atIndex:arg_index];
    }
    if (has(stages, ShaderStage::kFragment))
    {
        [encoder_ setFragmentBytes:data
                            length:byte_len
                           atIndex:arg_index];
    }
}

// ---------------------------------------------------------------------------
// copy_buffer — MTLBlitCommandEncoder copyFromBuffer:..:toBuffer:...
// Sprint-2: lookups miss (no real allocation yet) so the encoder is opened
// but the loop body skips every region. Sprint 3 allocates real MTLBuffers
// and the copy fires end-to-end.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::copy_buffer(BufferHandle src, BufferHandle dst,
                                         std::span<const BufferCopyRegion> regions)
{
    if (cmd_ == nil || ctx_ == nullptr || regions.empty())
    {
        return;
    }
    id<MTLBuffer> src_buf = ctx_->lookup_buffer(src);
    id<MTLBuffer> dst_buf = ctx_->lookup_buffer(dst);
    if (src_buf == nil || dst_buf == nil)
    {
        // Sprint-2: buffers not yet backed; gracefully skip. Sprint 3
        // surfaces a kInvalidArgument when create_buffer is real.
        return;
    }
    ensure_blit_encoder_open();
    if (blit_ == nil)
    {
        return;
    }
    for (const BufferCopyRegion& r : regions)
    {
        if (r.size == 0)
        {
            continue;
        }
        [blit_ copyFromBuffer:src_buf
                 sourceOffset:static_cast<NSUInteger>(r.src_offset)
                     toBuffer:dst_buf
            destinationOffset:static_cast<NSUInteger>(r.dst_offset)
                         size:static_cast<NSUInteger>(r.size)];
    }
}

// ---------------------------------------------------------------------------
// copy_buffer_to_image — MTLBlitCommandEncoder
// copyFromBuffer:..:sourceBytesPerRow:..:sourceBytesPerImage:..:sourceSize:..:
//   toTexture:destinationSlice:destinationLevel:destinationOrigin:
//
// Sprint-2 assumes tightly-packed input (sourceBytesPerRow = width *
// texel_bytes) — same contract as the Vulkan back-end with the
// `bufferRowLength = 0` / `bufferImageHeight = 0` defaults. Texel byte size
// derivation is deferred to Sprint 3 (where TextureHandle resolves to a
// real MTLTexture and we can read its pixelFormat); Sprint-2 simply passes
// 0 to let Metal recompute the row stride from the texture descriptor,
// which is supported for non-compressed formats.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::copy_buffer_to_image(
    BufferHandle src, TextureHandle dst,
    std::span<const BufferImageCopyRegion> regions)
{
    if (cmd_ == nil || ctx_ == nullptr || regions.empty())
    {
        return;
    }
    id<MTLBuffer>  src_buf = ctx_->lookup_buffer(src);
    id<MTLTexture> dst_tex = ctx_->lookup_texture(dst);
    if (src_buf == nil || dst_tex == nil)
    {
        return;
    }
    ensure_blit_encoder_open();
    if (blit_ == nil)
    {
        return;
    }
    for (const BufferImageCopyRegion& r : regions)
    {
        const MTLOrigin origin = MTLOriginMake(
            static_cast<NSUInteger>(r.image_offset.x < 0 ? 0 : r.image_offset.x),
            static_cast<NSUInteger>(r.image_offset.y < 0 ? 0 : r.image_offset.y),
            static_cast<NSUInteger>(r.image_offset.z < 0 ? 0 : r.image_offset.z));
        const MTLSize size = MTLSizeMake(
            static_cast<NSUInteger>(r.image_extent.width),
            static_cast<NSUInteger>(r.image_extent.height),
            static_cast<NSUInteger>(r.image_extent.depth));
        // sourceBytesPerRow == 0 + sourceBytesPerImage == 0 = "auto" for
        // non-compressed formats; Metal computes from the texture's
        // pixel format and the supplied size. Sprint 3 will resolve a
        // real texel-byte count for compressed formats.
        for (std::uint32_t layer = 0; layer < r.layer_count; ++layer)
        {
            const NSUInteger slice =
                static_cast<NSUInteger>(r.base_layer + layer);
            [blit_ copyFromBuffer:src_buf
                     sourceOffset:static_cast<NSUInteger>(r.buffer_offset)
                sourceBytesPerRow:0
              sourceBytesPerImage:0
                       sourceSize:size
                        toTexture:dst_tex
                 destinationSlice:slice
                 destinationLevel:static_cast<NSUInteger>(r.mip_level)
                destinationOrigin:origin];
        }
    }
}

// ---------------------------------------------------------------------------
// copy_image_to_buffer — symmetric readback path. Same Sprint-2
// "graceful skip when handles unbacked" semantics as copy_buffer_to_image.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::copy_image_to_buffer(
    TextureHandle src, BufferHandle dst,
    std::span<const BufferImageCopyRegion> regions)
{
    if (cmd_ == nil || ctx_ == nullptr || regions.empty())
    {
        return;
    }
    id<MTLTexture> src_tex = ctx_->lookup_texture(src);
    id<MTLBuffer>  dst_buf = ctx_->lookup_buffer(dst);
    if (src_tex == nil || dst_buf == nil)
    {
        return;
    }
    ensure_blit_encoder_open();
    if (blit_ == nil)
    {
        return;
    }
    for (const BufferImageCopyRegion& r : regions)
    {
        const MTLOrigin origin = MTLOriginMake(
            static_cast<NSUInteger>(r.image_offset.x < 0 ? 0 : r.image_offset.x),
            static_cast<NSUInteger>(r.image_offset.y < 0 ? 0 : r.image_offset.y),
            static_cast<NSUInteger>(r.image_offset.z < 0 ? 0 : r.image_offset.z));
        const MTLSize size = MTLSizeMake(
            static_cast<NSUInteger>(r.image_extent.width),
            static_cast<NSUInteger>(r.image_extent.height),
            static_cast<NSUInteger>(r.image_extent.depth));
        for (std::uint32_t layer = 0; layer < r.layer_count; ++layer)
        {
            const NSUInteger slice =
                static_cast<NSUInteger>(r.base_layer + layer);
            [blit_ copyFromTexture:src_tex
                       sourceSlice:slice
                       sourceLevel:static_cast<NSUInteger>(r.mip_level)
                      sourceOrigin:origin
                        sourceSize:size
                          toBuffer:dst_buf
                 destinationOffset:static_cast<NSUInteger>(r.buffer_offset)
            destinationBytesPerRow:0
          destinationBytesPerImage:0];
        }
    }
}

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
