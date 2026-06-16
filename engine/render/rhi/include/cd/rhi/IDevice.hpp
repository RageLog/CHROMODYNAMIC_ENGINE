// =============================================================================
// CHROMODYNAMIC — cd/rhi/IDevice.hpp
// ADR-001 (Sprint S3.0) — RHI device interface.
//
// IDevice is the per-process abstraction over a logical GPU device. Back-ends
// (Vulkan, D3D12, Metal, GL) implement this interface; the rest of the engine
// programs against the interface and never sees `vk*` / `d3d12*` symbols.
//
// Resource lifecycle:
//   * `create_*` returns a typed handle (or null on failure).
//   * `destroy_*` releases the resource and invalidates the handle.
//   * The device guarantees the handle stays valid until destroy() is called,
//     even across frames.
//
// Memory ownership:
//   * Device-allocated memory is owned by the device. Handles do not free on
//     scope exit; callers must call destroy_*. RAII wrappers can wrap a
//     `IDevice*` + handle if desired.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>  // complete type — the inline NVI
                                      // create_command_buffer() wrapper
                                      // returns unique_ptr<ICommandBuffer>
#include <cd/rhi/Pipeline.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace cd::rhi
{

// ---- Submit description (S3.6) ------------------------------------------
/// Wait/signal hookup for a queue submit. Binary and timeline semaphores
/// live in separate spans so the type system catches misuse — Vulkan stores
/// both flavors in the same VkSemaphore handle, but the engine treats them
/// as distinct kinds.
struct SemaphoreSubmit
{
    SemaphoreHandle semaphore {};
};

struct TimelineSemaphoreSubmit
{
    TimelineSemaphoreHandle semaphore {};
    std::uint64_t value { 0 };
};

struct SubmitDesc
{
    /// Command buffers to execute, in order. Empty submits are legal (Vulkan
    /// permits them) — they still consume sync hand-offs.
    std::span<class ICommandBuffer* const> command_buffers {};
    std::span<const SemaphoreSubmit> wait_semaphores {};
    std::span<const SemaphoreSubmit> signal_semaphores {};
    std::span<const TimelineSemaphoreSubmit> wait_timeline_semaphores {};
    std::span<const TimelineSemaphoreSubmit> signal_timeline_semaphores {};
    /// CPU-visible completion signal. Optional; pass {} to skip.
    FenceHandle signal_fence {};
};

enum class Backend : std::uint8_t
{
    kNone,
    kVulkan,
    kD3D12,
    kMetal,
    kOpenGL,
    kNull,  // headless test device — accepts everything, draws nothing
};

namespace rhi_errors
{
inline constexpr std::uint32_t kDomain = 0x0009;

enum class Code : std::uint32_t
{
    kOk = 0,
    kBackendInitFailed = 1,
    kNoSuitableAdapter = 2,
    kResourceCreationFailed = 3,
    kInvalidArgument = 4,
    kSwapchainOutOfDate = 5,
    kDeviceLost = 6,
    /// Feature recognized but not yet implemented by this backend version.
    /// Distinct from kInvalidArgument so callers can pick a sensible fallback
    /// instead of treating it as a programming bug.
    kNotImplemented = 7,
    /// Wait operation exceeded its deadline. Caller may retry.
    kTimeout = 8,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}

/// Owning-message variant for diagnostics built at run time (e.g.
/// "D3D12 init failed HRESULT 0x80...", "vkCreateInstance returned ...").
/// Allocates a shared_ptr so the message survives the caller's local
/// string going out of scope — mirrors the same pattern in
/// shader_errors / material_errors / render_errors.
[[nodiscard]] inline cd::core::ErrorCode make_owning(Code c, std::string m)
{
    return cd::core::ErrorCode::make_owning(kDomain, static_cast<std::uint32_t>(c), std::move(m));
}
}  // namespace rhi_errors

// -----------------------------------------------------------------------------
// Threading contract (X1-FU-G spec audit, phase 1075 — see
// research/reports/X1FUG_upload_buffer_thread_safety_audit.md):
//   1. Resource lifetime calls (create_* / destroy_*) require EXTERNAL
//      synchronization with every other IDevice call — backend handle
//      tables are plain maps and the id counter is non-atomic by design
//      while the engine is single-resource-threaded.
//   2. upload_buffer / download_buffer are thread-COMPATIBLE: concurrent
//      calls on DISTINCT handles are allowed provided rule 1 holds (no
//      create/destroy in flight). Vulkan's copy is VMA-internally
//      synchronized; the race surface is the handle lookup, not the copy.
//   3. Concurrent same-handle uploads are the caller's responsibility;
//      overlapping ranges are always a bug.
//   4. CPU↔GPU timeline hazards (uploading while a previous frame still
//      reads) stay with the caller — frames-in-flight fencing or the
//      park-margin convention (ADR-20260611-debug-line-draw-pair).
// -----------------------------------------------------------------------------
class IDevice
{
public:
    IDevice() noexcept = default;
    virtual ~IDevice() = default;
    IDevice(const IDevice&) = delete;
    IDevice& operator=(const IDevice&) = delete;
    IDevice(IDevice&&) = delete;
    IDevice& operator=(IDevice&&) = delete;

    // ---- Introspection -----------------------------------------------------

    [[nodiscard]] virtual Backend backend() const noexcept = 0;
    [[nodiscard]] virtual std::string_view adapter_name() const noexcept = 0;
    [[nodiscard]] virtual const DeviceLimits& limits() const noexcept = 0;
    [[nodiscard]] virtual const DeviceFeatures& features() const noexcept = 0;

    // ---- Resource creation -------------------------------------------------

    [[nodiscard]] virtual cd::core::Result<BufferHandle> create_buffer(const BufferDesc& desc) = 0;
    virtual void destroy_buffer(BufferHandle h) = 0;

    [[nodiscard]] virtual cd::core::Result<TextureHandle> create_texture(const TextureDesc& desc) = 0;
    virtual void destroy_texture(TextureHandle h) = 0;

    [[nodiscard]] virtual cd::core::Result<TextureViewHandle> create_texture_view(const TextureViewDesc& desc) = 0;
    virtual void destroy_texture_view(TextureViewHandle h) = 0;

    [[nodiscard]] virtual cd::core::Result<SamplerHandle> create_sampler(const SamplerDesc& desc) = 0;
    virtual void destroy_sampler(SamplerHandle h) = 0;

    [[nodiscard]] virtual cd::core::Result<ShaderModuleHandle> create_shader_module(const ShaderModuleDesc& desc) = 0;
    virtual void destroy_shader_module(ShaderModuleHandle h) = 0;

    [[nodiscard]] virtual cd::core::Result<DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const DescriptorSetLayoutDesc& desc) = 0;
    virtual void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) = 0;

    [[nodiscard]] virtual cd::core::Result<PipelineLayoutHandle>
    create_pipeline_layout(const PipelineLayoutDesc& desc) = 0;
    virtual void destroy_pipeline_layout(PipelineLayoutHandle h) = 0;

    [[nodiscard]] virtual cd::core::Result<GraphicsPipelineHandle>
    create_graphics_pipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual void destroy_graphics_pipeline(GraphicsPipelineHandle h) = 0;

    [[nodiscard]] virtual cd::core::Result<ComputePipelineHandle>
    create_compute_pipeline(const ComputePipelineDesc& desc) = 0;
    virtual void destroy_compute_pipeline(ComputePipelineHandle h) = 0;

    // ---- Descriptor sets (S3.4) -------------------------------------------
    /// Allocate a descriptor set from the device's internal pool, matching the
    /// supplied layout. The set is owned by the device and freed automatically
    /// at device destruction; callers may release earlier via destroy_descriptor_set.
    [[nodiscard]] virtual cd::core::Result<DescriptorSetHandle>
    allocate_descriptor_set(DescriptorSetLayoutHandle layout) = 0;
    virtual void destroy_descriptor_set(DescriptorSetHandle h) = 0;

    /// Apply a batch of writes to a descriptor set. The backend resolves
    /// BufferHandle / TextureViewHandle / SamplerHandle to native objects.
    /// Returns kInvalidArgument when any referenced handle is unknown.
    [[nodiscard]] virtual cd::core::Result<void>
    update_descriptor_set(DescriptorSetHandle set, std::span<const DescriptorWrite> writes) = 0;

    // ---- Synchronization primitives (S3.5) -------------------------------
    /// Create a binary semaphore (initial value undefined; signaled by submit,
    /// reset by wait). Use these for swapchain image acquire/present hand-off.
    [[nodiscard]] virtual cd::core::Result<SemaphoreHandle> create_semaphore() = 0;
    virtual void destroy_semaphore(SemaphoreHandle h) = 0;

    /// Create a fence in either signaled or unsignaled state. Fences are the
    /// CPU-side completion signal for queue submissions.
    [[nodiscard]] virtual cd::core::Result<FenceHandle> create_fence(bool signaled) = 0;
    virtual void destroy_fence(FenceHandle h) = 0;

    /// Wait for `fence` to enter the signaled state. `timeout_ns == ~0ULL`
    /// means wait forever; returns kTimeout if the deadline expires.
    [[nodiscard]] virtual cd::core::Result<void> wait_for_fence(FenceHandle fence, std::uint64_t timeout_ns) = 0;

    /// Reset a fence back to unsignaled. Caller must guarantee no in-flight
    /// queue submission references it.
    virtual void reset_fence(FenceHandle fence) = 0;

    /// Non-blocking status query: true if the fence is currently signaled.
    [[nodiscard]] virtual bool is_fence_signaled(FenceHandle fence) = 0;

    // ---- Timeline semaphores (Vulkan 1.2+ core) ---------------------------
    /// Create a timeline semaphore with a monotonically increasing 64-bit
    /// counter. Unlike binary semaphores, timelines support host wait/signal/
    /// peek, can gate multi-queue work without rebinding, and replace fences
    /// for most CPU/GPU sync — the SOTA path forward for submit dependencies.
    [[nodiscard]] virtual cd::core::Result<TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t initial_value) = 0;
    virtual void destroy_timeline_semaphore(TimelineSemaphoreHandle h) = 0;

    /// Host-side: block until the semaphore reaches `value` (or higher).
    /// Returns kTimeout when the deadline expires; `timeout_ns == ~0ULL`
    /// waits indefinitely.
    [[nodiscard]] virtual cd::core::Result<void>
    wait_timeline_semaphore(TimelineSemaphoreHandle h, std::uint64_t value, std::uint64_t timeout_ns) = 0;

    /// Host-side: signal the semaphore to `value`. The new value must be
    /// strictly greater than the current value (timelines are monotonic);
    /// kInvalidArgument is returned otherwise.
    [[nodiscard]] virtual cd::core::Result<void>
    signal_timeline_semaphore(TimelineSemaphoreHandle h, std::uint64_t value) = 0;

    /// Non-blocking: read the current counter value. Returns 0 for an unknown
    /// handle so polling callers don't need separate validity checks.
    [[nodiscard]] virtual std::uint64_t timeline_semaphore_value(TimelineSemaphoreHandle h) const = 0;

    // ---- Swapchain acquire / present (S3.5) -------------------------------
    /// Acquire the next presentable image from `swapchain`. The returned index
    /// is a slot into the swapchain's image array (use `swapchain_image_view`
    /// to access the matching view). `signal` is signaled when the image is
    /// safe to render into; `fence` is signaled when acquire is complete on
    /// the CPU. Either may be `{}` (null) to skip that signal.
    /// Returns kSwapchainOutOfDate when the swapchain needs recreation.
    [[nodiscard]] virtual cd::core::Result<std::uint32_t> acquire_next_image(
        SwapchainHandle swapchain,
        SemaphoreHandle signal,
        FenceHandle fence,
        std::uint64_t timeout_ns
    ) = 0;

    /// Present a swapchain image to the surface. `wait` is the list of
    /// semaphores the present engine must wait on (typically the one signaled
    /// by the final submit of the frame). Returns kSwapchainOutOfDate when the
    /// swapchain needs recreation.
    [[nodiscard]] virtual cd::core::Result<void>
    present(SwapchainHandle swapchain, std::uint32_t image_index, std::span<const SemaphoreHandle> wait) = 0;

    /// Resize the swapchain's back-buffers in place to (`width`, `height`)
    /// WITHOUT tearing down the underlying surface — the cheaper alternative
    /// to destroy + recreate on a window-resize. `width == 0 || height == 0`
    /// is a no-op refresh of the current extent (validates the chain is
    /// healthy). On success the swapchain's image/view handles are re-issued
    /// (call `swapchain_image_view` / `swapchain_image` again). Returns
    /// kSwapchainOutOfDate when the in-place resize is rejected and the caller
    /// must fall back to a full destroy + create_swapchain. Backends without
    /// an in-place resize primitive return kNotImplemented (the caller then
    /// uses the destroy/recreate path); the base default reports that so
    /// non-D3D12 backends need no override.
    [[nodiscard]] virtual cd::core::Result<void>
    resize_swapchain(SwapchainHandle /*swapchain*/, std::uint32_t /*width*/, std::uint32_t /*height*/)
    {
        return std::unexpected(cd::rhi::rhi_errors::make(
            cd::rhi::rhi_errors::Code::kNotImplemented,
            "resize_swapchain: backend has no in-place resize; use "
            "destroy_swapchain + create_swapchain"));
    }

    /// Look up the image view for a specific swapchain slot. Returns an invalid
    /// handle if the swapchain or index is unknown.
    [[nodiscard]] virtual TextureViewHandle
    swapchain_image_view(SwapchainHandle swapchain, std::uint32_t image_index) const = 0;

    /// Number of images owned by the swapchain (≥ image_count requested at
    /// create time; the driver may return more).
    [[nodiscard]] virtual std::uint32_t swapchain_image_count(SwapchainHandle swapchain) const = 0;

    /// Texture handle for the raw swapchain image at `index`. Lets callers
    /// transition the image (e.g. UNDEFINED → COLOR_ATTACHMENT → PRESENT) via
    /// barrier(). Returns an invalid handle if the swapchain or index is
    /// unknown. The image is *owned by the swapchain*, not by the caller —
    /// do NOT call destroy_texture on the result.
    [[nodiscard]] virtual TextureHandle swapchain_image(SwapchainHandle swapchain, std::uint32_t image_index) const = 0;

    // ---- Buffer staging ----------------------------------------------------

    /// Write to a CPU-visible (kCpuToGpu) buffer region. Returns invalid arg
    /// error if the buffer is GPU-only.
    /// Threading: thread-compatible — see the class-level contract (rules
    /// 1-4); distinct handles may upload concurrently, same-handle
    /// concurrency and GPU-timeline fencing are the caller's job.
    [[nodiscard]] virtual cd::core::Result<void>
    upload_buffer(BufferHandle h, std::uint64_t offset, std::span<const std::byte> data) = 0;

    /// Read from a CPU-visible (kGpuToCpu / kCpuRandomAccess) buffer region.
    /// `dst.size()` controls the byte count. Returns kInvalidArgument for an
    /// unknown handle, an OOB range, or a GPU-only buffer. Backends with
    /// deferred GPU writes (Vulkan) must flush the queue before mapping;
    /// callers should usually wait_idle() before this call so the data is
    /// fresh.
    [[nodiscard]] virtual cd::core::Result<void>
    download_buffer(BufferHandle h, std::uint64_t offset, std::span<std::byte> dst) = 0;

    // ---- Swapchain ---------------------------------------------------------

    [[nodiscard]] virtual cd::core::Result<SwapchainHandle> create_swapchain(const SwapchainDesc& desc) = 0;
    virtual void destroy_swapchain(SwapchainHandle h) = 0;

    // ---- Frame -------------------------------------------------------------

    /// Block until the device is idle. Useful before destruction.
    virtual void wait_idle() = 0;

    // ---- Command buffers ---------------------------------------------------

    /// Acquire a recordable command buffer for the given queue. Ownership is
    /// returned to the device when the buffer is destroyed.
    [[nodiscard]] std::unique_ptr<class ICommandBuffer>
    create_command_buffer(QueueType queue = QueueType::kGraphics)
    {
        return do_create_command_buffer(queue);
    }

    /// NVI hook for create_command_buffer() — the default queue argument
    /// lives on the non-virtual wrapper above (google-default-arguments).
    [[nodiscard]] virtual std::unique_ptr<class ICommandBuffer>
    do_create_command_buffer(QueueType queue) = 0;

    /// Submit a recorded command buffer to the device's queue.
    virtual void submit(ICommandBuffer& cmd) = 0;

    /// Full-fidelity submit: command buffers + binary/timeline semaphore
    /// wait/signal hookups + optional CPU completion fence. Modeled directly
    /// on Vulkan's VkSubmitInfo2 so backends translate one-to-one.
    /// Returns kInvalidArgument when any referenced handle is unknown.
    [[nodiscard]] virtual cd::core::Result<void> submit(const SubmitDesc& desc) = 0;

    // ---- Image readback (phase377-B-infra2) --------------------------------

    /// Pixel region within a single mip/layer of a texture.
    /// All coordinates are in texels relative to the top-left corner.
    struct ImageRegion
    {
        std::uint32_t x { 0 };
        std::uint32_t y { 0 };
        std::uint32_t width { 0 };
        std::uint32_t height { 0 };
        std::uint32_t mip_level { 0 };
        std::uint32_t base_layer { 0 };
        /// phase1127 (X4-B): the state/layout the CALLER left the image
        /// in. The backend transitions src_state -> transfer-source ->
        /// src_state around the copy. The default kUndefined preserves
        /// the legacy phase377 semantics — but per the Vulkan spec a
        /// transition FROM kUndefined may DISCARD the image contents,
        /// so callers reading back RENDERED results MUST pass the true
        /// current state (e.g. kShaderResource). The D3D12 backend
        /// tracks per-resource state internally and ignores this field.
        ResourceState src_state { ResourceState::kUndefined };
    };

    /// Copy a rectangular region of `src_image` into `dst_buffer` at
    /// `dst_offset`. `dst_buffer` must have been created with
    /// MemoryUsage::kGpuToCpu (readback heap).
    ///
    /// **Layout responsibility (Vulkan):** the Vulkan backend issues its own
    /// pipeline barriers to transition `src_image` from UNDEFINED to
    /// TRANSFER_SRC_OPTIMAL and back to SHADER_READ_ONLY_OPTIMAL inside a
    /// one-shot command buffer. Callers do NOT need to pre-transition the image.
    /// The D3D12 backend uses the per-resource state tracked in TextureRecord.
    ///
    /// The call blocks until the copy completes (one-shot submit + wait_idle).
    /// For non-blocking readback, use the command-buffer-level copy API (future).
    /// Backends that do not implement readback return kNotImplemented;
    /// callers should gate on feature availability or backend type.
    [[nodiscard]] virtual cd::core::Result<void> copy_image_to_buffer(
        TextureHandle    src_image,
        BufferHandle     dst_buffer,
        std::uint64_t    dst_offset,
        const ImageRegion& region
    )
    {
        (void)src_image;
        (void)dst_buffer;
        (void)dst_offset;
        (void)region;
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "copy_image_to_buffer: not implemented by this backend"));
    }

    // ---- GPU query pool (A-QUERY, Backend-to-100 Wave 3a) -----------------
    //
    // Allocate / release / read a pool of GPU queries (timestamp /
    // pipeline-statistics / occlusion). The command buffer records into the
    // pool (write_timestamp / begin_query / end_query); after the producing
    // submit completes the host reads the values via get_query_results.
    //
    // Callers gate on the matching DeviceFeatures flag (timestamp_queries /
    // pipeline_statistics_queries) before creating a pool. Backends without a
    // query implementation return kNotImplemented; the base default reports
    // that so non-implementing backends (and Null) need no override.

    [[nodiscard]] virtual cd::core::Result<QueryPoolHandle>
    create_query_pool(const QueryPoolDesc& /*desc*/)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "create_query_pool: backend has no GPU-query implementation"));
    }

    virtual void destroy_query_pool(QueryPoolHandle /*h*/) {}

    /// Read `count` resolved query values from `pool` starting at `first` into
    /// `out` (one std::uint64_t per query). For kTimestamp pools the value is
    /// in NANOSECONDS (the backend applies the device timestamp period). For
    /// kOcclusion pools it is the passing-sample count. `out.size()` must be
    /// >= `count`. Blocks until the values are ready is NOT implied — the
    /// caller must wait_for_fence / wait_idle on the producing submit first;
    /// the backend reads whatever is currently resolved. Returns
    /// kNotImplemented on backends without a query path, kInvalidArgument on an
    /// unknown handle / out-of-range slice / undersized `out`.
    [[nodiscard]] virtual cd::core::Result<void>
    get_query_results(QueryPoolHandle /*pool*/,
                      std::uint32_t /*first*/,
                      std::uint32_t /*count*/,
                      std::span<std::uint64_t> /*out*/)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "get_query_results: backend has no GPU-query implementation"));
    }

    // ---- Ray tracing (Phase 14.G — API shape only at v0.40.0) -------------
    //
    // Backends that don't yet implement RT return kNotImplemented from
    // these entry points; callers gate on `features().ray_tracing` before
    // touching them. The default non-pure-virtual implementation lets
    // existing backends compile without re-implementing the surface
    // until they're ready.

    [[nodiscard]] virtual cd::core::Result<AccelStructureHandle>
    create_acceleration_structure(const AccelStructureDesc& /*desc*/)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "create_acceleration_structure: backend has no RT implementation"));
    }

    virtual void destroy_acceleration_structure(AccelStructureHandle /*h*/) {}

    // Phase 134 — ray-tracing pipeline. Backends without RT return
    // kNotImplemented; callers gate on `features().ray_tracing`.
    [[nodiscard]] virtual cd::core::Result<RtPipelineHandle>
    create_rt_pipeline(const RtPipelineDesc& /*desc*/,
                      PipelineLayoutHandle /*layout*/)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "create_rt_pipeline: backend has no RT implementation"));
    }

    virtual void destroy_rt_pipeline(RtPipelineHandle /*h*/) {}

    /// Phase 134 — RT shader-group handle size + alignment for SBT
    /// authoring. Returns 0 on backends without RT.
    [[nodiscard]] virtual std::uint32_t rt_shader_group_handle_size() const noexcept { return 0; }
    [[nodiscard]] virtual std::uint32_t rt_shader_group_handle_alignment() const noexcept { return 0; }
    [[nodiscard]] virtual std::uint32_t rt_shader_group_base_alignment() const noexcept { return 0; }

    /// D-MIPSTATE (Backend-to-100 Wave 1) — DEBUG/TEST observability of the
    /// backend's per-subresource resource-state tracking. Returns an
    /// opaque, backend-specific encoding of the CURRENT tracked state of the
    /// (texture, mip, layer) subresource (on D3D12 the D3D12_RESOURCE_STATES
    /// value). Two subresources that the backend tracks in DIFFERENT states
    /// return DIFFERENT values; that is the only contract — the encoding is
    /// NOT a stable ABI and is for tests asserting that a subset barrier left
    /// the other subresource in its prior state (the mip-gen pattern). Returns
    /// the sentinel `~0u` on backends without per-subresource tracking (the
    /// base default) so this is a no-op everywhere but D3D12.
    [[nodiscard]] virtual std::uint32_t
    debug_texture_subresource_state(TextureHandle /*texture*/,
                                    std::uint32_t /*mip*/,
                                    std::uint32_t /*layer*/) const noexcept
    {
        return ~std::uint32_t { 0 };
    }

    /// Phase 134 — copy a contiguous block of shader-group handles
    /// (raygen + miss + hit) out of an RT pipeline into a caller-
    /// provided byte buffer. The buffer is then uploaded into the
    /// SBT region(s) the caller passes to `dispatch_rays`. Returns
    /// kNotImplemented on backends without RT.
    [[nodiscard]] virtual cd::core::Result<void>
    get_rt_shader_group_handles(RtPipelineHandle /*pipeline*/,
                                std::uint32_t /*first_group*/,
                                std::uint32_t /*group_count*/,
                                std::span<std::byte> /*out*/)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "get_rt_shader_group_handles: backend has no RT implementation"));
    }

    // ---- Mesh shader (Phase 765 W2A — F5) ---------------------------------
    //
    // Nanite-class virtual-geometry pipeline. Replaces the input assembler
    // + vertex shader with a task -> mesh -> fragment chain. Backends that
    // do not support mesh shading (no VK_EXT_mesh_shader on Vulkan, no
    // D3D12_MESH_SHADER_TIER_1 on D3D12, no equivalent on Metal/GL) return
    // kNotImplemented from create_mesh_pipeline -- callers gate on
    // `features().mesh_shader` before constructing one.
    //
    // The returned handle reuses the GraphicsPipelineHandle tag because
    // mesh pipelines bind on VK_PIPELINE_BIND_POINT_GRAPHICS (Vulkan) and
    // the D3D12 GRAPHICS root-signature space, exactly like a classic
    // graphics pipeline. The command buffer therefore binds them via
    // `bind_graphics_pipeline(handle)` and dispatches via
    // `draw_mesh_tasks(x, y, z)`.

    [[nodiscard]] virtual cd::core::Result<GraphicsPipelineHandle>
    create_mesh_pipeline(const MeshPipelineDesc& /*desc*/)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "create_mesh_pipeline: backend has no mesh-shader implementation"));
    }

    // ---- Bindless texture array (phase837 — ADR W8-BE) -------------------
    //
    // Runtime-indexed sampler2D array. Required for ray-side texture
    // sampling in the chrome-Sponza reflection path (see ADR W8-BE).
    //
    // Backends that do not implement descriptor_indexing (or the
    // equivalent on D3D12 / Metal) return kNotImplemented. Consumers
    // should gate on `features().bindless_resources` before reaching
    // for these and fall back to a per-prim avg-colour path (W8-BD).
    //
    // Lifecycle:
    //   1. `create_bindless_texture_array(desc)` — allocate the array
    //      with `desc.slot_count` empty slots + the bound sampler.
    //   2. Per slot: `write_bindless_texture_slot(handle, slot, view)`
    //      — populate one slot. Safe to call any time after creation
    //      and before the array is bound to a frame command buffer.
    //      Vulkan PARTIALLY_BOUND + UPDATE_AFTER_BIND let the write
    //      land while other slots remain empty.
    //   3. The handle gets attached to a `DescriptorWrite` with
    //      `type = kBindlessSampledImage` against a layout binding
    //      that declared the same `slot_count`.
    //   4. `destroy_bindless_texture_array(handle)` — release the
    //      array. Per-slot images / views are caller-owned and not
    //      destroyed here.

    [[nodiscard]] virtual cd::core::Result<BindlessTextureArrayHandle>
    create_bindless_texture_array(const BindlessTextureArrayDesc& /*desc*/)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "create_bindless_texture_array: backend has no descriptor_indexing "
            "support (fall back to per-prim avg colour path)"));
    }

    [[nodiscard]] virtual cd::core::Result<void>
    write_bindless_texture_slot(BindlessTextureArrayHandle /*array*/,
                                std::uint32_t              /*slot*/,
                                TextureViewHandle          /*view*/)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kNotImplemented,
            "write_bindless_texture_slot: backend has no bindless support"));
    }

    virtual void destroy_bindless_texture_array(BindlessTextureArrayHandle /*h*/) {}
};

}  // namespace cd::rhi
