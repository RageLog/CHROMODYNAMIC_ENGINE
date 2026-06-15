// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalCommandBuffer.mm
// phase548 — Metal command-buffer real impl (Sprint-1).
// phase559 — Sprint-2: push_constants + copy_buffer + copy_buffer_to_image
//            + copy_image_to_buffer (MTLBlitCommandEncoder path).
// phase572 — Sprint-3: bind_compute_pipeline + dispatch (MTLComputeCommand
//            Encoder path) + bind_vertex_buffer + bind_index_buffer +
//            draw_indexed (drawIndexedPrimitives:..:indexBuffer:..).
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
// M4 (ADR-20260615): the canonical push-constant argument-buffer slot
// (kPushConstantBufferIndex = 16) the M3 toolchain emits push blocks to.
#include <cd/rhi/metal/MetalShaderToolchain.hpp>

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

// M8 (ADR-20260615): begin_render_pass now handles MULTIPLE colour
// attachments + a depth/stencil attachment (was Sprint-1 single-color, no
// depth). Each ColorAttachmentInfo loops into rpd.colorAttachments[i];
// render-target views resolve through lookup_texture_view (real MTLTextures
// allocated by M1) OR the swapchain-drawable path. The optional depth_stencil
// attachment maps to rpd.depthAttachment (+ stencilAttachment when the format
// carries stencil). Load/store/clear honour the per-attachment ops.
//
// M4-Y (ADR-20260615): the default viewport uses a NEGATIVE HEIGHT
// (originY = render_area + height, height = -height) so Metal's +Y-up
// framebuffer renders the same as Vulkan's +Y-down — the mandatory cross-
// backend NDC parity (phase1196 D16 lesson, identical to the D3D12 negative-
// height-viewport). set_viewport() applies the same flip for caller-supplied
// viewports.
void MetalCommandBufferImpl::begin_render_pass(const RenderPassBeginInfo& info)
{
    if (info.color_attachments.empty() && info.depth_stencil == nullptr)
    {
        return;
    }
    // phase559/572: close any blit/compute encoder before a render encoder
    // opens — Metal forbids nested encoders on a single cmd-buf.
    close_blit_encoder_if_open();
    close_compute_encoder_if_open();
    close_accel_encoder_if_open();

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];

    bool any_target = false;
    for (std::size_t i = 0; i < info.color_attachments.size(); ++i)
    {
        const ColorAttachmentInfo& att = info.color_attachments[i];
        id<MTLTexture> tex = resolve_attachment_texture(att.view);
        if (tex == nil)
        {
            // Cannot resolve this target — skip it but keep going so a valid
            // sibling attachment still renders (matches the graceful-skip
            // pattern of the rest of the backend).
            continue;
        }
        any_target = true;
        MTLRenderPassColorAttachmentDescriptor* ca =
            rpd.colorAttachments[static_cast<NSUInteger>(i)];
        ca.texture     = tex;
        ca.loadAction  =
            (att.load_op == LoadOp::kClear) ? MTLLoadActionClear
          : (att.load_op == LoadOp::kLoad ) ? MTLLoadActionLoad
                                            : MTLLoadActionDontCare;
        ca.storeAction =
            (att.store_op == StoreOp::kStore) ? MTLStoreActionStore
                                              : MTLStoreActionDontCare;
        ca.clearColor  = MTLClearColorMake(
            static_cast<double>(att.clear_color.f32[0]),
            static_cast<double>(att.clear_color.f32[1]),
            static_cast<double>(att.clear_color.f32[2]),
            static_cast<double>(att.clear_color.f32[3]));
    }

    if (info.depth_stencil != nullptr)
    {
        const DepthStencilAttachmentInfo& ds = *info.depth_stencil;
        id<MTLTexture> dtex = resolve_attachment_texture(ds.view);
        if (dtex != nil)
        {
            any_target = true;
            rpd.depthAttachment.texture    = dtex;
            rpd.depthAttachment.loadAction =
                (ds.depth_load == LoadOp::kClear) ? MTLLoadActionClear
              : (ds.depth_load == LoadOp::kLoad ) ? MTLLoadActionLoad
                                                  : MTLLoadActionDontCare;
            rpd.depthAttachment.storeAction =
                (ds.depth_store == StoreOp::kStore) ? MTLStoreActionStore
                                                    : MTLStoreActionDontCare;
            rpd.depthAttachment.clearDepth =
                static_cast<double>(ds.clear.depth);

            // Stencil rides the same texture only when the format is a
            // combined depth+stencil one (Depth24Unorm_Stencil8 /
            // Depth32Float_Stencil8). Setting it unconditionally is harmless
            // for depth-only formats because Metal ignores a stencil
            // attachment whose pixel format lacks stencil; we gate on the
            // texture's pixelFormat to avoid a validation warning.
            const MTLPixelFormat pf = [dtex pixelFormat];
            if (pf == MTLPixelFormatDepth24Unorm_Stencil8
                || pf == MTLPixelFormatDepth32Float_Stencil8
                || pf == MTLPixelFormatStencil8)
            {
                rpd.stencilAttachment.texture    = dtex;
                rpd.stencilAttachment.loadAction =
                    (ds.stencil_load == LoadOp::kClear) ? MTLLoadActionClear
                  : (ds.stencil_load == LoadOp::kLoad ) ? MTLLoadActionLoad
                                                        : MTLLoadActionDontCare;
                rpd.stencilAttachment.storeAction =
                    (ds.stencil_store == StoreOp::kStore) ? MTLStoreActionStore
                                                          : MTLStoreActionDontCare;
                rpd.stencilAttachment.clearStencil = ds.clear.stencil;
            }
        }
    }

    if (!any_target)
    {
        // Nothing resolved — do not open an encoder against an empty
        // descriptor (Metal would assert). Skip the pass.
        return;
    }

    encoder_ = [cmd_ renderCommandEncoderWithDescriptor:rpd];
    encoder_.label = @"cd::rhi::metal::RenderEncoder";

    // Default viewport / scissor to the render area (NDC-Y flipped — M4-Y).
    const auto& ext = info.render_area.extent;
    if (ext.width > 0 && ext.height > 0)
    {
        const double h = static_cast<double>(ext.height);
        MTLViewport vp {
            .originX = static_cast<double>(info.render_area.offset.x),
            // Negative-height viewport: origin moves to the bottom and height
            // goes negative so +Y points down (Vulkan parity). M4-Y.
            .originY = static_cast<double>(info.render_area.offset.y) + h,
            .width   = static_cast<double>(ext.width),
            .height  = -h,
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

// M8 helper: resolve a render-target / depth attachment view to a live
// MTLTexture. Tries the swapchain-drawable path first (the most common colour
// target) then the regular texture-view registry (M1-backed offscreen
// targets: HDR scene, G-Buffer, shadow map, etc.).
id<MTLTexture>
MetalCommandBufferImpl::resolve_attachment_texture(TextureViewHandle view) noexcept
{
    if (ctx_ == nullptr)
    {
        return nil;
    }
    if (id<MTLTexture> sc = ctx_->lookup_swapchain_view_texture(view);
        sc != nil)
    {
        return sc;
    }
    return ctx_->lookup_texture_view(view);
}

void MetalCommandBufferImpl::end_render_pass()
{
    if (encoder_ != nil)
    {
        [encoder_ endEncoding];
        encoder_ = nil;
    }
}

// M2 (ADR-20260615): bind the full graphics-pipeline state. Metal carries
// depth-stencil, cull mode, and winding on the ENCODER (not the PSO), so we
// apply all four from the bound MetalGraphicsPipelineStateObj:
//   * [encoder setRenderPipelineState:pso]
//   * [encoder setDepthStencilState:dss]   (when the pipeline has depth)
//   * [encoder setCullMode:] + [encoder setFrontFacingWinding:]
// The pipeline's primitive type is cached for draw() / draw_indexed().
void MetalCommandBufferImpl::bind_graphics_pipeline(GraphicsPipelineHandle pipeline)
{
    if (encoder_ == nil || ctx_ == nullptr)
    {
        return;
    }
    const MetalGraphicsPipelineStateObj* state =
        ctx_->lookup_graphics_pipeline_state(pipeline);
    if (state == nullptr || state->pso() == nil)
    {
        return;
    }
    [encoder_ setRenderPipelineState:state->pso()];
    if (state->dss() != nil)
    {
        [encoder_ setDepthStencilState:state->dss()];
    }
    [encoder_ setCullMode:state->cull()];
    [encoder_ setFrontFacingWinding:state->winding()];
    bound_primitive_ = state->primitive();
}

// M4 (ADR-20260615): bind a descriptor set as a Metal argument buffer.
//
//   Vulkan descriptor set N  ->  Metal [[buffer(N)]] argument buffer.
//
// The argument buffer is bound to slot `set_index` on BOTH the vertex and
// fragment stages (render) or the compute stage (compute) so a set declared
// for either stage resolves — matches the SPIRV-Cross MSL the M3 toolchain
// emits (set-per-argument-buffer at [[buffer(set)]]). Every resource the
// argument buffer references is then made resident via
// [encoder useResource:usage:] (MANDATORY for argument buffers — the GPU
// cannot fault in a resource the encoder has not been told the argument
// buffer reaches; the W8-BE cross-encoder visibility rule, ADR-20260530
// §8.5.1). Lookup miss / empty set -> graceful skip.
void MetalCommandBufferImpl::bind_descriptor_set(std::uint32_t set_index,
                                                 DescriptorSetHandle set)
{
    if (ctx_ == nullptr)
    {
        return;
    }
    MetalDescriptorSetObj* ds = ctx_->lookup_descriptor_set(set);
    if (ds == nullptr || ds->arg_buffer() == nil)
    {
        return;
    }
    const NSUInteger slot = static_cast<NSUInteger>(set_index);
    id<MTLBuffer> arg = ds->arg_buffer();

    if (encoder_ != nil)
    {
        [encoder_ setVertexBuffer:arg offset:0 atIndex:slot];
        [encoder_ setFragmentBuffer:arg offset:0 atIndex:slot];
        // Residency: make every referenced resource resident for both stages.
        for (id<MTLBuffer> b in ds->resident_buffers())
        {
            [encoder_ useResource:b
                            usage:MTLResourceUsageRead | MTLResourceUsageWrite
                           stages:MTLRenderStageVertex | MTLRenderStageFragment];
        }
        for (id<MTLTexture> t in ds->resident_textures())
        {
            [encoder_ useResource:t
                            usage:MTLResourceUsageRead
                           stages:MTLRenderStageVertex | MTLRenderStageFragment];
        }
    }
    else if (compute_ != nil)
    {
        [compute_ setBuffer:arg offset:0 atIndex:slot];
        for (id<MTLBuffer> b in ds->resident_buffers())
        {
            [compute_ useResource:b
                            usage:MTLResourceUsageRead | MTLResourceUsageWrite];
        }
        for (id<MTLTexture> t in ds->resident_textures())
        {
            [compute_ useResource:t usage:MTLResourceUsageRead];
        }
    }
}

// M4-Y (ADR-20260615): caller viewports get the same NEGATIVE-HEIGHT flip as
// the default render-pass viewport so Metal's +Y-up framebuffer matches
// Vulkan's +Y-down (cross-backend NDC parity; phase1196 D16 lesson). The flip
// is applied in ONE place per the ADR invariant (ii) — here + begin_render_pass
// — keeping the shader backend-agnostic (no clip-space flip in MSL).
void MetalCommandBufferImpl::set_viewport(const Viewport& vp)
{
    if (encoder_ == nil)
    {
        return;
    }
    MTLViewport mvp {
        .originX = static_cast<double>(vp.x),
        .originY = static_cast<double>(vp.y) + static_cast<double>(vp.height),
        .width   = static_cast<double>(vp.width),
        .height  = -static_cast<double>(vp.height),
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
    // M2 (ADR-20260615): primitive type sourced from the bound pipeline
    // (was hard-coded Triangle in Sprint-1).
    [encoder_ drawPrimitives:bound_primitive_
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
    else if (compute_ != nil)
    {
        // phase572: debug groups while a compute-encoder is open ride on it.
        [compute_ pushDebugGroup:ns];
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
    else if (compute_ != nil)
    {
        [compute_ popDebugGroup];
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
    // phase572: also close any pending compute encoder.
    close_compute_encoder_if_open();
    // M9 (ADR-20260615): also close any pending acceleration-structure encoder.
    close_accel_encoder_if_open();
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
    // phase572: same rule for a compute encoder left open by a prior
    // bind_compute_pipeline / dispatch sequence.
    close_compute_encoder_if_open();
    // M9: same rule for an acceleration-structure encoder.
    close_accel_encoder_if_open();
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
// push_constants — Metal inline-byte fast path (M4 / ADR-20260615).
//
// Metal has no native "push constants"; the canonical replacement for small
// (≤4 KB) per-draw constants is setVertexBytes / setFragmentBytes. The M3
// toolchain remaps the Vulkan push_constant block to a FIXED argument-buffer
// slot [[buffer(kPushConstantBufferIndex = 16)]] that sits ABOVE the engine's
// descriptor-set range (sets 0/1 -> [[buffer(0/1)]]), so there is no
// collision. We therefore bind at THAT constant, NOT the caller's `offset`
// (the offset is a byte offset INTO the push range, which Metal's inline-byte
// path does not subdivide; the engine pushes the whole block per draw). This
// is the M4.4 contract fix — the Sprint-2 path mis-used `offset` as the arg
// index.
//
// `size` is clamped at 4 KB per Metal's documented inline-arg cap.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::push_constants(PipelineLayoutHandle /*layout*/,
                                            ShaderStage stages,
                                            std::uint32_t /*offset*/,
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
    const NSUInteger push_slot =
        static_cast<NSUInteger>(cd::rhi::metal::kPushConstantBufferIndex);

    if (has(stages, ShaderStage::kVertex))
    {
        [encoder_ setVertexBytes:data
                          length:byte_len
                         atIndex:push_slot];
    }
    if (has(stages, ShaderStage::kFragment))
    {
        [encoder_ setFragmentBytes:data
                            length:byte_len
                           atIndex:push_slot];
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

// ---------------------------------------------------------------------------
// phase572 (Sprint-3) — compute encoder + vertex/index binding +
// draw_indexed implementations.
//
// The MTLComputeCommandEncoder is opened lazily by ensure_compute_encoder_
// open() on the first compute-side call (bind_compute_pipeline / dispatch)
// after begin(). It is closed in four places, mirroring the Sprint-2 blit
// encoder discipline:
//   1. before begin_render_pass opens a render encoder.
//   2. before ensure_blit_encoder_open opens a blit encoder.
//   3. before submit_internal commits the cmd-buf.
//   4. explicitly via close_compute_encoder_if_open() anywhere we need to
//      transition encoder kinds in the future.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::ensure_compute_encoder_open()
{
    if (cmd_ == nil)
    {
        // No cmd-buf yet — begin() was not called. Match the blit-encoder
        // helper's defensive silent skip.
        return;
    }
    if (encoder_ != nil)
    {
        [encoder_ endEncoding];
        encoder_ = nil;
    }
    if (blit_ != nil)
    {
        [blit_ endEncoding];
        blit_ = nil;
    }
    // M9: close any acceleration-structure encoder before opening compute.
    close_accel_encoder_if_open();
    if (compute_ != nil)
    {
        // Already open — nothing to do.
        return;
    }
    compute_ = [cmd_ computeCommandEncoder];
    compute_.label = @"cd::rhi::metal::ComputeEncoder";
}

void MetalCommandBufferImpl::close_compute_encoder_if_open() noexcept
{
    if (compute_ != nil)
    {
        [compute_ endEncoding];
        compute_ = nil;
    }
    // Note: current_compute_pso_ is intentionally NOT cleared here. The
    // PSO is set once via bind_compute_pipeline and persists across
    // encoder open/close transitions inside the same cmd-buf so callers
    // can interleave dispatch with copy_* (which would close the encoder)
    // without re-binding the PSO. Sprint 4 may revisit if a stricter
    // validation policy is desired.
}

// ---------------------------------------------------------------------------
// bind_compute_pipeline — Sprint-3 setComputePipelineState path.
//
// Routes through the device-side compute-pipeline registry. Lookup miss is
// silently skipped (matches Sprint-1's render-pipeline behaviour).
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::bind_compute_pipeline(ComputePipelineHandle pipeline)
{
    if (cmd_ == nil || ctx_ == nullptr)
    {
        return;
    }
    id<MTLComputePipelineState> pso = ctx_->lookup_compute_pipeline(pipeline);
    if (pso == nil)
    {
        return;
    }
    ensure_compute_encoder_open();
    if (compute_ == nil)
    {
        return;
    }
    [compute_ setComputePipelineState:pso];
    current_compute_pso_ = pso;
}

// ---------------------------------------------------------------------------
// dispatch — Sprint-3 dispatchThreadgroups path.
//
// Sprint-3 surface uses a 1x1x1 threadgroup size; real compute shaders
// override this via SPIRV-Cross attributes (`[[threads_per_threadgroup]]`)
// once SPIR-V → MSL translation lands. The (x, y, z) grid dimensions are
// passed straight through as the threadgroup count.
//
// If the caller invokes dispatch without first binding a compute PSO the
// call is silently skipped — matches the Sprint-1 render-pipeline-not-bound
// behaviour for draw().
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::dispatch(std::uint32_t x,
                                      std::uint32_t y,
                                      std::uint32_t z)
{
    if (compute_ == nil || current_compute_pso_ == nil)
    {
        return;
    }
    if (x == 0u || y == 0u || z == 0u)
    {
        return;
    }
    const MTLSize groups = MTLSizeMake(
        static_cast<NSUInteger>(x),
        static_cast<NSUInteger>(y),
        static_cast<NSUInteger>(z));
    const MTLSize threads = MTLSizeMake(1, 1, 1);
    [compute_ dispatchThreadgroups:groups threadsPerThreadgroup:threads];
}

// ---------------------------------------------------------------------------
// bind_vertex_buffer — Sprint-3 setVertexBuffer:offset:atIndex: path.
//
// The Metal vertex-buffer table is a flat index space; the engine's
// `binding` parameter maps directly to it (matches the Vulkan back-end's
// VkVertexInputBindingDescription::binding and the D3D12 back-end's
// input-slot index). When the lookup misses (still the Sprint-3 default —
// create_buffer hands out stub handles) the call is gracefully skipped,
// mirroring the copy-buffer fallback.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::bind_vertex_buffer(std::uint32_t binding,
                                                BufferHandle buffer,
                                                std::uint64_t offset)
{
    if (encoder_ == nil || ctx_ == nullptr)
    {
        return;
    }
    id<MTLBuffer> buf = ctx_->lookup_buffer(buffer);
    if (buf == nil)
    {
        return;
    }
    [encoder_ setVertexBuffer:buf
                       offset:static_cast<NSUInteger>(offset)
                      atIndex:static_cast<NSUInteger>(binding)];
}

// ---------------------------------------------------------------------------
// bind_index_buffer — Sprint-3 index-state cache.
//
// Metal does not pre-bind an index buffer; drawIndexedPrimitives takes the
// buffer + offset + index type inline. We cache the resolved MTLBuffer and
// MTLIndexType here so draw_indexed can replay them. The buffer is resolved
// eagerly (rather than at draw time) so the cmd-buffer dependency on the
// device-side registry stays at the bind site, not scattered across each
// draw_indexed call.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::bind_index_buffer(BufferHandle buffer,
                                               std::uint64_t offset,
                                               IndexType type)
{
    if (ctx_ == nullptr)
    {
        return;
    }
    index_buf_    = ctx_->lookup_buffer(buffer);
    index_offset_ = static_cast<NSUInteger>(offset);
    index_type_   = (type == IndexType::kUInt32) ? MTLIndexTypeUInt32
                                                 : MTLIndexTypeUInt16;
}

// ---------------------------------------------------------------------------
// draw_indexed — Sprint-3 drawIndexedPrimitives:..:indexBuffer:.. path.
//
// Uses the index state cached by bind_index_buffer. Gracefully no-ops when
// the index buffer is nil (the cached lookup missed because create_buffer
// still returns stub handles), matching the Sprint-2 copy fallback.
//
// Topology is hardcoded at MTLPrimitiveTypeTriangle to match draw() — the
// bound pipeline's primitive topology will be sourced from the
// GraphicsPipelineDesc once Sprint-4 lights up the desc-driven pipeline
// builder. `vertex_offset` is propagated via baseVertex: which expects a
// signed integer (Vulkan's int32_t) — Metal accepts the value as-is.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::draw_indexed(std::uint32_t index_count,
                                          std::uint32_t instance_count,
                                          std::uint32_t first_index,
                                          std::int32_t  vertex_offset,
                                          std::uint32_t first_instance)
{
    if (encoder_ == nil || index_buf_ == nil || index_count == 0)
    {
        return;
    }
    // Metal indexes the index buffer in element units; first_index is
    // therefore folded into the buffer offset by adding (first_index *
    // index-element-size) bytes. This matches Vulkan's
    // vkCmdDrawIndexed firstIndex semantics.
    const NSUInteger index_elem_bytes =
        (index_type_ == MTLIndexTypeUInt32) ? 4u : 2u;
    const NSUInteger byte_offset =
        index_offset_
        + static_cast<NSUInteger>(first_index) * index_elem_bytes;

    const NSUInteger inst =
        (instance_count == 0u) ? 1u : static_cast<NSUInteger>(instance_count);

    // M2 (ADR-20260615): primitive type sourced from the bound pipeline
    // (was hard-coded Triangle in Sprint-3).
    [encoder_ drawIndexedPrimitives:bound_primitive_
                         indexCount:static_cast<NSUInteger>(index_count)
                          indexType:index_type_
                        indexBuffer:index_buf_
                  indexBufferOffset:byte_offset
                      instanceCount:inst
                         baseVertex:static_cast<NSInteger>(vertex_offset)
                       baseInstance:static_cast<NSUInteger>(first_instance)];
}

// ---------------------------------------------------------------------------
// M5 (ADR-20260615) — barrier.
//
// Metal auto-tracks hazards at encoder boundaries for tracked resources, so an
// explicit barrier is only needed for ordering/visibility WITHIN an open
// encoder (e.g. a compute write read by a later dispatch on the same encoder,
// or untracked argument-buffer resources). We translate the buffer/texture
// barrier spans into a memory-barrier scope on the active encoder:
//   * render encoder  -> [encoder memoryBarrierWithScope:afterStages:beforeStages:]
//   * compute encoder -> [encoder memoryBarrierWithScope:]
// Layout transitions do not exist on Metal (storageMode is fixed) so only the
// scope (buffers / textures) is mapped. Cross-encoder / queue ordering is
// already covered by Metal's automatic hazard tracking + the submit-time
// MTLSharedEvent hand-off, so no MTLFence is needed for the engine's tracked
// resources.
void MetalCommandBufferImpl::barrier(std::span<const BufferBarrier> bb,
                                     std::span<const TextureBarrier> tb)
{
    MTLBarrierScope scope = static_cast<MTLBarrierScope>(0);
    if (!bb.empty())
    {
        scope |= MTLBarrierScopeBuffers;
    }
    if (!tb.empty())
    {
        scope |= MTLBarrierScopeTextures;
    }
    if (scope == static_cast<MTLBarrierScope>(0))
    {
        return;
    }
    if (encoder_ != nil)
    {
        [encoder_ memoryBarrierWithScope:scope
                             afterStages:MTLRenderStageFragment
                            beforeStages:MTLRenderStageVertex];
    }
    else if (compute_ != nil)
    {
        [compute_ memoryBarrierWithScope:scope];
    }
}

// ---------------------------------------------------------------------------
// M9 (ADR-20260615) — acceleration-structure encoder + build.
//
// The AS encoder is lazy-opened by ensure_accel_encoder_open() and follows the
// SAME nested-encoder discipline as the blit / compute encoders (Metal forbids
// two open encoders on one cmd-buf). build_acceleration_structure runs the
// device-built descriptor via buildAccelerationStructure:descriptor:scratch
// Buffer:scratchBufferOffset:. This is the RHI's REAL RT path (AS build +
// ray-query, the rayQueryEXT analog); the SBT pipeline (dispatch_rays) stays
// a no-op on Metal exactly as on Vulkan.
// ---------------------------------------------------------------------------
void MetalCommandBufferImpl::ensure_accel_encoder_open()
{
    if (cmd_ == nil)
    {
        return;
    }
    if (encoder_ != nil)
    {
        [encoder_ endEncoding];
        encoder_ = nil;
    }
    if (blit_ != nil)
    {
        [blit_ endEncoding];
        blit_ = nil;
    }
    if (compute_ != nil)
    {
        [compute_ endEncoding];
        compute_ = nil;
    }
    if (accel_ != nil)
    {
        return;
    }
    accel_ = [cmd_ accelerationStructureCommandEncoder];
    accel_.label = @"cd::rhi::metal::AccelEncoder";
}

void MetalCommandBufferImpl::close_accel_encoder_if_open() noexcept
{
    if (accel_ != nil)
    {
        [accel_ endEncoding];
        accel_ = nil;
    }
}

void MetalCommandBufferImpl::build_acceleration_structure(AccelStructureHandle as)
{
    if (cmd_ == nil || ctx_ == nullptr)
    {
        return;
    }
    MetalAccelObj* obj = ctx_->lookup_accel(as);
    if (obj == nullptr || obj->as() == nil || obj->descriptor() == nil)
    {
        return;
    }
    ensure_accel_encoder_open();
    if (accel_ == nil)
    {
        return;
    }
    [accel_ buildAccelerationStructure:obj->as()
                            descriptor:obj->descriptor()
                         scratchBuffer:obj->scratch()
                   scratchBufferOffset:0];
}

void MetalCommandBufferImpl::acceleration_structure_barrier()
{
    // The AS-encoder boundary itself orders an AS build vs. its consumer; when
    // a render/compute encoder is already open (consuming a TLAS via
    // ray-query) we additionally issue a buffer-scope memory barrier so a TLAS
    // rebuilt earlier on the same cmd-buf is visible to the ray-query reads.
    if (encoder_ != nil)
    {
        [encoder_ memoryBarrierWithScope:MTLBarrierScopeBuffers
                             afterStages:MTLRenderStageVertex
                            beforeStages:MTLRenderStageFragment];
    }
    else if (compute_ != nil)
    {
        [compute_ memoryBarrierWithScope:MTLBarrierScopeBuffers];
    }
}

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
