// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/VulkanCommandBuffer.cpp
// =============================================================================
#include "VulkanCommandBuffer.hpp"

#include <cstring>

namespace cd::rhi_vulkan
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

}  // namespace

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
    if (cmd_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(device_, pool_, 1, &cmd_);
    }
}

void VulkanCommandBuffer::begin()
{
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
    ri.flags = 0;
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

void VulkanCommandBuffer::bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle h)
{
    if (tables_.graphics_pipelines == nullptr)
        return;
    auto it = tables_.graphics_pipelines->find(h.index());
    if (it == tables_.graphics_pipelines->end())
        return;
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second);
    // Remember the layout for follow-up bind_descriptor_set / push_constants.
    if (tables_.pipeline_to_layout != nullptr)
    {
        auto layout_it = tables_.pipeline_to_layout->find(h.index());
        if (layout_it != tables_.pipeline_to_layout->end())
        {
            current_graphics_layout_ = layout_it->second;
        }
    }
}

void VulkanCommandBuffer::bind_compute_pipeline(cd::rhi::ComputePipelineHandle h)
{
    if (tables_.compute_pipelines == nullptr)
        return;
    auto it = tables_.compute_pipelines->find(h.index());
    if (it == tables_.compute_pipelines->end())
        return;
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, it->second);
    if (tables_.pipeline_to_layout != nullptr)
    {
        auto layout_it = tables_.pipeline_to_layout->find(h.index());
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
    const VkPipelineLayout layout =
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
        vr.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
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
    // VkDebugUtilsLabelEXT.pLabelName requires a C string. Copy into a small
    // buffer to ensure null termination; debug labels are short by convention.
    char buf[128];
    const auto n = name.size() < sizeof(buf) - 1 ? name.size() : sizeof(buf) - 1;
    std::memcpy(buf, name.data(), n);
    buf[n] = '\0';
    const VkDebugUtilsLabelEXT label {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext = nullptr,
        .pLabelName = buf,
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

}  // namespace cd::rhi_vulkan
