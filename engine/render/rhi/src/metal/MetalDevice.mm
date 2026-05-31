// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalDevice.mm
// phase548 — Metal backend Sprint-1 real impl (Objective-C++).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (macOS / iOS host with the
// Metal SDK present). On Win11 / Linux this translation unit is excluded
// from the build by CMake; the cd::rhi_metal target falls back to the
// plain-C++ stub in MetalDevice.cpp which returns kBackendInitFailed on
// non-Apple platforms.
//
// phase548 lifts five of the original 27 kNotImpl call sites to a real
// implementation — enough that hello_metal can issue 1 clear+triangle
// frame end-to-end on macOS:
//
//   1. create_swapchain      — CAMetalLayer attach + drawable acquire/present
//   2. create_command_buffer — wraps id<MTLCommandBuffer> via MetalCommandBufferImpl
//   3. acquire_next_image    — pulls the next CAMetalDrawable
//   4. present               — chained on the command buffer at submit time
//   5. create_graphics_pipeline — inline-MSL triangle pipeline (Sprint-1)
//
// Plus the supporting plumbing:
//   * MTLCommandQueue is created at device construction and shared.
//   * Swapchain image views are reported via a synthetic TextureViewHandle
//     that the command buffer resolves through MetalDeviceCtx.
//   * submit(ICommandBuffer&) commits the underlying MTLCommandBuffer and
//     schedules presentation of the most-recently-acquired drawable.
//
// Remaining ~22 kNotImpl calls (descriptor sets, shader modules, samplers,
// pipeline layouts, fences, semaphores, timeline semaphores, compute,
// readback, full submit) belong to Sprint 2-5.
//
// Win11 build gate: this entire TU is gated on __APPLE__; MetalDevice.cpp
// is the cross-platform stub that compiles everywhere and the CMake build
// only compiles this .mm on Apple hosts.
// =============================================================================
#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>

#include <cd/rhi/metal/MetalDevice.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/NullCommandBuffer.hpp>

#include "MetalInternal.hpp"

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace cd::rhi::metal
{

namespace
{

using detail::MetalCommandBufferImpl;
using detail::MetalDeviceCtx;
using detail::MetalGraphicsPipelineObj;
using detail::MetalSwapchainObj;

// Sentinel 16-bit generation used by swapchain-image-view handles so the
// command-buffer resolver can tell them apart from regular texture views
// (which arrive in Sprint 2). Any pattern works as long as it is unique
// to swapchain-derived views; we pick a memorable bit-pattern that is
// trivially non-zero.
constexpr std::uint16_t kSwapchainViewGen = 0xF00D;

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
// phase548: MTLDevice is acquired via MTLCreateSystemDefaultDevice on
// construction; an MTLCommandQueue is created at the same time so command
// buffers can be allocated cheaply. The device exposes Sprint-1 surface
// (swapchain + cmd-buf + pipeline + acquire/present); the rest still
// returns kNotImplemented via kNotImpl().
// ---------------------------------------------------------------------------
class MetalDevice final : public IDevice, public MetalDeviceCtx
{
public:
    explicit MetalDevice(id<MTLDevice> device, id<MTLCommandQueue> queue,
                         const MetalCreateInfo& info) noexcept
        : mtl_device_(device)
        , mtl_queue_(queue)
        , adapter_name_(std::string { [[device name] UTF8String] })
        , enable_validation_(info.enable_validation)
    {
        // Zero-initialise limits / features so the NullDevice pattern
        // is honoured; concrete values will come in Sprint 2.
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
    // can exercise the factory path without a real GPU allocation. Sprint 2
    // backs these with actual MTLBuffer / MTLTexture objects.
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

    // --------------------------------------------------------------------------
    // create_graphics_pipeline — Sprint-1 inline-MSL triangle pipeline.
    // The descriptor is mostly ignored at this stage; we always build the
    // same hard-coded triangle PSO so the swapchain pipeline can be smoke-
    // tested end-to-end. The colour-attachment format is taken from
    // desc.color_attachment_formats[0] when supplied, else BGRA8 sRGB.
    // Sprint 2 wires in real shader-module compilation + vertex layouts.
    // --------------------------------------------------------------------------
    [[nodiscard]] cd::core::Result<GraphicsPipelineHandle>
    create_graphics_pipeline(const GraphicsPipelineDesc& desc) override
    {
        MTLPixelFormat fmt = MTLPixelFormatBGRA8Unorm_sRGB;
        if (!desc.color_attachment_formats.empty())
        {
            // Re-use the swapchain's format mapping by routing through the
            // same lookup helper would create a circular dependency; for
            // Sprint-1 we only need to honour the most common cases.
            switch (desc.color_attachment_formats[0])
            {
            case Format::kBGRA8Unorm:  fmt = MTLPixelFormatBGRA8Unorm; break;
            case Format::kBGRA8Srgb:   fmt = MTLPixelFormatBGRA8Unorm_sRGB; break;
            case Format::kRGBA8Unorm:  fmt = MTLPixelFormatRGBA8Unorm; break;
            case Format::kRGBA8Srgb:   fmt = MTLPixelFormatRGBA8Unorm_sRGB; break;
            case Format::kRGBA16Float: fmt = MTLPixelFormatRGBA16Float; break;
            default: break;
            }
        }

        std::string err_msg;
        id<MTLRenderPipelineState> pso =
            detail::build_sprint1_triangle_pipeline(mtl_device_, fmt, &err_msg);
        if (pso == nil)
        {
            return std::unexpected(rhi_errors::make_owning(
                rhi_errors::Code::kResourceCreationFailed,
                err_msg.empty() ? "Metal::create_graphics_pipeline: nil PSO"
                                : err_msg));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const GraphicsPipelineHandle h { id, 1u };

        const std::scoped_lock lock { pipelines_mu_ };
        pipelines_.emplace(h.index(), std::make_unique<MetalGraphicsPipelineObj>(pso));
        return h;
    }

    void destroy_graphics_pipeline(GraphicsPipelineHandle h) override
    {
        const std::scoped_lock lock { pipelines_mu_ };
        pipelines_.erase(h.index());
    }

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

    // --------------------------------------------------------------------------
    // acquire_next_image — pull the next CAMetalDrawable from the layer.
    // Sprint-1 ignores `signal` / `fence` / `timeout_ns` because Metal handles
    // drawable synchronization internally (nextDrawable blocks until a slot
    // is available). The returned index is always 0; image_count() reflects
    // the layer's drawable pool size for diagnostics only.
    // --------------------------------------------------------------------------
    [[nodiscard]] cd::core::Result<std::uint32_t>
    acquire_next_image(SwapchainHandle swapchain,
                       SemaphoreHandle /*signal*/,
                       FenceHandle /*fence*/,
                       std::uint64_t /*timeout_ns*/) override
    {
        MetalSwapchainObj* sc = lookup_swapchain(swapchain);
        if (sc == nullptr)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::acquire_next_image: unknown swapchain"));
        }
        id<CAMetalDrawable> drawable = sc->acquire_drawable();
        if (drawable == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kSwapchainOutOfDate,
                "Metal::acquire_next_image: nextDrawable returned nil"));
        }
        return 0u;
    }

    // --------------------------------------------------------------------------
    // present — Metal pattern is to chain [cmd presentDrawable:] on the
    // current frame's command buffer; the device-level present() is therefore
    // a marker. Sprint-1 records the swapchain in pending_present_ so the
    // very next submit() flushes it. Sprint 2 ties present directly to the
    // command-buffer-level submit_internal which already supports it.
    // --------------------------------------------------------------------------
    [[nodiscard]] cd::core::Result<void>
    present(SwapchainHandle swapchain,
            std::uint32_t /*image_index*/,
            std::span<const SemaphoreHandle> /*wait*/) override
    {
        MetalSwapchainObj* sc = lookup_swapchain(swapchain);
        if (sc == nullptr)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::present: unknown swapchain"));
        }
        const std::scoped_lock lock { present_mu_ };
        pending_present_ = sc;
        return {};
    }

    // Swapchain image views are reported as synthetic handles bound 1:1 to
    // the swapchain. The render-pass code-path goes through
    // lookup_swapchain_view_texture below, so the view does not need to
    // back a real MTLTexture until the frame is in flight.
    [[nodiscard]] TextureViewHandle
    swapchain_image_view(SwapchainHandle swapchain,
                         std::uint32_t /*image_index*/) const override
    {
        const std::scoped_lock lock { swapchains_mu_ };
        const auto it = swapchains_.find(swapchain.index());
        if (it == swapchains_.end())
        {
            return TextureViewHandle {};
        }
        // Encode the swapchain index in the view handle so the cmd-buffer
        // resolves back to the right swapchain without an extra lookup.
        // Generation is a 16-bit sentinel used to distinguish swapchain
        // views from regular (sprint-2+) texture views in lookup paths.
        return TextureViewHandle { swapchain.index(), kSwapchainViewGen };
    }

    [[nodiscard]] std::uint32_t
    swapchain_image_count(SwapchainHandle swapchain) const override
    {
        const std::scoped_lock lock { swapchains_mu_ };
        const auto it = swapchains_.find(swapchain.index());
        return (it == swapchains_.end()) ? 0u : it->second->image_count();
    }

    [[nodiscard]] TextureHandle
    swapchain_image(SwapchainHandle /*swapchain*/,
                    std::uint32_t /*image_index*/) const override
    {
        // Sprint-1 does not expose raw swapchain images to user code;
        // the only legitimate consumer is the cmd-buffer render-pass
        // path which goes through lookup_swapchain_view_texture instead.
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

    // --------------------------------------------------------------------------
    // create_swapchain — attach a CAMetalLayer that the caller already
    // bound to its NSView. SwapchainDesc.window_handle is treated as a
    // CAMetalLayer* by the Metal backend. If it is nil we fail rather than
    // silently allocate a layer with no surface to draw on.
    // --------------------------------------------------------------------------
    [[nodiscard]] cd::core::Result<SwapchainHandle>
    create_swapchain(const SwapchainDesc& desc) override
    {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)(desc.window_handle);
        if (layer == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_swapchain: window_handle is nil; "
                "expected a CAMetalLayer* already attached to an NSView/UIView"));
        }

        auto obj = std::make_unique<MetalSwapchainObj>(layer, mtl_device_, desc);

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const SwapchainHandle h { id, 1u };

        const std::scoped_lock lock { swapchains_mu_ };
        swapchains_.emplace(h.index(), std::move(obj));
        return h;
    }

    void destroy_swapchain(SwapchainHandle h) override
    {
        const std::scoped_lock lock { swapchains_mu_ };
        swapchains_.erase(h.index());
    }

    void wait_idle() override
    {
        // Submit an empty command buffer with waitUntilCompleted — this is
        // the SOTA pattern for Metal-side device drains and replaces the
        // Phase531 stub. Cheap (~µs) when the queue is already idle.
        id<MTLCommandBuffer> sentinel = [mtl_queue_ commandBuffer];
        [sentinel commit];
        [sentinel waitUntilCompleted];
    }

    [[nodiscard]] std::unique_ptr<ICommandBuffer>
    create_command_buffer(QueueType /*queue*/ = QueueType::kGraphics) override
    {
        return std::make_unique<MetalCommandBufferImpl>(mtl_queue_, this);
    }

    void submit(ICommandBuffer& cmd) override
    {
        auto* mcb = dynamic_cast<MetalCommandBufferImpl*>(&cmd);
        if (mcb == nullptr)
        {
            // Null / wrong-backend command buffer — Sprint-1 just skips.
            return;
        }

        // Pop the most-recent pending-present swapchain so its drawable
        // gets chained onto this cmd-buf's commit. Multiple consecutive
        // present() calls without an intervening submit are not supported
        // in Sprint-1; this matches the Vulkan single-swapchain pattern.
        MetalSwapchainObj* sc = nullptr;
        {
            const std::scoped_lock lock { present_mu_ };
            sc = pending_present_;
            pending_present_ = nullptr;
        }
        id<CAMetalDrawable> d = (sc != nullptr) ? sc->current_drawable() : nil;
        mcb->submit_internal(d);
    }

    [[nodiscard]] cd::core::Result<void> submit(const SubmitDesc& /*desc*/) override
    {
        return std::unexpected(kNotImpl("Metal::submit(SubmitDesc)"));
    }

    // ---- MetalDeviceCtx interface -------------------------------------------
    [[nodiscard]] id<MTLRenderPipelineState>
    lookup_pipeline(GraphicsPipelineHandle h) const noexcept override
    {
        const std::scoped_lock lock { pipelines_mu_ };
        const auto it = pipelines_.find(h.index());
        return (it == pipelines_.end()) ? nil : it->second->pso();
    }

    [[nodiscard]] id<MTLTexture>
    lookup_swapchain_view_texture(TextureViewHandle h) const noexcept override
    {
        // Sprint-1 view handles encode the swapchain index in the view's
        // own index and a sentinel generation; resolve back to the
        // currently-acquired drawable.
        if (h.generation() != kSwapchainViewGen)
        {
            return nil;
        }
        const std::scoped_lock lock { swapchains_mu_ };
        const auto it = swapchains_.find(h.index());
        if (it == swapchains_.end())
        {
            return nil;
        }
        id<CAMetalDrawable> d = it->second->current_drawable();
        return (d == nil) ? nil : [d texture];
    }

    [[nodiscard]] MetalSwapchainObj*
    lookup_swapchain_for_view(TextureViewHandle h) const noexcept override
    {
        if (h.generation() != kSwapchainViewGen)
        {
            return nullptr;
        }
        const std::scoped_lock lock { swapchains_mu_ };
        const auto it = swapchains_.find(h.index());
        return (it == swapchains_.end()) ? nullptr : it->second.get();
    }

private:
    // Internal helper: not part of MetalDeviceCtx, used by present/acquire
    // which take a SwapchainHandle directly.
    [[nodiscard]] MetalSwapchainObj* lookup_swapchain(SwapchainHandle h) noexcept
    {
        const std::scoped_lock lock { swapchains_mu_ };
        const auto it = swapchains_.find(h.index());
        return (it == swapchains_.end()) ? nullptr : it->second.get();
    }

    id<MTLDevice>        mtl_device_ { nil };
    id<MTLCommandQueue>  mtl_queue_ { nil };
    std::string          adapter_name_;
    bool                 enable_validation_ { false };
    DeviceLimits         limits_ {};
    DeviceFeatures       features_ {};
    std::atomic<std::uint32_t> next_id_ { 1 };

    // Swapchain + pipeline registries. Sprint-1 keeps them small and uses a
    // mutex for thread safety; concrete handle->object mapping replaces the
    // hand-built dummy handles from phase531.
    mutable std::mutex   swapchains_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalSwapchainObj>> swapchains_;

    mutable std::mutex   pipelines_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalGraphicsPipelineObj>> pipelines_;

    // Pending present hand-off — present() records the swapchain whose
    // drawable should ride out on the next submit's command-buffer commit.
    mutable std::mutex   present_mu_;
    MetalSwapchainObj*   pending_present_ { nullptr };
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

    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (queue == nil)
    {
        return std::unexpected(rhi_errors::make(
            rhi_errors::Code::kBackendInitFailed,
            "MTLDevice::newCommandQueue returned nil"));
    }

    std::fprintf(stdout,
                 "[Metal] adapter: %s  validation: %s\n",
                 [[device name] UTF8String],
                 info.enable_validation ? "ON" : "OFF");

    return std::make_unique<MetalDevice>(device, queue, info);
}

}  // namespace cd::rhi::metal

#endif  // __APPLE__
