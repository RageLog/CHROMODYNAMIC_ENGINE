// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalDevice.mm
// phase531 — Metal backend MVP skeleton (Objective-C++).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (macOS / iOS host with the
// Metal SDK present). On Win11 / Linux this translation unit is excluded
// from the build by CMake; the cd::rhi_metal target falls back to the
// plain-C++ stub in MetalDevice.cpp which returns kBackendInitFailed on
// non-Apple platforms.
//
// Phase 9 Sprint 1 intent (deferred):
//   MTLCreateSystemDefaultDevice → MTLDevice
//   MTLDevice newCommandQueue    → MTLCommandQueue
//   MTLDevice newDefaultLibrary  → MTLLibrary (MSL shader archive)
//
// Wave 97 / phase531 ships:
//   - MetalDevice class implementing every IDevice pure virtual.
//   - create_buffer + create_texture return valid-looking handles (index 1).
//   - Every other method returns kNotImplemented via kNotImpl helper.
//   - MetalCommandBuffer, MetalSwapchain, MetalPipeline stubs follow in
//     their own translation units (same compilation unit boundary so each
//     file stays small and reviewable).
// =============================================================================
#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <cd/rhi/metal/MetalDevice.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

namespace cd::rhi::metal
{

namespace
{

// ---------------------------------------------------------------------------
// kNotImpl — convenience wrapper for the kNotImplemented error code.
// Avoids repeating the long namespace path in every method body.
// ---------------------------------------------------------------------------
[[nodiscard]] inline cd::core::ErrorCode kNotImpl(const char* fn) noexcept
{
    return rhi_errors::make(
        rhi_errors::Code::kNotImplemented,
        fn);
}

// ---------------------------------------------------------------------------
// MetalDevice — IDevice backed by an MTLDevice instance.
//
// Phase531: MTLDevice is acquired via MTLCreateSystemDefaultDevice on
// construction; the device object is retained under ARC. Every IDevice
// method that is not yet implemented returns kNotImplemented. The two
// exceptions — create_buffer and create_texture — return dummy handles so
// hello_metal can verify that the factory path is reachable.
// ---------------------------------------------------------------------------
class MetalDevice final : public IDevice
{
public:
    explicit MetalDevice(id<MTLDevice> device, const MetalCreateInfo& info) noexcept
        : mtl_device_(device)
        , adapter_name_(std::string { [[device name] UTF8String] })
        , enable_validation_(info.enable_validation)
    {
        // Zero-initialise limits / features so the NullDevice pattern
        // is honoured; concrete values will come in Phase 9 Sprint 1.
        limits_ = DeviceLimits {};
        features_ = DeviceFeatures {};
    }

    ~MetalDevice() override = default;
    MetalDevice(const MetalDevice&) = delete;
    MetalDevice& operator=(const MetalDevice&) = delete;
    MetalDevice(MetalDevice&&) = delete;
    MetalDevice& operator=(MetalDevice&&) = delete;

    // ---- Introspection -------------------------------------------------------
    [[nodiscard]] Backend backend() const noexcept override { return Backend::kMetal; }

    [[nodiscard]] std::string_view adapter_name() const noexcept override
    {
        return adapter_name_;
    }

    [[nodiscard]] const DeviceLimits& limits() const noexcept override { return limits_; }

    [[nodiscard]] const DeviceFeatures& features() const noexcept override { return features_; }

    // ---- Resource creation ---------------------------------------------------
    // create_buffer and create_texture return valid stub handles so callers
    // can exercise the factory path without a real GPU allocation (Phase531).
    [[nodiscard]] cd::core::Result<BufferHandle>
    create_buffer(const BufferDesc& /*desc*/) override
    {
        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        return BufferHandle { id, 1u };
    }

    void destroy_buffer(BufferHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<TextureHandle>
    create_texture(const TextureDesc& /*desc*/) override
    {
        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        return TextureHandle { id, 1u };
    }

    void destroy_texture(TextureHandle /*h*/) override {}

    // ---- Everything else → kNotImplemented ----------------------------------
    [[nodiscard]] cd::core::Result<TextureViewHandle>
    create_texture_view(const TextureViewDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_texture_view"));
    }

    void destroy_texture_view(TextureViewHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<SamplerHandle>
    create_sampler(const SamplerDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_sampler"));
    }

    void destroy_sampler(SamplerHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<ShaderModuleHandle>
    create_shader_module(const ShaderModuleDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_shader_module"));
    }

    void destroy_shader_module(ShaderModuleHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const DescriptorSetLayoutDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_descriptor_set_layout"));
    }

    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<PipelineLayoutHandle>
    create_pipeline_layout(const PipelineLayoutDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_pipeline_layout"));
    }

    void destroy_pipeline_layout(PipelineLayoutHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<GraphicsPipelineHandle>
    create_graphics_pipeline(const GraphicsPipelineDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_graphics_pipeline"));
    }

    void destroy_graphics_pipeline(GraphicsPipelineHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<ComputePipelineHandle>
    create_compute_pipeline(const ComputePipelineDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_compute_pipeline"));
    }

    void destroy_compute_pipeline(ComputePipelineHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<DescriptorSetHandle>
    allocate_descriptor_set(DescriptorSetLayoutHandle /*layout*/) override
    {
        return std::unexpected(kNotImpl("Metal::allocate_descriptor_set"));
    }

    void destroy_descriptor_set(DescriptorSetHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(DescriptorSetHandle /*set*/,
                          std::span<const DescriptorWrite> /*writes*/) override
    {
        return std::unexpected(kNotImpl("Metal::update_descriptor_set"));
    }

    [[nodiscard]] cd::core::Result<SemaphoreHandle> create_semaphore() override
    {
        return std::unexpected(kNotImpl("Metal::create_semaphore"));
    }

    void destroy_semaphore(SemaphoreHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<FenceHandle> create_fence(bool /*signaled*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_fence"));
    }

    void destroy_fence(FenceHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<void>
    wait_for_fence(FenceHandle /*fence*/, std::uint64_t /*timeout_ns*/) override
    {
        return std::unexpected(kNotImpl("Metal::wait_for_fence"));
    }

    void reset_fence(FenceHandle /*fence*/) override {}

    [[nodiscard]] bool is_fence_signaled(FenceHandle /*fence*/) override { return false; }

    [[nodiscard]] cd::core::Result<TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t /*initial_value*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_timeline_semaphore"));
    }

    void destroy_timeline_semaphore(TimelineSemaphoreHandle /*h*/) override {}

    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(TimelineSemaphoreHandle /*h*/,
                            std::uint64_t /*value*/,
                            std::uint64_t /*timeout_ns*/) override
    {
        return std::unexpected(kNotImpl("Metal::wait_timeline_semaphore"));
    }

    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(TimelineSemaphoreHandle /*h*/,
                              std::uint64_t /*value*/) override
    {
        return std::unexpected(kNotImpl("Metal::signal_timeline_semaphore"));
    }

    [[nodiscard]] std::uint64_t
    timeline_semaphore_value(TimelineSemaphoreHandle /*h*/) const override { return 0; }

    [[nodiscard]] cd::core::Result<std::uint32_t>
    acquire_next_image(SwapchainHandle /*swapchain*/,
                       SemaphoreHandle /*signal*/,
                       FenceHandle /*fence*/,
                       std::uint64_t /*timeout_ns*/) override
    {
        return std::unexpected(kNotImpl("Metal::acquire_next_image"));
    }

    [[nodiscard]] cd::core::Result<void>
    present(SwapchainHandle /*swapchain*/,
            std::uint32_t /*image_index*/,
            std::span<const SemaphoreHandle> /*wait*/) override
    {
        return std::unexpected(kNotImpl("Metal::present"));
    }

    [[nodiscard]] TextureViewHandle
    swapchain_image_view(SwapchainHandle /*swapchain*/,
                         std::uint32_t /*image_index*/) const override
    {
        return TextureViewHandle {};
    }

    [[nodiscard]] std::uint32_t
    swapchain_image_count(SwapchainHandle /*swapchain*/) const override { return 0; }

    [[nodiscard]] TextureHandle
    swapchain_image(SwapchainHandle /*swapchain*/,
                    std::uint32_t /*image_index*/) const override
    {
        return TextureHandle {};
    }

    [[nodiscard]] cd::core::Result<void>
    upload_buffer(BufferHandle /*h*/,
                  std::uint64_t /*offset*/,
                  std::span<const std::byte> /*data*/) override
    {
        return std::unexpected(kNotImpl("Metal::upload_buffer"));
    }

    [[nodiscard]] cd::core::Result<void>
    download_buffer(BufferHandle /*h*/,
                    std::uint64_t /*offset*/,
                    std::span<std::byte> /*dst*/) override
    {
        return std::unexpected(kNotImpl("Metal::download_buffer"));
    }

    [[nodiscard]] cd::core::Result<SwapchainHandle>
    create_swapchain(const SwapchainDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::create_swapchain"));
    }

    void destroy_swapchain(SwapchainHandle /*h*/) override {}

    void wait_idle() override
    {
        // Phase531 stub: no queue to drain yet.
    }

    [[nodiscard]] std::unique_ptr<ICommandBuffer>
    create_command_buffer(QueueType /*queue*/ = QueueType::kGraphics) override
    {
        // Return a NullCommandBuffer so the sample can call begin/end without
        // crashing. Phase 9 Sprint 1 wires a real MTLCommandBuffer.
        return std::make_unique<NullCommandBuffer>();
    }

    void submit(ICommandBuffer& /*cmd*/) override {}

    [[nodiscard]] cd::core::Result<void> submit(const SubmitDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::submit(SubmitDesc)"));
    }

private:
    id<MTLDevice>  mtl_device_ { nil };
    std::string    adapter_name_;
    bool           enable_validation_ { false };
    DeviceLimits   limits_ {};
    DeviceFeatures features_ {};
    std::atomic<std::uint32_t> next_id_ { 1 };
};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// create_metal_device — public factory (Apple path).
// ---------------------------------------------------------------------------
cd::core::Result<std::unique_ptr<cd::rhi::IDevice>>
create_metal_device(MetalCreateInfo info)
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kBackendInitFailed,
            "MTLCreateSystemDefaultDevice returned nil — no Metal-capable GPU"));
    }

    std::fprintf(stdout,
                 "[Metal] adapter: %s  validation: %s\n",
                 [[device name] UTF8String],
                 info.enable_validation ? "ON" : "OFF");

    return std::make_unique<MetalDevice>(device, info);
}

}  // namespace cd::rhi::metal

#endif  // __APPLE__
