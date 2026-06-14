// =============================================================================
// CHROMODYNAMIC — cd/rhi/vulkan/VulkanCommandBuffer.cpp
// =============================================================================
#include "VulkanCommandBuffer.hpp"

#include <cd/rhi/vulkan/VulkanFormat.hpp>  // vk_aspect_for_format (single source of truth)

#include <cassert>

namespace cd::rhi::vulkan
{

namespace
{

[[nodiscard]] VkPipelineStageFlags2 stage_for(cd::rhi::ResourceState state) noexcept
{
    using RS = cd::rhi::ResourceState;
    // Coarse mapping — picks the pipeline stages that the engine's resource
    // state transitions actually fire at. Refined image-layout tracking lands
    // in S3.5 once a render-graph is layered on top.
    switch (state)
    {
        case RS::kVertexBuffer:
            return VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        case RS::kIndexBuffer:
            return VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        case RS::kConstantBuffer:
        case RS::kShaderResource:
        case RS::kUnorderedAccess:
            return VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case RS::kColorAttachment:
            return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case RS::kDepthRead:
        case RS::kDepthWrite:
            return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        case RS::kTransferSrc:
        case RS::kTransferDst:
            return VK_PIPELINE_STAGE_2_COPY_BIT;
        case RS::kIndirectArgument:
            return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        case RS::kPresent:
            return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        case RS::kCommon:
        case RS::kUndefined:
            return VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    }
    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

[[nodiscard]] VkAccessFlags2 access_for(cd::rhi::ResourceState state) noexcept
{
    using RS = cd::rhi::ResourceState;
    switch (state)
    {
        case RS::kVertexBuffer:
            return VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        case RS::kIndexBuffer:
            return VK_ACCESS_2_INDEX_READ_BIT;
        case RS::kConstantBuffer:
            return VK_ACCESS_2_UNIFORM_READ_BIT;
        case RS::kShaderResource:
            return VK_ACCESS_2_SHADER_READ_BIT;
        case RS::kUnorderedAccess:
            return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        case RS::kColorAttachment:
            return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case RS::kDepthRead:
            return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case RS::kDepthWrite:
            return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case RS::kTransferSrc:
            return VK_ACCESS_2_TRANSFER_READ_BIT;
        case RS::kTransferDst:
            return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case RS::kIndirectArgument:
            return VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        case RS::kPresent:
        case RS::kCommon:
        case RS::kUndefined:
            return 0;
    }
    return 0;
}

[[nodiscard]] VkImageLayout layout_for(cd::rhi::ResourceState state) noexcept
{
    using RS = cd::rhi::ResourceState;
    switch (state)
    {
        case RS::kShaderResource:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case RS::kUnorderedAccess:
            return VK_IMAGE_LAYOUT_GENERAL;
        case RS::kColorAttachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case RS::kDepthRead:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        case RS::kDepthWrite:
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case RS::kTransferSrc:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case RS::kTransferDst:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case RS::kPresent:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default:
            return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

// Vulkan V1 (depth-aware barrier fix): resolve a texture handle's aspect mask
// via the device's image_formats map. The format -> aspect mapping itself comes
// from the single public source of truth vk_aspect_for_format() (defined in
// VulkanDevice.cpp, declared in <cd/rhi/vulkan/VulkanFormat.hpp>), so the
// barrier/copy wiring exercised here is the SAME mapping the unit test pins —
// no anon-namespace mirror that could silently drift. On a miss (e.g. swapchain
// images, which are always colour and carry no image_formats_ entry) the COLOR
// fallback is correct and safe.
[[nodiscard]] VkImageAspectFlags aspect_for_texture(
    const ResourceTables& tables, std::uint32_t texture_index) noexcept
{
    if (tables.image_formats != nullptr)
    {
        const auto it = tables.image_formats->find(texture_index);
        if (it != tables.image_formats->end())
            return static_cast<VkImageAspectFlags>(vk_aspect_for_format(it->second));
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

}  // namespace

VkImageLayout layout_for_state(cd::rhi::ResourceState state) noexcept
{
    // phase1127 (X4-B): public alias of the anon-namespace mapping so the
    // device-level image readback shares it instead of duplicating the
    // switch (drift here = silent layout corruption on round-trips).
    return layout_for(state);
}

VulkanCommandBuffer::VulkanCommandBuffer(
    VkDevice device,
    VkCommandPool pool,
    VkCommandBuffer command_buffer,
    ResourceTables tables
) noexcept
    : device_ { device }
    , pool_ { pool }
    , cmd_ { command_buffer }
    , tables_ { tables }
{
}

VulkanCommandBuffer::~VulkanCommandBuffer()
{
    // phase1116: lane pools retired by parallel recorders die with the
    // primary (engine usage waits for GPU idle before destroying cmd
    // buffers, so the secondaries are no longer pending). Destroying a
    // pool frees its command buffers implicitly.
    for (auto pool : retired_lane_pools_)
    {
        if (pool != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, pool, nullptr);
    }
    if (cmd_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(device_, pool_, 1, &cmd_);
    }
}

void VulkanCommandBuffer::adopt_label_arena(VulkanCommandBuffer& lane)
{
    // phase1119 (audit C): splice the lane's label strings into the primary
    // arena. The primary's begin() clears them once the next frame proves
    // the GPU is done — identical fencing to the retired pools.
    for (auto& s : lane.debug_label_arena_)
        debug_label_arena_.push_back(std::move(s));
    lane.debug_label_arena_.clear();
}

void VulkanCommandBuffer::retire_lane_pools(std::vector<VkCommandPool>&& pools)
{
    retired_lane_pools_.insert(retired_lane_pools_.end(),
                               pools.begin(), pools.end());
    pools.clear();
}

void VulkanCommandBuffer::begin()
{
    // Discard last frame's debug-label strings — the GPU has long since
    // consumed them. clear() releases the std::string payloads but
    // leaves the deque's bookkeeping nodes for reuse; reset() would
    // also release those, which is the wrong tradeoff for a per-frame
    // command buffer that will refill the arena immediately.
    debug_label_arena_.clear();

    // phase1119 (X1-FU-F step-2 gate, audit A3): free lane pools retired by
    // parallel recorders in earlier frames. begin() may only run when this
    // primary is no longer pending (Renderer waits on the frame fence first),
    // and a retired secondary's pending lifetime is a subset of the
    // primary's — so the pools are provably idle here. Without this, a
    // parallel pass per frame leaks lane_count pools per frame for the
    // application lifetime.
    for (auto pool : retired_lane_pools_)
    {
        if (pool != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, pool, nullptr);
    }
    retired_lane_pools_.clear();

    const VkCommandBufferBeginInfo bi {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    vkBeginCommandBuffer(cmd_, &bi);
}

void VulkanCommandBuffer::end()
{
    vkEndCommandBuffer(cmd_);
}

namespace
{

[[nodiscard]] VkAttachmentLoadOp load_op_for(cd::rhi::LoadOp op) noexcept
{
    switch (op)
    {
        case cd::rhi::LoadOp::kLoad:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case cd::rhi::LoadOp::kClear:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case cd::rhi::LoadOp::kDontCare:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

[[nodiscard]] VkAttachmentStoreOp store_op_for(cd::rhi::StoreOp op) noexcept
{
    switch (op)
    {
        case cd::rhi::StoreOp::kStore:
            return VK_ATTACHMENT_STORE_OP_STORE;
        case cd::rhi::StoreOp::kDontCare:
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

[[nodiscard]] VkShaderStageFlags shader_stages_for(cd::rhi::ShaderStage s) noexcept
{
    using SS = cd::rhi::ShaderStage;
    VkShaderStageFlags out { 0 };
    if (cd::rhi::has(s, SS::kVertex))
        out |= VK_SHADER_STAGE_VERTEX_BIT;
    if (cd::rhi::has(s, SS::kFragment))
        out |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (cd::rhi::has(s, SS::kCompute))
        out |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (cd::rhi::has(s, SS::kGeometry))
        out |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (cd::rhi::has(s, SS::kTessControl))
        out |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (cd::rhi::has(s, SS::kTessEval))
        out |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    return out;
}

}  // namespace

void VulkanCommandBuffer::begin_render_pass(const cd::rhi::RenderPassBeginInfo& info)
{
    begin_rendering_(info, 0);
}

void VulkanCommandBuffer::begin_rendering_(const cd::rhi::RenderPassBeginInfo& info,
                                           VkRenderingFlags flags)
{
    // Dynamic rendering path (Vulkan 1.3 core). The caller is expected to have
    // transitioned attachments into VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL /
    // VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL via a barrier() call before
    // begin_render_pass — we do not emit implicit transitions here because the
    // command buffer doesn't track per-image layout state in v1.
    std::vector<VkRenderingAttachmentInfo> color_attachments;
    color_attachments.reserve(info.color_attachments.size());
    for (const auto& a : info.color_attachments)
    {
        VkImageView vk_view { VK_NULL_HANDLE };
        if (tables_.views != nullptr)
        {
            auto it = tables_.views->find(a.view.index());
            if (it != tables_.views->end())
                vk_view = it->second;
        }
        VkRenderingAttachmentInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        ai.pNext = nullptr;
        ai.imageView = vk_view;
        ai.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ai.resolveMode = VK_RESOLVE_MODE_NONE;
        ai.resolveImageView = VK_NULL_HANDLE;
        ai.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ai.loadOp = load_op_for(a.load_op);
        ai.storeOp = store_op_for(a.store_op);
        ai.clearValue.color.float32[0] = a.clear_color.f32[0];
        ai.clearValue.color.float32[1] = a.clear_color.f32[1];
        ai.clearValue.color.float32[2] = a.clear_color.f32[2];
        ai.clearValue.color.float32[3] = a.clear_color.f32[3];
        color_attachments.push_back(ai);
    }

    VkRenderingAttachmentInfo depth_attachment {};
    bool has_depth = false;
    if (info.depth_stencil != nullptr)
    {
        VkImageView vk_view { VK_NULL_HANDLE };
        if (tables_.views != nullptr)
        {
            auto it = tables_.views->find(info.depth_stencil->view.index());
            if (it != tables_.views->end())
                vk_view = it->second;
        }
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = vk_view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.loadOp = load_op_for(info.depth_stencil->depth_load);
        depth_attachment.storeOp = store_op_for(info.depth_stencil->depth_store);
        depth_attachment.clearValue.depthStencil.depth = info.depth_stencil->clear.depth;
        depth_attachment.clearValue.depthStencil.stencil = info.depth_stencil->clear.stencil;
        has_depth = (vk_view != VK_NULL_HANDLE);
    }

    VkRenderingInfo ri {};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.pNext = nullptr;
    ri.flags = flags;
    ri.renderArea.offset = { info.render_area.offset.x, info.render_area.offset.y };
    ri.renderArea.extent = { info.render_area.extent.width, info.render_area.extent.height };
    ri.layerCount = 1;
    ri.viewMask = 0;
    ri.colorAttachmentCount = static_cast<std::uint32_t>(color_attachments.size());
    ri.pColorAttachments = color_attachments.empty() ? nullptr : color_attachments.data();
    ri.pDepthAttachment = has_depth ? &depth_attachment : nullptr;
    ri.pStencilAttachment = nullptr;

    vkCmdBeginRendering(cmd_, &ri);
}

void VulkanCommandBuffer::end_render_pass()
{
    vkCmdEndRendering(cmd_);
}

void VulkanCommandBuffer::bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle pipeline)
{
    if (tables_.graphics_pipelines == nullptr)
        return;
    auto it = tables_.graphics_pipelines->find(pipeline.index());
    if (it == tables_.graphics_pipelines->end())
        return;
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
    // Remember the layout for follow-up bind_descriptor_set / push_constants.
    if (tables_.pipeline_to_layout != nullptr)
    {
        auto layout_it = tables_.pipeline_to_layout->find(pipeline.index());
        if (layout_it != tables_.pipeline_to_layout->end())
        {
            current_graphics_layout_ = layout_it->second;
        }
    }
}

void VulkanCommandBuffer::bind_compute_pipeline(cd::rhi::ComputePipelineHandle pipeline)
{
    if (tables_.compute_pipelines == nullptr)
        return;
    auto it = tables_.compute_pipelines->find(pipeline.index());
    if (it == tables_.compute_pipelines->end())
        return;
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, it->second);
    if (tables_.pipeline_to_layout != nullptr)
    {
        auto layout_it = tables_.pipeline_to_layout->find(pipeline.index());
        if (layout_it != tables_.pipeline_to_layout->end())
        {
            current_compute_layout_ = layout_it->second;
        }
    }
}

void VulkanCommandBuffer::bind_descriptor_set(std::uint32_t set_index, cd::rhi::DescriptorSetHandle set)
{
    if (tables_.descriptor_sets == nullptr)
        return;
    auto it = tables_.descriptor_sets->find(set.index());
    if (it == tables_.descriptor_sets->end())
        return;
    // Prefer graphics layout if a graphics pipeline is currently bound; fall
    // back to compute. Callers that mix bind points within a render pass are
    // out-of-spec — Vulkan forbids it.
    const VkPipelineBindPoint bp =
        (current_graphics_layout_ != VK_NULL_HANDLE) ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
    const auto layout =
        (bp == VK_PIPELINE_BIND_POINT_GRAPHICS) ? current_graphics_layout_ : current_compute_layout_;
    if (layout == VK_NULL_HANDLE)
        return;  // no pipeline bound yet
    vkCmdBindDescriptorSets(cmd_, bp, layout, set_index, 1, &it->second, 0, nullptr);
}

void VulkanCommandBuffer::bind_vertex_buffer(std::uint32_t binding, cd::rhi::BufferHandle buffer, std::uint64_t offset)
{
    if (tables_.buffers == nullptr)
        return;
    auto it = tables_.buffers->find(buffer.index());
    if (it == tables_.buffers->end())
        return;
    vkCmdBindVertexBuffers(cmd_, binding, 1, &it->second, &offset);
}

void VulkanCommandBuffer::bind_index_buffer(cd::rhi::BufferHandle buffer, std::uint64_t offset, cd::rhi::IndexType type)
{
    if (tables_.buffers == nullptr)
        return;
    auto it = tables_.buffers->find(buffer.index());
    if (it == tables_.buffers->end())
        return;
    const VkIndexType vk_type = (type == cd::rhi::IndexType::kUInt16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd_, it->second, offset, vk_type);
}

void VulkanCommandBuffer::push_constants(
    cd::rhi::PipelineLayoutHandle layout,
    cd::rhi::ShaderStage stages,
    std::uint32_t offset,
    std::uint32_t size,
    const void* data
)
{
    if (data == nullptr || size == 0)
        return;
    VkPipelineLayout vk_layout { VK_NULL_HANDLE };
    if (tables_.pipeline_layouts != nullptr)
    {
        auto it = tables_.pipeline_layouts->find(layout.index());
        if (it != tables_.pipeline_layouts->end())
            vk_layout = it->second;
    }
    // If the caller passed an invalid handle, fall back to whatever pipeline is
    // currently bound — that's the strongly-typed common case (one push-constant
    // range per pipeline layout).
    if (vk_layout == VK_NULL_HANDLE)
    {
        vk_layout = (current_graphics_layout_ != VK_NULL_HANDLE) ? current_graphics_layout_ : current_compute_layout_;
    }
    if (vk_layout == VK_NULL_HANDLE)
        return;
    vkCmdPushConstants(cmd_, vk_layout, shader_stages_for(stages), offset, size, data);
}

void VulkanCommandBuffer::set_viewport(const cd::rhi::Viewport& vp)
{
    const VkViewport vk_vp {
        .x = vp.x,
        .y = vp.y,
        .width = vp.width,
        .height = vp.height,
        .minDepth = vp.min_depth,
        .maxDepth = vp.max_depth,
    };
    vkCmdSetViewport(cmd_, 0, 1, &vk_vp);
}

void VulkanCommandBuffer::set_scissor(const cd::rhi::Rect2D& rect)
{
    const VkRect2D vk_rect {
        .offset = { rect.offset.x,     rect.offset.y      },
        .extent = { rect.extent.width, rect.extent.height },
    };
    vkCmdSetScissor(cmd_, 0, 1, &vk_rect);
}

void VulkanCommandBuffer::draw(
    std::uint32_t vertex_count,
    std::uint32_t instance_count,
    std::uint32_t first_vertex,
    std::uint32_t first_instance
)
{
    vkCmdDraw(cmd_, vertex_count, instance_count, first_vertex, first_instance);
}

void VulkanCommandBuffer::draw_indexed(
    std::uint32_t index_count,
    std::uint32_t instance_count,
    std::uint32_t first_index,
    std::int32_t vertex_offset,
    std::uint32_t first_instance
)
{
    vkCmdDrawIndexed(cmd_, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void VulkanCommandBuffer::dispatch(std::uint32_t group_x, std::uint32_t group_y, std::uint32_t group_z)
{
    vkCmdDispatch(cmd_, group_x, group_y, group_z);
}

void VulkanCommandBuffer::copy_buffer(
    cd::rhi::BufferHandle src,
    cd::rhi::BufferHandle dst,
    std::span<const cd::rhi::BufferCopyRegion> regions
)
{
    if (tables_.buffers == nullptr || regions.empty())
        return;
    auto src_it = tables_.buffers->find(src.index());
    auto dst_it = tables_.buffers->find(dst.index());
    if (src_it == tables_.buffers->end() || dst_it == tables_.buffers->end())
    {
        return;  // unknown handle — silently skip (caller bug, can't surface here)
    }
    std::vector<VkBufferCopy> vk_regions;
    vk_regions.reserve(regions.size());
    for (const auto& r : regions)
    {
        vk_regions.push_back(
            VkBufferCopy {
                .srcOffset = r.src_offset,
                .dstOffset = r.dst_offset,
                .size = r.size,
            }
        );
    }
    vkCmdCopyBuffer(
        cmd_,
        src_it->second,
        dst_it->second,
        static_cast<std::uint32_t>(vk_regions.size()),
        vk_regions.data()
    );
}

void VulkanCommandBuffer::copy_buffer_to_image(
    cd::rhi::BufferHandle src,
    cd::rhi::TextureHandle dst,
    std::span<const cd::rhi::BufferImageCopyRegion> regions
)
{
    if (tables_.buffers == nullptr || tables_.images == nullptr || regions.empty())
        return;
    auto src_it = tables_.buffers->find(src.index());
    auto dst_it = tables_.images->find(dst.index());
    if (src_it == tables_.buffers->end() || dst_it == tables_.images->end())
        return;

    std::vector<VkBufferImageCopy> vk_regions;
    vk_regions.reserve(regions.size());
    for (const auto& r : regions)
    {
        VkBufferImageCopy vr {};
        vr.bufferOffset = r.buffer_offset;
        vr.bufferRowLength = 0;  // tightly packed (driver derives from extent)
        vr.bufferImageHeight = 0;
        vr.imageSubresource.aspectMask = aspect_for_texture(tables_, dst.index());
        vr.imageSubresource.mipLevel = r.mip_level;
        vr.imageSubresource.baseArrayLayer = r.base_layer;
        vr.imageSubresource.layerCount = r.layer_count;
        vr.imageOffset = { r.image_offset.x, r.image_offset.y, r.image_offset.z };
        vr.imageExtent = { r.image_extent.width, r.image_extent.height, r.image_extent.depth };
        vk_regions.push_back(vr);
    }
    // The destination image is expected to already be in TRANSFER_DST_OPTIMAL
    // — the caller emits the matching barrier before this call and another
    // one after (back to SHADER_READ_ONLY_OPTIMAL or whatever the consumer
    // expects). We do NOT do an implicit transition here so the cost is
    // predictable and call sites stay explicit about layout policy.
    vkCmdCopyBufferToImage(
        cmd_,
        src_it->second,
        dst_it->second,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<std::uint32_t>(vk_regions.size()),
        vk_regions.data()
    );
}

void VulkanCommandBuffer::copy_image_to_buffer(
    cd::rhi::TextureHandle src,
    cd::rhi::BufferHandle dst,
    std::span<const cd::rhi::BufferImageCopyRegion> regions
)
{
    if (tables_.buffers == nullptr || tables_.images == nullptr || regions.empty())
        return;
    auto src_it = tables_.images->find(src.index());
    auto dst_it = tables_.buffers->find(dst.index());
    if (src_it == tables_.images->end() || dst_it == tables_.buffers->end())
        return;

    std::vector<VkBufferImageCopy> vk_regions;
    vk_regions.reserve(regions.size());
    for (const auto& r : regions)
    {
        VkBufferImageCopy vr {};
        vr.bufferOffset = r.buffer_offset;
        vr.bufferRowLength = 0;
        vr.bufferImageHeight = 0;
        vr.imageSubresource.aspectMask = aspect_for_texture(tables_, src.index());
        vr.imageSubresource.mipLevel = r.mip_level;
        vr.imageSubresource.baseArrayLayer = r.base_layer;
        vr.imageSubresource.layerCount = r.layer_count;
        vr.imageOffset = { r.image_offset.x, r.image_offset.y, r.image_offset.z };
        vr.imageExtent = { r.image_extent.width, r.image_extent.height, r.image_extent.depth };
        vk_regions.push_back(vr);
    }
    // Source image expected to be in TRANSFER_SRC_OPTIMAL. Symmetric with
    // copy_buffer_to_image — the caller owns the surrounding barriers.
    vkCmdCopyImageToBuffer(
        cmd_,
        src_it->second,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst_it->second,
        static_cast<std::uint32_t>(vk_regions.size()),
        vk_regions.data()
    );
}

void VulkanCommandBuffer::barrier(
    std::span<const cd::rhi::BufferBarrier> buffer_barriers,
    std::span<const cd::rhi::TextureBarrier> texture_barriers
)
{
    std::vector<VkBufferMemoryBarrier2> vk_bbs;
    vk_bbs.reserve(buffer_barriers.size());
    for (const auto& bb : buffer_barriers)
    {
        VkBuffer resolved = VK_NULL_HANDLE;
        if (tables_.buffers != nullptr)
        {
            auto it = tables_.buffers->find(bb.buffer.index());
            if (it != tables_.buffers->end())
                resolved = it->second;
        }
        if (resolved == VK_NULL_HANDLE)
            continue;  // skip unresolvable barriers
        vk_bbs.push_back(
            VkBufferMemoryBarrier2 {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = stage_for(bb.from),
                .srcAccessMask = access_for(bb.from),
                .dstStageMask = stage_for(bb.to),
                .dstAccessMask = access_for(bb.to),
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = resolved,
                .offset = bb.offset,
                .size = bb.size == 0 ? VK_WHOLE_SIZE : bb.size,
            }
        );
    }
    std::vector<VkImageMemoryBarrier2> vk_tbs;
    vk_tbs.reserve(texture_barriers.size());
    for (const auto& tb : texture_barriers)
    {
        VkImage resolved = VK_NULL_HANDLE;
        if (tables_.images != nullptr)
        {
            auto it = tables_.images->find(tb.texture.index());
            if (it != tables_.images->end())
                resolved = it->second;
        }
        if (resolved == VK_NULL_HANDLE)
            continue;
        vk_tbs.push_back(VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = stage_for(tb.from),
        .srcAccessMask = access_for(tb.from),
        .dstStageMask = stage_for(tb.to),
        .dstAccessMask = access_for(tb.to),
        .oldLayout = layout_for(tb.from),
        .newLayout = layout_for(tb.to),
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = resolved,
        .subresourceRange = {
            .aspectMask = aspect_for_texture(tables_, tb.texture.index()),
            .baseMipLevel = tb.range.base_mip,
            .levelCount = tb.range.mip_count,
            .baseArrayLayer = tb.range.base_layer,
            .layerCount = tb.range.layer_count,
        },
    });
    }
    if (vk_bbs.empty() && vk_tbs.empty())
        return;  // nothing to record
    const VkDependencyInfo dep {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = static_cast<std::uint32_t>(vk_bbs.size()),
        .pBufferMemoryBarriers = vk_bbs.empty() ? nullptr : vk_bbs.data(),
        .imageMemoryBarrierCount = static_cast<std::uint32_t>(vk_tbs.size()),
        .pImageMemoryBarriers = vk_tbs.empty() ? nullptr : vk_tbs.data(),
    };
    vkCmdPipelineBarrier2(cmd_, &dep);
}

void VulkanCommandBuffer::push_debug_group(std::string_view name)
{
    if (vkCmdBeginDebugUtilsLabelEXT == nullptr)
        return;
    // Per the Vulkan spec pLabelName is consumed at RECORD time (it is
    // not on the retained-pointer list), so a stack buffer would already
    // be legal for conformant drivers. The per-command-buffer
    // std::deque<std::string> arena (stable pointers, cleared at
    // begin()) is defence-in-depth against non-conformant tooling that
    // holds the pointer longer than the spec allows.
    const auto& stored = debug_label_arena_.emplace_back(name);
    const VkDebugUtilsLabelEXT label {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext = nullptr,
        .pLabelName = stored.c_str(),
        .color = { 1.0F, 1.0F, 1.0F, 1.0F },
    };
    vkCmdBeginDebugUtilsLabelEXT(cmd_, &label);
}

void VulkanCommandBuffer::pop_debug_group()
{
    if (vkCmdEndDebugUtilsLabelEXT == nullptr)
        return;
    vkCmdEndDebugUtilsLabelEXT(cmd_);
}

// Phase 765 W2A — F5: vkCmdDrawMeshTasksEXT. Resolved by volk when the
// device was created with VK_EXT_mesh_shader enabled. Guard the call so
// running this method against a device without mesh-shader support is
// silent (matches the rest of the cmd-buffer's "unknown handle" policy).
void VulkanCommandBuffer::draw_mesh_tasks(std::uint32_t group_x,
                                          std::uint32_t group_y,
                                          std::uint32_t group_z)
{
    if (vkCmdDrawMeshTasksEXT == nullptr)
        return;
    vkCmdDrawMeshTasksEXT(cmd_, group_x, group_y, group_z);
}

// Phase 135 — RT pipeline bind.
void VulkanCommandBuffer::bind_rt_pipeline(cd::rhi::RtPipelineHandle pipeline)
{
    if (tables_.rt_pipeline_lookup == nullptr) return;
    VkPipeline vp = tables_.rt_pipeline_lookup(tables_.accel_lookup_user, pipeline.index());
    if (vp == VK_NULL_HANDLE) return;
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vp);
}

// Phase 135 — vkCmdTraceRaysKHR dispatch.
void VulkanCommandBuffer::dispatch_rays(const cd::rhi::DispatchRaysDesc& desc)
{
    if (vkCmdTraceRaysKHR == nullptr || tables_.buffers == nullptr) return;
    auto resolve = [this](const cd::rhi::SbtRegion& r) -> VkStridedDeviceAddressRegionKHR {
        VkStridedDeviceAddressRegionKHR out {};
        if (!r.buffer.is_valid()) return out;
        auto it = tables_.buffers->find(r.buffer.index());
        if (it == tables_.buffers->end()) return out;
        VkBufferDeviceAddressInfo info {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = it->second;
        out.deviceAddress = vkGetBufferDeviceAddress(device_, &info) + r.offset;
        out.stride        = r.stride_bytes;
        out.size          = r.size_bytes;
        return out;
    };
    const auto rg = resolve(desc.raygen);
    const auto ms = resolve(desc.miss);
    const auto hi = resolve(desc.hit);
    const auto ca = resolve(desc.callable);
    vkCmdTraceRaysKHR(cmd_, &rg, &ms, &hi, &ca,
                      desc.width, desc.height, desc.depth);
}

// Phase 132 — vkCmdBuildAccelerationStructuresKHR override.
void VulkanCommandBuffer::build_acceleration_structure(cd::rhi::AccelStructureHandle as)
{
    if (tables_.accel_lookup == nullptr || vkCmdBuildAccelerationStructuresKHR == nullptr)
        return;
    AccelBuildView view {};
    if (!tables_.accel_lookup(tables_.accel_lookup_user, as.index(), view))
        return;
    if (view.as == VK_NULL_HANDLE || view.scratch_device_address == 0)
        return;

    VkAccelerationStructureBuildGeometryInfoKHR bgi {};
    bgi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bgi.mode  = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bgi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bgi.dstAccelerationStructure = view.as;
    bgi.scratchData.deviceAddress = view.scratch_device_address;

    VkAccelerationStructureGeometryKHR instances_geo {};
    VkAccelerationStructureBuildRangeInfoKHR range_one {};

    if (view.is_tlas)
    {
        bgi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        instances_geo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        instances_geo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        instances_geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        instances_geo.geometry.instances.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instances_geo.geometry.instances.arrayOfPointers = VK_FALSE;
        instances_geo.geometry.instances.data.deviceAddress = view.instance_device_address;
        bgi.geometryCount = 1;
        bgi.pGeometries   = &instances_geo;

        range_one.primitiveCount = view.instance_count;
    }
    else
    {
        bgi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        bgi.geometryCount = view.triangle_count;
        bgi.pGeometries   = view.triangle_geos;
    }

    // BLAS may have multiple geometries; allocate a stack array.
    //
    // phase833-rt-chrome-sponza-blas-geo-cap-128:
    // Cap raised from 32 -> 128 to match
    // `cd::hello_engine::kMaxGeomsPerInst` (HelloRayQuery.hpp, bumped to
    // 128 in phase798). Khronos Sponza ships **103 primitives**; the
    // previous 32-cap was silently truncating the BLAS to the first 32
    // prims, so 71/103 prims (most curtain panels, vegetation pots, the
    // lion fountain, the lower arcade carving) were NEVER in the
    // acceleration structure. Ray-query reflections from chrome
    // surfaces had no way to hit those prims and the chrome spheres
    // returned IBL sky / nearby small objects only — the user-reported
    // "asla pbr kurelerde spanzaya ait bir yansima yok" symptom.
    //
    // 128 × 32-byte VkAccelerationStructureBuildRangeInfoKHR = 4 KiB on
    // the stack; well under the typical 1 MiB stack limit. The host-side
    // BLAS-builder loop already supplies `triangle_primitive_counts` per
    // geometry up to whatever the caller passed.
    constexpr std::size_t kMaxBuildGeos = 128;
    // phase833-rt-chrome-sponza-blas-geo-cap-128: compile-time floor.
    // Khronos Sponza ships with 103 primitives; the cap must comfortably
    // cover that and leave headroom for the next multi-geometry showcase
    // mesh. If a future refactor pushes the cap back below the known
    // geo count, the build fails with a pointer to this site so the
    // regression is caught at compile time instead of as "silently
    // missing Sponza prims in chrome reflections".
    constexpr std::size_t kKhronosSponzaPrimCount = 103;
    static_assert(kMaxBuildGeos >= kKhronosSponzaPrimCount,
                  "kMaxBuildGeos must cover the Khronos Sponza primitive set "
                  "so the multi-geometry BLAS does not silently drop prims. "
                  "See ADR W8-BD and phase833.");
    VkAccelerationStructureBuildRangeInfoKHR ranges[kMaxBuildGeos] {};
    const VkAccelerationStructureBuildRangeInfoKHR* range_ptrs[1] { nullptr };

    if (view.is_tlas)
    {
        range_ptrs[0] = &range_one;
    }
    else
    {
        const std::uint32_t n = (view.triangle_count > kMaxBuildGeos)
            ? static_cast<std::uint32_t>(kMaxBuildGeos) : view.triangle_count;
        for (std::uint32_t i = 0; i < n; ++i)
            ranges[i].primitiveCount = view.triangle_primitive_counts[i];
        range_ptrs[0] = ranges;
    }

    vkCmdBuildAccelerationStructuresKHR(cmd_, 1, &bgi, range_ptrs);
}

// Phase 251 — AS-build → AS-build memory barrier. Required when a BLAS
// is rebuilt in-place every frame (skinned mesh) and the TLAS that
// references it is rebuilt later in the same submission: without this
// barrier the TLAS build can race the BLAS write. Vulkan spec requires
// the same stage on both sides of an AS-build dependency.
void VulkanCommandBuffer::acceleration_structure_barrier()
{
    if (vkCmdPipelineBarrier2 == nullptr)
        return;
    const VkMemoryBarrier2 mb {
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                         VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
    };
    const VkDependencyInfo dep {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = nullptr,
        .dependencyFlags          = 0,
        .memoryBarrierCount       = 1,
        .pMemoryBarriers          = &mb,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers    = nullptr,
        .imageMemoryBarrierCount  = 0,
        .pImageMemoryBarriers     = nullptr,
    };
    vkCmdPipelineBarrier2(cmd_, &dep);
}


// ---------------------------------------------------------------------------
// phase1116 (X1-FU-F step 2) — parallel lanes over dynamic rendering.
// ---------------------------------------------------------------------------

std::unique_ptr<cd::rhi::IParallelPassRecorder>
VulkanCommandBuffer::begin_parallel_render_pass(
    const cd::rhi::RenderPassBeginInfo& info, std::uint32_t lane_count)
{
    if (lane_count == 0)
        lane_count = 1;
    if (tables_.view_formats == nullptr)
        return nullptr;  // device didn't plumb formats — serial fallback

    // Resolve attachment formats for the secondaries' inheritance info.
    std::vector<VkFormat> color_formats;
    color_formats.reserve(info.color_attachments.size());
    for (const auto& a : info.color_attachments)
    {
        const auto it = tables_.view_formats->find(a.view.index());
        if (it == tables_.view_formats->end())
            return nullptr;
        color_formats.push_back(it->second);
    }
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    if (info.depth_stencil != nullptr)
    {
        const auto it = tables_.view_formats->find(info.depth_stencil->view.index());
        if (it == tables_.view_formats->end())
            return nullptr;
        depth_format = it->second;
    }

    // Build every lane BEFORE opening the rendering scope so a failure
    // leaves the primary untouched (caller falls back to serial).
    std::vector<VkCommandPool> pools;
    std::vector<VkCommandBuffer> secondaries;
    pools.reserve(lane_count);
    secondaries.reserve(lane_count);
    auto cleanup = [&]
    {
        for (auto pool : pools)
            vkDestroyCommandPool(device_, pool, nullptr);
    };

    VkCommandBufferInheritanceRenderingInfo iri {};
    iri.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
    iri.colorAttachmentCount = static_cast<std::uint32_t>(color_formats.size());
    iri.pColorAttachmentFormats = color_formats.empty() ? nullptr : color_formats.data();
    iri.depthAttachmentFormat = depth_format;
    iri.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    iri.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkCommandBufferInheritanceInfo inh {};
    inh.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inh.pNext = &iri;

    for (std::uint32_t i = 0; i < lane_count; ++i)
    {
        const VkCommandPoolCreateInfo pi {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = tables_.graphics_queue_family,
        };
        VkCommandPool pool { VK_NULL_HANDLE };
        if (vkCreateCommandPool(device_, &pi, nullptr, &pool) != VK_SUCCESS)
        {
            cleanup();
            return nullptr;
        }
        pools.push_back(pool);

        const VkCommandBufferAllocateInfo ai {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer sec { VK_NULL_HANDLE };
        if (vkAllocateCommandBuffers(device_, &ai, &sec) != VK_SUCCESS)
        {
            cleanup();
            return nullptr;
        }
        const VkCommandBufferBeginInfo bi {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                     VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
            .pInheritanceInfo = &inh,
        };
        if (vkBeginCommandBuffer(sec, &bi) != VK_SUCCESS)
        {
            cleanup();
            return nullptr;
        }
        secondaries.push_back(sec);
    }

    begin_rendering_(info, VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT);
    return std::make_unique<VulkanParallelPassRecorder>(
        *this, device_, std::move(pools), std::move(secondaries), tables_);
}

VulkanParallelPassRecorder::VulkanParallelPassRecorder(
    VulkanCommandBuffer& primary, VkDevice device,
    std::vector<VkCommandPool> pools, std::vector<VkCommandBuffer> secondaries,
    ResourceTables tables)
    : primary_ { &primary }
    , device_ { device }
    , pools_ { std::move(pools) }
    , secondaries_ { std::move(secondaries) }
{
    lanes_.reserve(secondaries_.size());
    for (auto sec : secondaries_)
    {
        // pool = VK_NULL_HANDLE: the wrapper must NOT free the secondary
        // (the pool owns it; the pool dies with the primary at retire).
        lanes_.push_back(std::make_unique<VulkanCommandBuffer>(
            device_, VK_NULL_HANDLE, sec, tables));
    }
}

VulkanParallelPassRecorder::~VulkanParallelPassRecorder()
{
    if (!finished_)
    {
        // Never finished: nothing was executed — the pools can die now,
        // but the primary's rendering scope is the CALLER's problem
        // (documented: always finish() a recorder you began).
        for (auto pool : pools_)
            vkDestroyCommandPool(device_, pool, nullptr);
        pools_.clear();
    }
}

std::uint32_t VulkanParallelPassRecorder::lane_count() const noexcept
{
    return static_cast<std::uint32_t>(lanes_.size());
}

cd::rhi::IDrawRecorder& VulkanParallelPassRecorder::lane(std::uint32_t i) noexcept
{
    // phase1119 (audit B2): an out-of-range index silently clamped to the
    // last lane would alias two threads onto one secondary — an external-
    // sync violation that surfaces as device-lost. Fail loudly in debug.
    assert(i < lanes_.size() && "lane index out of range");
    const auto idx = i < lanes_.size() ? i : lanes_.size() - 1u;
    return *lanes_[idx];
}

void VulkanParallelPassRecorder::finish()
{
    if (finished_)
        return;
    // phase1119/1120 (audit C re-audit): per the spec pLabelName is
    // consumed at record time, so lane labels need no lifetime transfer
    // for conformant drivers. The arena handover below mirrors the
    // primary's belt-and-suspenders discipline for non-conformant
    // tooling. NOTE: SSO strings change buffer address on move, so this
    // transfer deliberately does NOT claim execution-time pointer
    // validity — only allocation-lifetime parity with the pools.
    for (auto& l : lanes_)
        primary_->adopt_label_arena(*l);
    for (auto sec : secondaries_)
        vkEndCommandBuffer(sec);
    if (!secondaries_.empty())
    {
        vkCmdExecuteCommands(primary_->native(),
                             static_cast<std::uint32_t>(secondaries_.size()),
                             secondaries_.data());
    }
    primary_->end_render_pass();
    primary_->retire_lane_pools(std::move(pools_));
    finished_ = true;
}

}  // namespace cd::rhi::vulkan
