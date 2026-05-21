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
}  // namespace rhi_errors

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
    [[nodiscard]] virtual cd::core::Result<void>
    upload_buffer(BufferHandle h, std::uint64_t offset, std::span<const std::byte> data) = 0;

    // ---- Swapchain ---------------------------------------------------------

    [[nodiscard]] virtual cd::core::Result<SwapchainHandle> create_swapchain(const SwapchainDesc& desc) = 0;
    virtual void destroy_swapchain(SwapchainHandle h) = 0;

    // ---- Frame -------------------------------------------------------------

    /// Block until the device is idle. Useful before destruction.
    virtual void wait_idle() = 0;

    // ---- Command buffers ---------------------------------------------------

    /// Acquire a recordable command buffer for the given queue. Ownership is
    /// returned to the device when the buffer is destroyed.
    [[nodiscard]] virtual std::unique_ptr<class ICommandBuffer>
    create_command_buffer(QueueType queue = QueueType::kGraphics) = 0;

    /// Submit a recorded command buffer to the device's queue.
    virtual void submit(ICommandBuffer& cmd) = 0;

    /// Full-fidelity submit: command buffers + binary/timeline semaphore
    /// wait/signal hookups + optional CPU completion fence. Modeled directly
    /// on Vulkan's VkSubmitInfo2 so backends translate one-to-one.
    /// Returns kInvalidArgument when any referenced handle is unknown.
    [[nodiscard]] virtual cd::core::Result<void> submit(const SubmitDesc& desc) = 0;
};

}  // namespace cd::rhi
