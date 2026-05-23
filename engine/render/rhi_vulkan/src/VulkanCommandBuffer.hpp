// =============================================================================
// CHROMODYNAMIC — cd/rhi_vulkan/VulkanCommandBuffer.hpp (private)
//
// Sprint S3.4 — wraps a VkCommandBuffer behind cd::rhi::ICommandBuffer.
//
// Lifetime: owned by the VulkanDevice that produced it (the device keeps a
// VkCommandPool per command-buffer instance for simplicity in v1; a per-frame
// pool with vkResetCommandPool is the planned S3.5 evolution).
// =============================================================================
#pragma once

#include <cd/rhi/ICommandBuffer.hpp>
#include <volk.h>

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi_vulkan
{

/// Non-owning views into the producing VulkanDevice's resource tables. The
/// command buffer uses these to translate engine-side handles (BufferHandle,
/// TextureHandle, etc.) to native Vk objects at record time. Pointers must
/// remain valid for the lifetime of the device that issued the command
/// buffer; concurrent map mutation during recording is not allowed.
struct ResourceTables
{
    const std::unordered_map<std::uint32_t, VkBuffer>* buffers { nullptr };
    const std::unordered_map<std::uint32_t, VkImage>* images { nullptr };
    const std::unordered_map<std::uint32_t, VkImageView>* views { nullptr };
    const std::unordered_map<std::uint32_t, VkPipeline>* graphics_pipelines { nullptr };
    const std::unordered_map<std::uint32_t, VkPipeline>* compute_pipelines { nullptr };
    const std::unordered_map<std::uint32_t, VkPipelineLayout>* pipeline_layouts { nullptr };
    /// pipeline-id → VkPipelineLayout it was built against. Lets
    /// bind_descriptor_set look up the layout implied by the most-recently
    /// bound pipeline without the caller passing it again.
    const std::unordered_map<std::uint32_t, VkPipelineLayout>* pipeline_to_layout { nullptr };
    const std::unordered_map<std::uint32_t, VkDescriptorSet>* descriptor_sets { nullptr };
};

class VulkanCommandBuffer final : public cd::rhi::ICommandBuffer
{
public:
    VulkanCommandBuffer(
        VkDevice device,
        VkCommandPool pool,
        VkCommandBuffer command_buffer,
        ResourceTables tables
    ) noexcept;
    ~VulkanCommandBuffer() override;

    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer&&) = delete;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&&) = delete;

    // ICommandBuffer
    void begin() override;
    void end() override;

    void begin_render_pass(const cd::rhi::RenderPassBeginInfo& info) override;
    void end_render_pass() override;

    void bind_graphics_pipeline(cd::rhi::GraphicsPipelineHandle pipeline) override;
    void bind_compute_pipeline(cd::rhi::ComputePipelineHandle pipeline) override;
    void bind_descriptor_set(std::uint32_t set_index, cd::rhi::DescriptorSetHandle set) override;
    void bind_vertex_buffer(std::uint32_t binding, cd::rhi::BufferHandle buffer, std::uint64_t offset) override;
    void bind_index_buffer(cd::rhi::BufferHandle buffer, std::uint64_t offset, cd::rhi::IndexType type) override;
    void push_constants(
        cd::rhi::PipelineLayoutHandle layout,
        cd::rhi::ShaderStage stages,
        std::uint32_t offset,
        std::uint32_t size,
        const void* data
    ) override;

    void set_viewport(const cd::rhi::Viewport& vp) override;
    void set_scissor(const cd::rhi::Rect2D& rect) override;

    void draw(
        std::uint32_t vertex_count,
        std::uint32_t instance_count,
        std::uint32_t first_vertex,
        std::uint32_t first_instance
    ) override;
    void draw_indexed(
        std::uint32_t index_count,
        std::uint32_t instance_count,
        std::uint32_t first_index,
        std::int32_t vertex_offset,
        std::uint32_t first_instance
    ) override;
    void dispatch(std::uint32_t group_x, std::uint32_t group_y, std::uint32_t group_z) override;

    void copy_buffer(
        cd::rhi::BufferHandle src,
        cd::rhi::BufferHandle dst,
        std::span<const cd::rhi::BufferCopyRegion> regions
    ) override;

    void copy_buffer_to_image(
        cd::rhi::BufferHandle src,
        cd::rhi::TextureHandle dst,
        std::span<const cd::rhi::BufferImageCopyRegion> regions
    ) override;

    void copy_image_to_buffer(
        cd::rhi::TextureHandle src,
        cd::rhi::BufferHandle dst,
        std::span<const cd::rhi::BufferImageCopyRegion> regions
    ) override;

    void barrier(
        std::span<const cd::rhi::BufferBarrier> buffer_barriers,
        std::span<const cd::rhi::TextureBarrier> texture_barriers
    ) override;

    void push_debug_group(std::string_view name) override;
    void pop_debug_group() override;

    [[nodiscard]] VkCommandBuffer native() const noexcept
    {
        return cmd_;
    }

private:
    VkDevice device_ { VK_NULL_HANDLE };
    VkCommandPool pool_ { VK_NULL_HANDLE };
    VkCommandBuffer cmd_ { VK_NULL_HANDLE };
    ResourceTables tables_ {};
    // bind_descriptor_set / push_constants need a VkPipelineLayout but the
    // ICommandBuffer signature doesn't accept one — these caches remember the
    // layout that the most-recent bind_*_pipeline call implied.
    VkPipelineLayout current_graphics_layout_ { VK_NULL_HANDLE };
    VkPipelineLayout current_compute_layout_ { VK_NULL_HANDLE };

    // Per-command-buffer storage for debug-group label strings. The
    // Vulkan spec lets the driver read VkDebugUtilsLabelEXT::pLabelName
    // up until command-buffer execution completes, so a stack buffer in
    // push_debug_group() would dangle. `std::deque<std::string>`
    // guarantees pointer stability across push_back (unlike std::vector)
    // so each label's c_str() remains valid for the lifetime of this
    // command buffer. Cleared in begin(); the next begin() reuses the
    // already-allocated nodes.
    std::deque<std::string> debug_label_arena_ {};
};

}  // namespace cd::rhi_vulkan
