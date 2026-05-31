// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalCommandBuffer.mm
// phase548 — Metal command-buffer real impl (Sprint-1).
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
// Everything else stays a no-op for Sprint-1; subsequent sprints fill in
// the remaining ICommandBuffer surface (vertex buffers, uniforms, copies,
// compute, indexed draw, barriers).
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
    // Make sure any in-flight encoder is closed before we commit.
    if (encoder_ != nil)
    {
        [encoder_ endEncoding];
        encoder_ = nil;
    }
    if (drawable_to_present != nil)
    {
        [cmd_ presentDrawable:drawable_to_present];
    }
    [cmd_ commit];
    cmd_ = nil;
}

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
