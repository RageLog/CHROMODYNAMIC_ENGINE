// =============================================================================
// CHROMODYNAMIC — cd/rhi/vulkan/VulkanCommandBuffer.hpp (private)
//
// Sprint S3.4 — wraps a VkCommandBuffer behind cd::rhi::ICommandBuffer.
//
// Lifetime: owned by the VulkanDevice that produced it (the device keeps a
// VkCommandPool per command-buffer instance for simplicity in v1; a per-frame
// pool with vkResetCommandPool is the planned S3.5 evolution).
// =============================================================================
#pragma once

#include <cd/rhi/Enums.hpp>    // cd::rhi::QueueType (multi-queue routing tag)
#include <cd/rhi/Format.hpp>   // cd::rhi::Format (image_formats aspect lookup)
#include <cd/rhi/ICommandBuffer.hpp>
#include <volk.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::rhi::vulkan
{

/// Phase 132 — opaque holder for build-time AS state. Cmd buffer
/// pulls one of these from `ResourceTables::accel_lookup`; pointer
/// fields reference vectors owned by the AccelRecord (lifetime tied
/// to the AS handle).
struct AccelBuildView
{
    VkAccelerationStructureKHR  as { VK_NULL_HANDLE };
    VkDeviceAddress             scratch_device_address { 0 };
    bool                        is_tlas { false };
    VkDeviceAddress             instance_device_address { 0 };
    std::uint32_t               instance_count { 0 };
    const VkAccelerationStructureGeometryKHR* triangle_geos { nullptr };
    const std::uint32_t*                      triangle_primitive_counts { nullptr };
    std::uint32_t                             triangle_count { 0 };
};

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

    /// A-QUERY (Backend-to-100 Wave 3a): query-pool-id -> VkQueryPool. The
    /// cmd-buffer record path (write_timestamp / begin_query / end_query /
    /// reset_query_pool) resolves the handle through this map. Same stability
    /// contract as the other tables (no concurrent mutation during recording).
    const std::unordered_map<std::uint32_t, VkQueryPool>* query_pools { nullptr };

    /// phase1116 (X1-FU-F step 2): view-id -> VkFormat, needed by the
    /// parallel-lane inheritance info (dynamic rendering secondaries
    /// must declare attachment formats up front).
    const std::unordered_map<std::uint32_t, VkFormat>* view_formats { nullptr };

    /// Vulkan V1 (depth-aware barrier fix): texture-id -> cd::rhi::Format.
    /// barrier() and the buffer<->image copy regions must compute the
    /// VkImageAspectFlags from the texture's real format (a depth or
    /// depth+stencil image transitioned/copied with a hardcoded
    /// COLOR aspect is a validation error / silent no-op). Points at the
    /// producing device's image-format map; same stability contract as the
    /// other tables (no concurrent mutation during recording).
    const std::unordered_map<std::uint32_t, cd::rhi::Format>* image_formats { nullptr };
    /// Queue family for per-lane command pools (one pool per lane —
    /// pools are externally synchronized, so each recording thread
    /// needs its own).
    std::uint32_t graphics_queue_family { 0 };

    /// Phase 132 — callback that resolves an AS handle to an
    /// AccelBuildView (BLAS triangles or TLAS instances + scratch).
    /// Returns false on unknown handle.
    using AccelLookupFn = bool(*)(void* user, std::uint32_t handle_index, AccelBuildView& out);
    AccelLookupFn accel_lookup { nullptr };
    void*         accel_lookup_user { nullptr };

    /// Phase 135 — RT pipeline lookup callback. Returns the VkPipeline
    /// (RT) for a given RtPipelineHandle index, or VK_NULL_HANDLE on
    /// unknown handle. Shares `accel_lookup_user` (also VulkanDevice*).
    using RtPipelineLookupFn = VkPipeline (*)(void* user, std::uint32_t handle_index);
    RtPipelineLookupFn rt_pipeline_lookup { nullptr };
};

/// phase1127 (X4-B): shared ResourceState -> VkImageLayout mapping. The
/// command-buffer barrier path and the device-level image readback must
/// agree on this mapping or readback round-trips silently corrupt layouts.
[[nodiscard]] VkImageLayout layout_for_state(cd::rhi::ResourceState state) noexcept;

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

    /// phase1116 (X1-FU-F step 2): Vulkan parallel lanes — dynamic
    /// rendering with SECONDARY_COMMAND_BUFFERS contents; each lane is
    /// a secondary command buffer on its OWN pool (thread-confined),
    /// joined by vkCmdExecuteCommands in lane order at finish().
    /// Returns nullptr (serial fallback) when an attachment format
    /// cannot be resolved or lane setup fails.
    [[nodiscard]] std::unique_ptr<cd::rhi::IParallelPassRecorder>
    begin_parallel_render_pass(const cd::rhi::RenderPassBeginInfo& info,
                               std::uint32_t lane_count) override;

    /// Lane pools survive until this primary is destroyed — secondaries
    /// must outlive the primary's GPU execution.
    void retire_lane_pools(std::vector<VkCommandPool>&& pools);

    /// phase1119 (audit C): move a lane wrapper's debug-label arena into
    /// this (primary) buffer so label strings outlive the recorder and
    /// follow the same begin()-fenced reclamation as retired lane pools.
    void adopt_label_arena(VulkanCommandBuffer& lane);

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

    // A-INDIRECT (Backend-to-100 Wave 3a) — vkCmdDrawIndirect /
    // vkCmdDrawIndexedIndirect / vkCmdDispatchIndirect on the resolved
    // buffer + offset (honours the bound pipeline + index buffer).
    void draw_indirect(cd::rhi::BufferHandle args,
                       std::uint64_t offset,
                       std::uint32_t draw_count,
                       std::uint32_t stride) override;
    void draw_indexed_indirect(cd::rhi::BufferHandle args,
                               std::uint64_t offset,
                               std::uint32_t draw_count,
                               std::uint32_t stride) override;
    void dispatch_indirect(cd::rhi::BufferHandle args, std::uint64_t offset) override;

    // A-QUERY (Backend-to-100 Wave 3a) — VkQueryPool recording: reset +
    // vkCmdWriteTimestamp2 + vkCmdBeginQuery / vkCmdEndQuery.
    void write_timestamp(cd::rhi::QueryPoolHandle pool, std::uint32_t index) override;
    void begin_query(cd::rhi::QueryPoolHandle pool, std::uint32_t index) override;
    void end_query(cd::rhi::QueryPoolHandle pool, std::uint32_t index) override;
    void reset_query_pool(cd::rhi::QueryPoolHandle pool,
                          std::uint32_t first,
                          std::uint32_t count) override;

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
    [[nodiscard]] std::uint32_t debug_group_depth() const noexcept override;

    // Phase 765 W2A — F5: vkCmdDrawMeshTasksEXT.
    void draw_mesh_tasks(std::uint32_t group_x,
                         std::uint32_t group_y,
                         std::uint32_t group_z) override;

    // Phase 132 — vkCmdBuildAccelerationStructuresKHR override.
    void build_acceleration_structure(cd::rhi::AccelStructureHandle as) override;

    // Phase 251 — vkCmdPipelineBarrier2 AS-build → AS-build memory barrier.
    void acceleration_structure_barrier() override;

    // Phase 135 — RT pipeline bind + vkCmdTraceRaysKHR dispatch.
    void bind_rt_pipeline(cd::rhi::RtPipelineHandle pipeline) override;
    void dispatch_rays(const cd::rhi::DispatchRaysDesc& desc) override;

    [[nodiscard]] VkCommandBuffer native() const noexcept
    {
        return cmd_;
    }

    /// Vulkan V2 (multi-queue model): the QueueType this buffer was allocated
    /// for. submit() reads it to route the VkSubmitInfo2 to the matching
    /// VkQueue. Defaults to kGraphics so the existing graphics submit path —
    /// and the parallel-lane secondaries, which are never submitted directly —
    /// stay byte-identical. The producing device sets this immediately after
    /// construction in do_create_command_buffer(QueueType).
    void set_queue_type(cd::rhi::QueueType q) noexcept { queue_type_ = q; }
    [[nodiscard]] cd::rhi::QueueType queue_type() const noexcept { return queue_type_; }

private:
    VkDevice device_ { VK_NULL_HANDLE };
    VkCommandPool pool_ { VK_NULL_HANDLE };
    VkCommandBuffer cmd_ { VK_NULL_HANDLE };
    ResourceTables tables_ {};
    // Vulkan V2 (multi-queue model) — submit() routing tag. See set_queue_type.
    cd::rhi::QueueType queue_type_ { cd::rhi::QueueType::kGraphics };
    // bind_descriptor_set / push_constants need a VkPipelineLayout but the
    // ICommandBuffer signature doesn't accept one — these caches remember the
    // layout that the most-recent bind_*_pipeline call implied.
    VkPipelineLayout current_graphics_layout_ { VK_NULL_HANDLE };
    VkPipelineLayout current_compute_layout_ { VK_NULL_HANDLE };

    // Per-command-buffer storage for debug-group label strings. Per the
    // Vulkan spec (Fundamentals, "Application Memory Lifetime"),
    // VkDebugUtilsLabelEXT::pLabelName is CONSUMED AT RECORD TIME —
    // conformant drivers/layers deep-copy it before vkCmdBegin...Label
    // returns. The arena is therefore belt-and-suspenders against
    // non-conformant tooling, not a spec requirement: it keeps each
    // label's c_str() valid for the lifetime of this command buffer
    // (std::deque guarantees pointer stability across push_back).
    // Cleared in begin(); the next begin() reuses the allocated nodes.
    // (phase1120: earlier comments here claimed the driver may read the
    // pointer "until execution completes" — that misread the spec.)
    std::deque<std::string> debug_label_arena_ {};

    // V-DBGDEPTH: balanced push/pop nesting depth, mirroring the D3D12
    // backend's tracker. ++ in push_debug_group, guarded -- in
    // pop_debug_group (clamps at 0 so an unmatched pop is a safe no-op),
    // reset to 0 in begin() so a stale depth from a previous (possibly
    // unbalanced) recording never leaks into the next. The base
    // ICommandBuffer getter returns 0; this override exposes the real value.
    std::uint32_t debug_group_depth_ { 0 };

    /// phase1116: pools handed over by finished parallel recorders;
    /// destroyed in the dtor (which implies GPU completion in the
    /// engine's wait-before-destroy usage).
    std::vector<VkCommandPool> retired_lane_pools_ {};

    friend class VulkanParallelPassRecorder;
    void begin_rendering_(const cd::rhi::RenderPassBeginInfo& info,
                          VkRenderingFlags flags);
};

/// phase1116 — Vulkan lanes. Construction allocates one pool +
/// secondary per lane and begins them with RENDER_PASS_CONTINUE +
/// dynamic-rendering inheritance; the primary's rendering scope is
/// opened by begin_parallel_render_pass BEFORE this object is created.
/// finish() ends the secondaries, executes them in lane order, ends
/// the primary's rendering and retires the pools to the primary.
class VulkanParallelPassRecorder final : public cd::rhi::IParallelPassRecorder
{
public:
    VulkanParallelPassRecorder(VulkanCommandBuffer& primary,
                               VkDevice device,
                               std::vector<VkCommandPool> pools,
                               std::vector<VkCommandBuffer> secondaries,
                               ResourceTables tables);
    ~VulkanParallelPassRecorder() override;

    [[nodiscard]] std::uint32_t lane_count() const noexcept override;
    [[nodiscard]] cd::rhi::IDrawRecorder& lane(std::uint32_t i) noexcept override;
    void finish() override;

private:
    VulkanCommandBuffer* primary_;
    VkDevice device_;
    std::vector<VkCommandPool> pools_;
    std::vector<VkCommandBuffer> secondaries_;
    std::vector<std::unique_ptr<VulkanCommandBuffer>> lanes_;
    bool finished_ { false };
};

}  // namespace cd::rhi::vulkan
