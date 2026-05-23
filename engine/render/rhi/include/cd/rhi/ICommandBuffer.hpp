// =============================================================================
// CHROMODYNAMIC — cd/rhi/ICommandBuffer.hpp
// ADR-001 (Sprint S3.1) — RHI command-buffer recording surface.
//
// A command buffer captures GPU work between `begin()` and `end()`, then is
// submitted via IDevice::submit(). Recording is single-threaded per buffer;
// multiple buffers may be recorded in parallel and submitted in order.
//
// The interface intentionally mirrors Vulkan's structure (passes, dynamic
// state, pipeline binding) so the Vulkan back-end is a direct translation
// and the D3D12 back-end translates state transitions to barriers.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/Pipeline.hpp>

#include <cstdint>
#include <span>

namespace cd::rhi
{

struct ColorAttachmentInfo
{
    TextureViewHandle view {};
    LoadOp load_op { LoadOp::kClear };
    StoreOp store_op { StoreOp::kStore };
    ClearColor clear_color {};
};

struct DepthStencilAttachmentInfo
{
    TextureViewHandle view {};
    LoadOp depth_load { LoadOp::kClear };
    StoreOp depth_store { StoreOp::kStore };
    LoadOp stencil_load { LoadOp::kDontCare };
    StoreOp stencil_store { StoreOp::kDontCare };
    ClearDepthStencil clear {};
};

struct RenderPassBeginInfo
{
    Rect2D render_area {};
    std::span<const ColorAttachmentInfo> color_attachments;
    const DepthStencilAttachmentInfo* depth_stencil { nullptr };
};

struct BufferCopyRegion
{
    std::uint64_t src_offset { 0 };
    std::uint64_t dst_offset { 0 };
    std::uint64_t size { 0 };
};

/// One sub-region of a buffer→image copy. The buffer is treated as a flat
/// byte blob; the image side is addressed by mip / array layer / 3D extent.
/// The engine assumes tightly-packed input (`buffer_row_length = 0`,
/// `buffer_image_height = 0` in Vulkan terms) so the driver derives the
/// row pitch from the texel extent — adequate for the common case of
/// uploading a decoded PNG/JPG image.
struct BufferImageCopyRegion
{
    std::uint64_t buffer_offset { 0 };
    std::uint32_t mip_level { 0 };
    std::uint32_t base_layer { 0 };
    std::uint32_t layer_count { 1 };
    Offset3D image_offset { 0, 0, 0 };
    Extent3D image_extent { 1, 1, 1 };
};

class ICommandBuffer
{
public:
    ICommandBuffer() noexcept = default;
    virtual ~ICommandBuffer() = default;
    ICommandBuffer(const ICommandBuffer&) = delete;
    ICommandBuffer& operator=(const ICommandBuffer&) = delete;
    ICommandBuffer(ICommandBuffer&&) = delete;
    ICommandBuffer& operator=(ICommandBuffer&&) = delete;

    // ---- Lifecycle ---------------------------------------------------------
    virtual void begin() = 0;
    virtual void end() = 0;

    // ---- Render pass -------------------------------------------------------
    virtual void begin_render_pass(const RenderPassBeginInfo& info) = 0;
    virtual void end_render_pass() = 0;

    // ---- Pipeline binding --------------------------------------------------
    virtual void bind_graphics_pipeline(GraphicsPipelineHandle pipeline) = 0;
    virtual void bind_compute_pipeline(ComputePipelineHandle pipeline) = 0;

    virtual void bind_descriptor_set(std::uint32_t set_index, DescriptorSetHandle set) = 0;

    virtual void bind_vertex_buffer(std::uint32_t binding, BufferHandle buffer, std::uint64_t offset) = 0;
    virtual void bind_index_buffer(BufferHandle buffer, std::uint64_t offset, IndexType type) = 0;

    virtual void push_constants(
        PipelineLayoutHandle layout,
        ShaderStage stages,
        std::uint32_t offset,
        std::uint32_t size,
        const void* data
    ) = 0;

    // ---- Dynamic state -----------------------------------------------------
    virtual void set_viewport(const Viewport& vp) = 0;
    virtual void set_scissor(const Rect2D& rect) = 0;

    // ---- Draw / dispatch ---------------------------------------------------
    virtual void draw(
        std::uint32_t vertex_count,
        std::uint32_t instance_count,
        std::uint32_t first_vertex,
        std::uint32_t first_instance
    ) = 0;
    virtual void draw_indexed(
        std::uint32_t index_count,
        std::uint32_t instance_count,
        std::uint32_t first_index,
        std::int32_t vertex_offset,
        std::uint32_t first_instance
    ) = 0;
    virtual void dispatch(std::uint32_t group_x, std::uint32_t group_y, std::uint32_t group_z) = 0;

    // ---- Copies / clears ---------------------------------------------------
    virtual void copy_buffer(BufferHandle src, BufferHandle dst, std::span<const BufferCopyRegion> regions) = 0;

    /// Stream bytes from a host-visible buffer into an image. The image is
    /// expected to already be in `kTransferDst` (the user emits the matching
    /// barrier before this call and another one after, back to the layout
    /// the shader expects, typically `kShaderResource`). Used for one-shot
    /// texture uploads — the dominant pattern in every asset pipeline.
    virtual void
    copy_buffer_to_image(BufferHandle src, TextureHandle dst, std::span<const BufferImageCopyRegion> regions) = 0;

    /// Stream bytes from an image into a host-visible buffer. The source
    /// image is expected to already be in `kTransferSrc` (the user emits
    /// the matching barriers around this call). The buffer must be sized
    /// `width * height * bytes_per_pixel` for an RGBA8 capture, etc. Used
    /// for screenshot / golden-image capture (Phase 11 Track A) and for
    /// any readback-style operation a compute or render pass requires.
    /// Symmetric with `copy_buffer_to_image`.
    virtual void
    copy_image_to_buffer(TextureHandle src, BufferHandle dst, std::span<const BufferImageCopyRegion> regions) = 0;

    // ---- Barriers ----------------------------------------------------------
    virtual void
    barrier(std::span<const BufferBarrier> buffer_barriers, std::span<const TextureBarrier> texture_barriers) = 0;

    // ---- Debug -------------------------------------------------------------
    /// Pushes a named debug-group marker (RenderDoc / PIX / NSight Graphics
    /// surface this as a hierarchical scope).
    ///
    /// Lifetime contract: `name` only needs to outlive *this call*. The
    /// backend is required to copy the string into per-command-buffer
    /// storage before returning, because the underlying API (Vulkan
    /// `vkCmdBeginDebugUtilsLabelEXT`, D3D12 `BeginEvent`, Metal
    /// `pushDebugGroup:`) can keep the pointer alive until the command
    /// buffer finishes executing on the GPU — long after this call
    /// returns. Callers may therefore safely pass a `string_view` over a
    /// temporary `std::string`, a `std::format` buffer, or any other
    /// transient storage.
    virtual void push_debug_group(std::string_view name) = 0;
    virtual void pop_debug_group() = 0;
};

}  // namespace cd::rhi
