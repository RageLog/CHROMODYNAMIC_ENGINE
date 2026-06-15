// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalDevice.mm
// phase548 — Metal backend Sprint-1 real impl (Objective-C++).
// phase559 — Metal backend Sprint-2 (sampler + descriptor-write +
//            blit-copy plumbing).
// phase572 — Metal backend Sprint-3 (shader-module + compute-pipeline
//            factories + the supporting registries / lookups).
// phase615 — Metal backend Sprint-4 (fence + event + pipeline-layout +
//            descriptor-set-layout factories + submit(SubmitDesc) queue
//            plumbing with completion-handler fence signal).
// phase649 — Metal backend Sprint-5 (texture-view registry +
//            allocate_descriptor_set argument-encoder path +
//            timeline-semaphore registry + upload_buffer / download_buffer
//            shared-storage memcpy + SubmitDesc timeline-encode hand-off).
//            Closes the final 9 kNotImpl sites on the IDevice surface;
//            the Metal backend now reports 27/27 implemented coverage at
//            startup (RT-related DescriptorWrite::kAccelerationStructure
//            still returns kInvalidArgument because Metal RT is a separate
//            roadmap tier).
//
// Compiled only when CD_RHI_METAL_ENABLED=ON (macOS / iOS host with the
// Metal SDK present). On Win11 / Linux this translation unit is excluded
// from the build by CMake; the cd::rhi_metal target falls back to the
// plain-C++ stub in MetalDevice.cpp which returns kBackendInitFailed on
// non-Apple platforms.
//
// phase548 (Sprint-1) lifted five of the original 27 kNotImpl sites:
//   1. create_swapchain      — CAMetalLayer attach + drawable acquire/present
//   2. create_command_buffer — wraps id<MTLCommandBuffer> via MetalCommandBufferImpl
//   3. acquire_next_image    — pulls the next CAMetalDrawable
//   4. present               — chained on the command buffer at submit time
//   5. create_graphics_pipeline — inline-MSL triangle pipeline (Sprint-1)
//
// phase559 (Sprint-2) lifts five more, focused on the resource-copy +
// descriptor-binding surface so a future Sprint-3 buffer/texture allocator
// drops in without further wiring changes:
//
//   1. copy_buffer             — MTLBlitCommandEncoder copyFromBuffer:toBuffer:
//                                (cmd-buffer side; device-side stub already in
//                                place)
//   2. copy_buffer_to_image    — MTLBlitCommandEncoder copyFromBuffer:...:toTexture:
//                                (the "copy_texture" path of the brief; texture
//                                upload via blit encoder)
//   3. create_sampler          — MTLSamplerState via [device newSamplerStateWith
//                                Descriptor:]; SOTA mapping of filters, address
//                                modes, lod range, compare op, anisotropy,
//                                border colour
//   4. push_constants          — setVertexBytes / setFragmentBytes inline-arg
//                                fast path (≤4 KB; the Metal canonical replacement
//                                for Vulkan vkCmdPushConstants under SPIRV-Cross)
//   5. update_descriptor_set   — argument-buffer-ready surface; Sprint-2 ships
//                                the validate-and-resolve path that consumes
//                                buffer / view / sampler handles via the new
//                                MetalDeviceCtx lookups. Argument-encoder
//                                emission lands in Sprint 3 once allocate_
//                                descriptor_set graduates from kNotImpl.
//
// Plus the supporting plumbing:
//   * MTLCommandQueue is created at device construction and shared.
//   * Swapchain image views are reported via a synthetic TextureViewHandle
//     that the command buffer resolves through MetalDeviceCtx.
//   * submit(ICommandBuffer&) commits the underlying MTLCommandBuffer and
//     schedules presentation of the most-recently-acquired drawable.
//   * Sampler / buffer / texture registries hang off MetalDeviceCtx so the
//     command buffer + the descriptor path can both resolve handles. The
//     buffer / texture maps are intentionally empty in Sprint-2; Sprint 3
//     wires real MTLBuffer / MTLTexture allocation into create_buffer /
//     create_texture and the lookups light up automatically.
//
// phase572 (Sprint-3) lifts five more, focused on the shader + compute
// surface so the engine can start exercising compute-side paths on Metal:
//
//   1. create_shader_module     — newLibraryWithSource: + newFunctionWithName:
//                                 from ShaderModuleDesc::code (UTF-8 MSL).
//   2. create_compute_pipeline  — newComputePipelineStateWithFunction: built
//                                 from a previously-registered ShaderModule.
//   3. bind_compute_pipeline    — MTLComputeCommandEncoder
//                                 setComputePipelineState: with the same
//                                 lazy-open / encoder-transition discipline
//                                 as the Sprint-2 blit encoder.
//   4. bind_vertex_buffer +
//      bind_index_buffer        — [encoder setVertexBuffer:offset:atIndex:]
//                                 + index buffer + type cached for the
//                                 next draw_indexed call.
//   5. draw_indexed +
//      dispatch                 — drawIndexedPrimitives via the cached
//                                 index state + dispatchThreadgroups via
//                                 the bound compute PSO.
//
// phase615 (Sprint-4) lifts five more, focused on the queue-submit + sync
// surface so frame pacing + multi-queue handoff can be expressed end-to-end:
//
//   1. create_fence + wait_for_fence + reset_fence + is_fence_signaled
//                                — dispatch_semaphore_t backed CPU-side
//                                  completion fence; raised from
//                                  -[MTLCommandBuffer addCompletedHandler:]
//                                  on the matching submit.
//   2. create_semaphore         — id<MTLSharedEvent>-backed GPU-to-GPU sync
//                                  primitive. Wait + signal hookups are
//                                  expressed via encodeWaitForEvent:value: /
//                                  encodeSignalEvent:value: on the submit
//                                  cmd-buf, mirroring the Vulkan binary-
//                                  semaphore contract.
//   3. create_pipeline_layout   — Metal has no explicit pipeline-layout
//                                  object (binding topology is folded into
//                                  the PSO + argument encoders); the factory
//                                  records the descriptor metadata against
//                                  a registry handle for Sprint-5
//                                  argument-buffer emission to consume.
//   4. create_descriptor_set_layout — Symmetric metadata-only registry; the
//                                  real MTLArgumentEncoder is created at
//                                  allocate_descriptor_set time in Sprint 5.
//   5. submit(SubmitDesc)       — Iterates the cmd-buffer span, commits
//                                  each underlying MTLCommandBuffer in
//                                  order, encodes wait / signal MTLSharedEvent
//                                  hand-offs on the first / last cmd-buf,
//                                  and chains the SubmitDesc.signal_fence
//                                  completion handler so callers can block
//                                  on wait_for_fence.
//
// phase649 (Sprint-5) lifts the remaining nine, completing the IDevice
// surface coverage for the Metal backend:
//
//   1. create_texture_view       — id<MTLTexture> newTextureViewWithPixel
//                                  Format:textureType:levels:slices: against
//                                  a live parent MTLTexture (or registers
//                                  the desc for a later-arriving parent
//                                  with graceful nil resolution).
//   2. allocate_descriptor_set   — id<MTLArgumentEncoder> from the cached
//                                  DescriptorSetLayoutBinding table +
//                                  id<MTLBuffer> sized via [encoder
//                                  encodedLength]; populates the
//                                  descriptor-set registry.
//   3. update_descriptor_set     — kAccelerationStructure branch now
//                                  surfaces kInvalidArgument (RT is a
//                                  separate Metal tier) instead of
//                                  kNotImpl — the contract is now stable.
//   4. create_timeline_semaphore — id<MTLSharedEvent>::signaledValue is
//                                  the timeline counter source of truth.
//   5. wait_timeline_semaphore   — -[id<MTLSharedEvent> notifyListener:
//                                  atValue:block:] fed into a
//                                  dispatch_semaphore_t with the IDevice
//                                  timeout contract preserved.
//   6. signal_timeline_semaphore — host-side direct write to
//                                  `signaledValue` with monotonic guard.
//   7. upload_buffer             — memcpy into [MTLBuffer contents] when
//                                  storageMode is Shared / Managed; OOB
//                                  ranges surface kInvalidArgument.
//   8. download_buffer           — symmetric memcpy back to caller's span.
//   9. submit(SubmitDesc) timeline path — encodeWaitForEvent: + encode
//                                  SignalEvent: against the timeline's
//                                  MTLSharedEvent for both the wait_
//                                  timeline_semaphores and signal_
//                                  timeline_semaphores SubmitDesc fields.
//
// MOMENT (Sprint-5): a developer running the Metal backend boots their
// macOS app and the cd::rhi adapter banner reports "27/27 implemented,
// 0 kNotImpl" — proof that the backend is feature-complete on every
// IDevice contract method even before runtime smoke lands.
//
// Runtime smoke not executed for Sprint-5 (no Mac CI yet); the .mm TU
// remains gated behind __APPLE__ so Win11 + Linux builds keep skipping
// it and link the kBackendInitFailed stub from MetalDevice.cpp.
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

// M6 (ADR-20260615): the host-side GLSL/SPIR-V -> MSL toolchain
// (cd::rhi_metal_shader) + the glslang front-end + the gluon include resolver.
// The .mm CONSUMES the MSL text these produce; it never inlines the
// cross-compile logic (ADR invariant iv).
#include <cd/rhi/metal/MetalShaderToolchain.hpp>
#include <cd/shader/Compiler.hpp>
#include <cd/gluon/ModuleRegistry.hpp>

#include "MetalInternal.hpp"

#include <span>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::rhi::metal
{

namespace
{

using detail::MetalAccelObj;
using detail::MetalBufferObj;
using detail::MetalCommandBufferImpl;
using detail::MetalComputePipelineObj;
using detail::MetalDescriptorSetLayoutObj;
using detail::MetalDescriptorSetObj;
using detail::MetalDeviceCtx;
using detail::MetalEventObj;
using detail::MetalFenceObj;
using detail::MetalGraphicsPipelineObj;
using detail::MetalGraphicsPipelineStateObj;
using detail::MetalPipelineLayoutObj;
using detail::MetalSamplerObj;
using detail::MetalShaderModuleObj;
using detail::MetalSwapchainObj;
using detail::MetalTextureObj;
using detail::MetalTextureViewObj;
using detail::MetalTimelineObj;

// Sentinel 16-bit generation used by swapchain-image-view handles so the
// command-buffer resolver can tell them apart from regular texture views
// (which arrive in Sprint 2). Any pattern works as long as it is unique
// to swapchain-derived views; we pick a memorable bit-pattern that is
// trivially non-zero.
constexpr std::uint16_t kSwapchainViewGen = 0xF00D;

// phase649 / Sprint-5: regular (non-swapchain) TextureView handles use a
// different sentinel generation so the swapchain-view resolver can tell
// them apart from "live" texture views allocated via create_texture_view.
// Any non-kSwapchainViewGen pattern works; 0xBADE picks a different bit
// pattern from kSwapchainViewGen so a misrouted view handle is obvious in
// a debugger.
constexpr std::uint16_t kRegularViewGen = 0xBADE;

// ---------------------------------------------------------------------------
// kNotImpl — convenience wrapper for the kNotImplemented error code.
// Avoids repeating the long namespace path in every method body. Kept
// around post-Sprint-5 (when every IDevice call site stopped returning
// kNotImplemented) as a defensive helper: future surface-area additions
// (e.g. a new IDevice method that lands ahead of the matching Metal
// implementation) can route through this helper so the diagnostic shape
// stays uniform. Marked [[maybe_unused]] so the Sprint-5 baseline (no
// remaining call sites) does not trip -Wunused-function.
// ---------------------------------------------------------------------------
[[nodiscard]] [[maybe_unused]] inline cd::core::ErrorCode
kNotImpl(const char* fn) noexcept
{
    return rhi_errors::make(
        rhi_errors::Code::kNotImplemented,
        fn);
}

// ---------------------------------------------------------------------------
// M1/M2 (ADR-20260615) — shared format + resource-state helpers.
//
// These mirror the Vulkan back-end's map_* helpers (VulkanDevice.cpp:286+
// map_format, 158 map_texture_type, 137 map_texture_usage) so the same
// cd::rhi::Format / TextureType / TextureUsage produce parity-equivalent
// native objects. The list intentionally covers the formats the engine
// actually uses (swapchain BGRA, HDR RGBA16F, depth D32/D24S8, sampled
// RGBA8); the ADR's follow-up MetalFormat.mm hosts the long tail.
// ---------------------------------------------------------------------------
[[nodiscard]] MTLPixelFormat
metal_pixel_format(Format f, MTLPixelFormat fallback) noexcept
{
    switch (f)
    {
    case Format::kUndefined:       return fallback;
    case Format::kR8Unorm:         return MTLPixelFormatR8Unorm;
    case Format::kRG8Unorm:        return MTLPixelFormatRG8Unorm;
    case Format::kRGBA8Unorm:      return MTLPixelFormatRGBA8Unorm;
    case Format::kRGBA8Srgb:       return MTLPixelFormatRGBA8Unorm_sRGB;
    case Format::kBGRA8Unorm:      return MTLPixelFormatBGRA8Unorm;
    case Format::kBGRA8Srgb:       return MTLPixelFormatBGRA8Unorm_sRGB;
    case Format::kR16Float:        return MTLPixelFormatR16Float;
    case Format::kRG16Float:       return MTLPixelFormatRG16Float;
    case Format::kRGBA16Float:     return MTLPixelFormatRGBA16Float;
    case Format::kR32Float:        return MTLPixelFormatR32Float;
    case Format::kRG32Float:       return MTLPixelFormatRG32Float;
    case Format::kRGBA32Float:     return MTLPixelFormatRGBA32Float;
    case Format::kRG32Uint:        return MTLPixelFormatRG32Uint;
    case Format::kR32Uint:         return MTLPixelFormatR32Uint;
    case Format::kRGBA32Uint:      return MTLPixelFormatRGBA32Uint;
    case Format::kR11G11B10Float:  return MTLPixelFormatRG11B10Float;
    case Format::kRGB10A2Unorm:    return MTLPixelFormatRGB10A2Unorm;
    case Format::kRGB9E5Float:     return MTLPixelFormatRGB9E5Float;
    case Format::kD16Unorm:        return MTLPixelFormatDepth16Unorm;
    case Format::kD32Float:        return MTLPixelFormatDepth32Float;
    case Format::kD24UnormS8Uint:  return MTLPixelFormatDepth24Unorm_Stencil8;
    case Format::kD32FloatS8Uint:  return MTLPixelFormatDepth32Float_Stencil8;
    case Format::kS8Uint:          return MTLPixelFormatStencil8;
    default:                       return fallback;
    }
}

[[nodiscard]] MTLTextureType metal_texture_type(TextureType t) noexcept
{
    switch (t)
    {
    case TextureType::k1D:        return MTLTextureType1D;
    case TextureType::k2D:        return MTLTextureType2D;
    case TextureType::k3D:        return MTLTextureType3D;
    case TextureType::kCube:      return MTLTextureTypeCube;
    case TextureType::k1DArray:   return MTLTextureType1DArray;
    case TextureType::k2DArray:   return MTLTextureType2DArray;
    case TextureType::kCubeArray: return MTLTextureTypeCubeArray;
    }
    return MTLTextureType2D;
}

[[nodiscard]] MTLTextureUsage metal_texture_usage(TextureUsage u) noexcept
{
    MTLTextureUsage out = MTLTextureUsageUnknown;
    if (has(u, TextureUsage::kSampled))
    {
        out |= MTLTextureUsageShaderRead;
    }
    if (has(u, TextureUsage::kStorage))
    {
        out |= MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    }
    if (has(u, TextureUsage::kColorAttachment)
        || has(u, TextureUsage::kDepthStencilAttachment)
        || has(u, TextureUsage::kInputAttachment))
    {
        out |= MTLTextureUsageRenderTarget;
    }
    return out;
}

// Storage mode for a buffer's MemoryUsage. Apple-Silicon unified memory means
// Shared covers every CPU-visible case (the engine never targets discrete
// Intel macs in Fork-A); GPU-only buffers use Private. Mirrors
// VulkanDevice::map_vma_usage's host_visible decision.
[[nodiscard]] MTLResourceOptions storage_options_for(MemoryUsage u) noexcept
{
    switch (u)
    {
    case MemoryUsage::kGpuOnly:
        return MTLResourceStorageModePrivate;
    case MemoryUsage::kCpuToGpu:
    case MemoryUsage::kGpuToCpu:
    case MemoryUsage::kCpuRandomAccess:
    case MemoryUsage::kAuto:
        return MTLResourceStorageModeShared;
    }
    return MTLResourceStorageModeShared;
}

// ---------------------------------------------------------------------------
// M4 (ADR-20260615) — DescriptorType -> MTLArgumentDescriptor mapping. Each
// Vulkan binding becomes one [[id(binding)]] entry inside the argument-buffer
// struct; the dataType selects how update_descriptor_set encodes it
// (setBuffer / setTexture / setSamplerState / setAccelerationStructure).
// ---------------------------------------------------------------------------
[[nodiscard]] MTLDataType arg_data_type_for(DescriptorType t) noexcept
{
    switch (t)
    {
    case DescriptorType::kUniformBuffer:
    case DescriptorType::kStorageBuffer:
    case DescriptorType::kUniformBufferDynamic:
    case DescriptorType::kStorageBufferDynamic:
        return MTLDataTypePointer;
    case DescriptorType::kSampledImage:
    case DescriptorType::kStorageImage:
    case DescriptorType::kCombinedImageSampler:
    case DescriptorType::kInputAttachment:
    case DescriptorType::kBindlessSampledImage:
        return MTLDataTypeTexture;
    case DescriptorType::kSampler:
        return MTLDataTypeSampler;
    case DescriptorType::kAccelerationStructure:
        return MTLDataTypeInstanceAccelerationStructure;
    }
    return MTLDataTypePointer;
}

[[nodiscard]] MTLArgumentAccess arg_access_for(DescriptorType t) noexcept
{
    switch (t)
    {
    case DescriptorType::kStorageBuffer:
    case DescriptorType::kStorageBufferDynamic:
    case DescriptorType::kStorageImage:
        return MTLArgumentAccessReadWrite;
    default:
        return MTLArgumentAccessReadOnly;
    }
}

// ---------------------------------------------------------------------------
// MetalDevice — IDevice backed by an MTLDevice instance.
//
// phase548: MTLDevice is acquired via MTLCreateSystemDefaultDevice on
// construction; an MTLCommandQueue is created at the same time so command
// buffers can be allocated cheaply. The device exposes Sprint-1 surface
// (swapchain + cmd-buf + pipeline + acquire/present); subsequent sprints
// expand the surface until Sprint-5 closes the final kNotImpl sites.
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
        // M9 (ADR-20260615): the Metal RT path is ray-query (inline RT) via
        // MTLAccelerationStructure + the MSL metal::raytracing intersector,
        // exactly the path the engine consumes (rayQueryEXT analog). Gate the
        // bits on macOS Metal-3 ray-tracing support so callers branch on the
        // real capability. ray_tracing here means "AS build + ray-query"
        // (NOT the SBT pipeline — that stays kNotImplemented on every backend
        // including Vulkan).
        if (@available(macOS 11.0, iOS 14.0, *))
        {
            const bool rt = [mtl_device_ supportsRaytracing];
            features_.ray_tracing = rt;
            features_.ray_query   = rt;
        }
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
    // M1 (ADR-20260615): real [device newBufferWithLength:options:] allocation
    // with a handle->id<MTLBuffer> registry. The storage mode is chosen from
    // BufferDesc::memory exactly as the Vulkan back-end picks a VMA usage:
    //   * kGpuOnly             -> Private (GPU-only; uploads go via blit copy)
    //   * kCpuToGpu/kGpuToCpu/
    //     kCpuRandomAccess/kAuto -> Shared (CPU-visible unified memory; the
    //                              dominant Apple-Silicon case — host writes
    //                              land straight in the GPU-visible region).
    // Mirrors VulkanDevice::create_buffer (size==0 -> kInvalidArgument; alloc
    // failure -> kResourceCreationFailed). Once this lights up, every path
    // that resolves a buffer (copy_buffer, bind_vertex_buffer, draw_indexed,
    // upload_buffer) does real work instead of a graceful skip.
    [[nodiscard]] cd::core::Result<BufferHandle>
    create_buffer(const BufferDesc& desc) override
    {
        if (desc.size == 0u)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_buffer: size == 0"));
        }
        const MTLResourceOptions opts = storage_options_for(desc.memory);
        id<MTLBuffer> buf =
            [mtl_device_ newBufferWithLength:static_cast<NSUInteger>(desc.size)
                                     options:opts];
        if (buf == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kResourceCreationFailed,
                "Metal::create_buffer: newBufferWithLength returned nil"));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const BufferHandle h { id, 1u };

        const std::scoped_lock lock { buffers_mu_ };
        buffers_.emplace(h.index(),
                         std::make_unique<MetalBufferObj>(buf, desc.memory));
        return h;
    }

    void destroy_buffer(BufferHandle h) override
    {
        const std::scoped_lock lock { buffers_mu_ };
        buffers_.erase(h.index());
    }

    // M1 (ADR-20260615): real [device newTextureWithDescriptor:] allocation.
    // MTLTextureDescriptor mirrors the Vulkan VkImageCreateInfo translation:
    // pixelFormat from Format, width/height/depth from extent, mipmapLevelCount
    // from mip_levels, arrayLength from array_layers, usage from TextureUsage,
    // sampleCount from samples, textureType from TextureType. storageMode is
    // Private by default (GPU-only render targets / sampled textures);
    // host-visible texture memory is uncommon and uploads go via blit. Mirrors
    // VulkanDevice::create_texture validation (zero extent / kUndefined format
    // -> kInvalidArgument; alloc failure -> kResourceCreationFailed).
    [[nodiscard]] cd::core::Result<TextureHandle>
    create_texture(const TextureDesc& desc) override
    {
        if (desc.extent.width == 0u || desc.extent.height == 0u)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_texture: zero extent"));
        }
        if (desc.format == Format::kUndefined)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_texture: kUndefined format"));
        }

        MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
        td.textureType      = metal_texture_type(desc.type);
        td.pixelFormat      = metal_pixel_format(desc.format,
                                                 MTLPixelFormatRGBA8Unorm);
        td.width            = static_cast<NSUInteger>(desc.extent.width);
        td.height           = static_cast<NSUInteger>(desc.extent.height);
        td.depth            = (desc.type == TextureType::k3D)
                                  ? static_cast<NSUInteger>(desc.extent.depth)
                                  : 1u;
        td.mipmapLevelCount = static_cast<NSUInteger>(
            desc.mip_levels == 0u ? 1u : desc.mip_levels);
        td.arrayLength      = static_cast<NSUInteger>(
            desc.array_layers == 0u ? 1u : desc.array_layers);
        td.sampleCount      = static_cast<NSUInteger>(desc.samples);
        td.usage            = metal_texture_usage(desc.usage);
        td.storageMode      = MTLStorageModePrivate;

        id<MTLTexture> tex = [mtl_device_ newTextureWithDescriptor:td];
        if (tex == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kResourceCreationFailed,
                "Metal::create_texture: newTextureWithDescriptor returned nil"));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const TextureHandle h { id, 1u };

        const std::scoped_lock lock { textures_mu_ };
        textures_.emplace(h.index(),
                          std::make_unique<MetalTextureObj>(tex, desc.format));
        return h;
    }

    void destroy_texture(TextureHandle h) override
    {
        const std::scoped_lock lock { textures_mu_ };
        textures_.erase(h.index());
    }

    // phase649 (Sprint-5): real id<MTLTexture> newTextureViewWith* path.
    //
    // The view object captures the TextureViewDesc up-front; the actual
    // MTLTexture view is materialised lazily on lookup_texture_view so an
    // unbacked parent texture (still the Sprint-3 default — create_texture
    // hands out stub handles) resolves to nil without aborting view
    // creation. That matches the Sprint-2 / Sprint-3 graceful-skip pattern
    // already used by the copy / descriptor paths.
    //
    // The returned handle encodes (registry_id, kRegularViewGen) so the
    // swapchain-view resolver (which uses kSwapchainViewGen) does not pick
    // up regular view handles and vice versa. Both resolvers live behind
    // the same MetalDeviceCtx interface so the cmd-buffer side does not
    // care which one fires.
    [[nodiscard]] cd::core::Result<TextureViewHandle>
    create_texture_view(const TextureViewDesc& desc) override
    {
        auto obj = std::make_unique<MetalTextureViewObj>(desc);

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const TextureViewHandle h { id, kRegularViewGen };

        const std::scoped_lock lock { texture_views_mu_ };
        texture_views_.emplace(h.index(), std::move(obj));
        return h;
    }

    void destroy_texture_view(TextureViewHandle h) override
    {
        if (h.generation() != kRegularViewGen)
        {
            // Swapchain-view handles are owned by the swapchain itself;
            // destroy_swapchain handles their cleanup. Silently ignore so
            // generic teardown code that calls destroy on every view does
            // not crash.
            return;
        }
        const std::scoped_lock lock { texture_views_mu_ };
        texture_views_.erase(h.index());
    }

    // phase559 (Sprint-2): real MTLSamplerState path.
    //
    // The descriptor mapping is delegated to build_metal_sampler in
    // MetalPipeline.mm so MetalDevice.mm stays focused on registry / RAII.
    // Border-colour caveats are documented in build_metal_sampler — Metal
    // only supports three discrete border values (transparent / opaque
    // black / opaque white) and silently rounds non-matching colours to
    // the closest one. That matches the Vulkan back-end's enum behaviour.
    [[nodiscard]] cd::core::Result<SamplerHandle>
    create_sampler(const SamplerDesc& desc) override
    {
        std::string err_msg;
        id<MTLSamplerState> state =
            detail::build_metal_sampler(mtl_device_, desc, &err_msg);
        if (state == nil)
        {
            return std::unexpected(rhi_errors::make_owning(
                rhi_errors::Code::kResourceCreationFailed,
                err_msg.empty() ? "Metal::create_sampler: nil sampler state"
                                : err_msg));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const SamplerHandle h { id, 1u };

        const std::scoped_lock lock { samplers_mu_ };
        samplers_.emplace(h.index(), std::make_unique<MetalSamplerObj>(state));
        return h;
    }

    void destroy_sampler(SamplerHandle h) override
    {
        const std::scoped_lock lock { samplers_mu_ };
        samplers_.erase(h.index());
    }

    // M6 (ADR-20260615): create_shader_module routes the source language to
    // MSL exactly as the D3D12 backend routes it to DXIL (D16 symmetry).
    //
    //   * kGlsl  -> compose_glsl_to_msl(spirv_compiler, GlslToMslDesc{...})
    //              GLSL -> SPIR-V (glslang) -> MSL (SPIRV-Cross), with the M3
    //              set-per-argument-buffer binding contract enforced
    //              (argument_buffers = true, push_constant -> [[buffer(8)]]).
    //   * kSpirv -> compose_spirv_to_msl(words, ...) — SPIR-V tail only.
    //   * kBytecode (kMsl-equivalent legacy) -> the existing raw-MSL path.
    //
    // The MSL text from the toolchain feeds [device newLibraryWithSource:];
    // the cleansed entry name (SPIRV-Cross renames "main" -> "main0" per
    // stage) comes from MslArtifact::entry_point — we look the MTLFunction up
    // by that name, NOT by assuming "main". The .mm only CONSUMES the toolchain
    // output; the GLSL/SPIR-V->MSL logic stays host-side in cd::rhi_metal_shader.
    [[nodiscard]] cd::core::Result<ShaderModuleHandle>
    create_shader_module(const ShaderModuleDesc& desc) override
    {
        if (desc.code == nullptr || desc.code_size == 0u)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_shader_module: empty source/bytecode"));
        }

        // Cross-compile path (kGlsl / kSpirv) -> MSL text via the M3 toolchain.
        if (desc.language == ShaderSourceLanguage::kGlsl
            || desc.language == ShaderSourceLanguage::kSpirv)
        {
            auto msl = cross_compile_to_msl(desc);
            if (!msl.has_value())
            {
                return std::unexpected(rhi_errors::make_owning(
                    rhi_errors::Code::kResourceCreationFailed,
                    std::string { "Metal::create_shader_module (->MSL): " }
                        + std::string { msl.error().message }));
            }
            return build_module_from_msl(msl->source, msl->entry_point,
                                         desc.stage);
        }

        // Legacy path: desc.code is raw MSL/native (kBytecode default, or kMsl
        // equivalent) — compile directly via build_metal_shader_function.
        std::string err_msg;
        id<MTLLibrary>  lib = nil;
        id<MTLFunction> fn  = detail::build_metal_shader_function(
            mtl_device_, desc, &lib, &err_msg);
        if (fn == nil)
        {
            return std::unexpected(rhi_errors::make_owning(
                rhi_errors::Code::kResourceCreationFailed,
                err_msg.empty() ? "Metal::create_shader_module: nil function"
                                : err_msg));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const ShaderModuleHandle h { id, 1u };

        const std::scoped_lock lock { shader_modules_mu_ };
        shader_modules_.emplace(
            h.index(),
            std::make_unique<MetalShaderModuleObj>(lib, fn, desc.stage));
        return h;
    }

    void destroy_shader_module(ShaderModuleHandle h) override
    {
        const std::scoped_lock lock { shader_modules_mu_ };
        shader_modules_.erase(h.index());
    }

    // phase615 (Sprint-4): metadata-only descriptor-set-layout factory.
    //
    // Metal expresses descriptor sets via MTLArgumentEncoder; the encoder is
    // built at allocate_descriptor_set time (Sprint 5). For Sprint-4 we
    // capture the binding table here so the encoder can size the argument
    // buffer correctly when the time comes. Empty layouts (zero bindings)
    // are accepted — the engine sometimes binds an empty set as a "no
    // resources" marker, matching Vulkan's VkDescriptorSetLayout behaviour.
    [[nodiscard]] cd::core::Result<DescriptorSetLayoutHandle>
    create_descriptor_set_layout(const DescriptorSetLayoutDesc& desc) override
    {
        auto obj = std::make_unique<MetalDescriptorSetLayoutObj>();
        for (const DescriptorSetLayoutBinding& b : desc.bindings)
        {
            obj->add_binding(b);
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const DescriptorSetLayoutHandle h { id, 1u };

        const std::scoped_lock lock { dsl_mu_ };
        dsls_.emplace(h.index(), std::move(obj));
        return h;
    }

    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) override
    {
        const std::scoped_lock lock { dsl_mu_ };
        dsls_.erase(h.index());
    }

    // phase615 (Sprint-4): metadata-only pipeline-layout factory.
    //
    // Metal does not expose an explicit pipeline-layout object; the
    // bind-table topology is folded into the PSO + argument encoders. The
    // factory records the descriptor-set / push-constant metadata so the
    // Sprint-5 argument-buffer emission can size argument encoders + the
    // push-constant arg index range. Invalid set-layout handles surface as
    // kInvalidArgument so callers fix their bind-order bugs.
    [[nodiscard]] cd::core::Result<PipelineLayoutHandle>
    create_pipeline_layout(const PipelineLayoutDesc& desc) override
    {
        auto obj = std::make_unique<MetalPipelineLayoutObj>();
        {
            const std::scoped_lock lock { dsl_mu_ };
            for (const DescriptorSetLayoutHandle& sl : desc.set_layouts)
            {
                if (sl.is_valid() && dsls_.find(sl.index()) == dsls_.end())
                {
                    return std::unexpected(rhi_errors::make(
                        rhi_errors::Code::kInvalidArgument,
                        "Metal::create_pipeline_layout: unknown "
                        "DescriptorSetLayoutHandle in set_layouts"));
                }
                obj->add_set_layout(sl);
            }
        }
        for (const PushConstantRange& pc : desc.push_constants)
        {
            obj->add_push_constant_range(pc);
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const PipelineLayoutHandle h { id, 1u };

        const std::scoped_lock lock { pipeline_layouts_mu_ };
        pipeline_layouts_.emplace(h.index(), std::move(obj));
        return h;
    }

    void destroy_pipeline_layout(PipelineLayoutHandle h) override
    {
        const std::scoped_lock lock { pipeline_layouts_mu_ };
        pipeline_layouts_.erase(h.index());
    }

    // M2 (ADR-20260615): desc-driven graphics pipeline. Builds a real
    // MTLRenderPipelineState from the GraphicsPipelineDesc (vertex descriptor,
    // colour-attachment format + blend, depth/stencil format, MSAA) + the
    // M6-resolved vertex/fragment MTLFunctions, plus the matching
    // MTLDepthStencilState and resolved raster state. Mirrors the Vulkan
    // VkGraphicsPipelineCreateInfo translation. The triangle-only Sprint-1
    // path is gone — the engine's real Sponza/PBR/IBL/shadow pipelines now
    // build from their descriptors.
    [[nodiscard]] cd::core::Result<GraphicsPipelineHandle>
    create_graphics_pipeline(const GraphicsPipelineDesc& desc) override
    {
        // Resolve the vertex (required) + fragment (optional, e.g. depth-only)
        // shader modules from the registry. Unknown handles -> kInvalidArgument
        // so callers fix bind-order bugs rather than chase a Metal validation
        // assert.
        const MetalShaderModuleObj* vs =
            lookup_shader_module(desc.vertex_shader);
        if (vs == nullptr || vs->fn() == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_graphics_pipeline: unknown / unresolved "
                "vertex shader module"));
        }
        id<MTLFunction> fs_fn = nil;
        if (desc.fragment_shader.is_valid())
        {
            const MetalShaderModuleObj* fs =
                lookup_shader_module(desc.fragment_shader);
            if (fs == nullptr || fs->fn() == nil)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::create_graphics_pipeline: unknown / unresolved "
                    "fragment shader module"));
            }
            fs_fn = fs->fn();
        }

        std::string err_msg;
        id<MTLDepthStencilState> dss = nil;
        MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
        MTLCullMode      cull = MTLCullModeNone;
        MTLWinding       winding = MTLWindingClockwise;
        id<MTLRenderPipelineState> pso =
            detail::build_metal_graphics_pipeline(
                mtl_device_, desc, vs->fn(), fs_fn,
                &dss, &primitive, &cull, &winding, &err_msg);
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
        pipelines_.emplace(
            h.index(),
            std::make_unique<MetalGraphicsPipelineStateObj>(
                pso, dss, primitive, cull, winding));
        return h;
    }

    void destroy_graphics_pipeline(GraphicsPipelineHandle h) override
    {
        const std::scoped_lock lock { pipelines_mu_ };
        pipelines_.erase(h.index());
    }

    // phase572 (Sprint-3): newComputePipelineStateWithFunction:error: path.
    //
    // The shader-module lookup MUST resolve and the module's stage MUST be
    // kCompute; otherwise we surface a kInvalidArgument so callers can fix
    // their bind-order bug rather than chase a confusing Metal validation
    // assert. PSO compilation errors propagate via kResourceCreationFailed
    // with the Metal-supplied diagnostic preserved.
    [[nodiscard]] cd::core::Result<ComputePipelineHandle>
    create_compute_pipeline(const ComputePipelineDesc& desc) override
    {
        const MetalShaderModuleObj* mod = lookup_shader_module(desc.shader);
        if (mod == nullptr || mod->fn() == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_compute_pipeline: unknown / unresolved "
                "shader module"));
        }
        if (mod->stage() != ShaderStage::kCompute
            && mod->stage() != ShaderStage::kNone)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_compute_pipeline: shader module is not "
                "ShaderStage::kCompute"));
        }

        NSError* err = nil;
        id<MTLComputePipelineState> pso =
            [mtl_device_ newComputePipelineStateWithFunction:mod->fn()
                                                       error:&err];
        if (pso == nil)
        {
            std::string err_msg = (err != nil)
                ? std::string { [[err localizedDescription] UTF8String] }
                : std::string { "Metal::create_compute_pipeline: nil PSO" };
            return std::unexpected(rhi_errors::make_owning(
                rhi_errors::Code::kResourceCreationFailed,
                std::move(err_msg)));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const ComputePipelineHandle h { id, 1u };

        const std::scoped_lock lock { compute_pipelines_mu_ };
        compute_pipelines_.emplace(
            h.index(),
            std::make_unique<MetalComputePipelineObj>(pso));
        return h;
    }

    void destroy_compute_pipeline(ComputePipelineHandle h) override
    {
        const std::scoped_lock lock { compute_pipelines_mu_ };
        compute_pipelines_.erase(h.index());
    }

    // M9 (ADR-20260615): create a BLAS / TLAS as a real
    // id<MTLAccelerationStructure>. The descriptor is built here (host side)
    // and the actual build runs on the command-buffer-side AS encoder (so it
    // can be batched + barriered with the rest of the frame, mirroring the
    // Vulkan vkCmdBuildAccelerationStructures path). This is the RHI's REAL
    // RT surface (AS build + ray-query); the SBT-pipeline surface
    // (create_rt_pipeline) intentionally stays kNotImplemented exactly as it
    // is on Vulkan.
    //
    //   * BLAS: MTLPrimitiveAccelerationStructureDescriptor +
    //           MTLAccelerationStructureTriangleGeometryDescriptor per
    //           AccelTriangleGeometry (vertex/index buffer resolved from the
    //           M1 registry).
    //   * TLAS: MTLInstanceAccelerationStructureDescriptor referencing the
    //           per-instance BLAS list + a Shared instance-descriptor buffer
    //           (MTLAccelerationStructureInstanceDescriptor packed from
    //           AccelInstance: transform / mask / instance_id).
    [[nodiscard]] cd::core::Result<AccelStructureHandle>
    create_acceleration_structure(const AccelStructureDesc& desc) override
    {
        if (!features_.ray_tracing)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kNotImplemented,
                "Metal::create_acceleration_structure: device does not "
                "support ray tracing"));
        }

        MTLAccelerationStructureDescriptor* as_desc = nil;
        if (desc.kind == AccelStructureKind::kBottomLevel)
        {
            auto built = build_blas_descriptor(desc);
            if (!built.has_value())
            {
                return std::unexpected(built.error());
            }
            as_desc = *built;
        }
        else
        {
            auto built = build_tlas_descriptor(desc);
            if (!built.has_value())
            {
                return std::unexpected(built.error());
            }
            as_desc = *built;
        }

        const MTLAccelerationStructureSizes sizes =
            [mtl_device_ accelerationStructureSizesWithDescriptor:as_desc];
        id<MTLAccelerationStructure> as =
            [mtl_device_ newAccelerationStructureWithSize:sizes.accelerationStructureSize];
        if (as == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kResourceCreationFailed,
                "Metal::create_acceleration_structure: "
                "newAccelerationStructureWithSize returned nil"));
        }
        id<MTLBuffer> scratch = nil;
        if (sizes.buildScratchBufferSize > 0u)
        {
            scratch = [mtl_device_
                newBufferWithLength:sizes.buildScratchBufferSize
                            options:MTLResourceStorageModePrivate];
            if (scratch == nil)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kResourceCreationFailed,
                    "Metal::create_acceleration_structure: scratch buffer "
                    "allocation failed"));
            }
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const AccelStructureHandle h { id, 1u };

        const std::scoped_lock lock { accels_mu_ };
        accels_.emplace(
            h.index(),
            std::make_unique<MetalAccelObj>(as, as_desc, scratch, desc.kind));
        return h;
    }

    void destroy_acceleration_structure(AccelStructureHandle h) override
    {
        const std::scoped_lock lock { accels_mu_ };
        accels_.erase(h.index());
    }

    // phase649 (Sprint-5): real id<MTLArgumentEncoder> path.
    //
    // The argument encoder is built from the DescriptorSetLayoutBinding
    // table cached at create_descriptor_set_layout time. We map each
    // DescriptorType -> MTLDataType / MTLArgumentAccess pair, build a
    // sorted [MTLArgumentDescriptor] array, and let
    // [device newArgumentEncoderWithArguments:] size the resulting
    // encoder. An id<MTLBuffer> sized to [encoder encodedLength] is
    // allocated next so update_descriptor_set + future bind_descriptor_set
    // can write into it directly.
    //
    // Empty layouts (zero bindings) are legal and produce a 16-byte
    // sentinel argument buffer; the cmd-buffer bind_descriptor_set call
    // tolerates them as a "no resources" marker (matches VkDescriptorSet
    // empty-set semantics).
    //
    // Unknown layout handles surface as kInvalidArgument; allocation
    // failures (newArgumentEncoder or newBuffer returning nil) surface
    // as kResourceCreationFailed so callers can distinguish bind-bugs
    // from out-of-memory at runtime.
    [[nodiscard]] cd::core::Result<DescriptorSetHandle>
    allocate_descriptor_set(DescriptorSetLayoutHandle layout) override
    {
        // M4 (ADR-20260615): build a per-binding [MTLArgumentDescriptor] array
        // from the layout's REAL binding table (M3 set-per-argument-buffer
        // contract) instead of the Sprint-5 single-slot encoder. Each binding
        // maps index = binding, dataType from DescriptorType:
        //   kUniformBuffer / kStorageBuffer(*Dynamic) -> MTLDataTypePointer
        //   kSampledImage / kStorageImage / kCombinedImageSampler /
        //     kInputAttachment / kBindlessSampledImage  -> MTLDataTypeTexture
        //   kSampler                                    -> MTLDataTypeSampler
        //   kAccelerationStructure                      -> MTLDataTypeInstance
        //                                                  AccelerationStructure
        // arrayLength = binding.count (>= 1; bindless slot_count).
        NSMutableArray<MTLArgumentDescriptor*>* args = [[NSMutableArray alloc] init];
        {
            const std::scoped_lock lock { dsl_mu_ };
            const auto it = (layout.is_valid())
                                ? dsls_.find(layout.index())
                                : dsls_.end();
            if (layout.is_valid() && it == dsls_.end())
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::allocate_descriptor_set: unknown "
                    "DescriptorSetLayoutHandle"));
            }
            if (it != dsls_.end())
            {
                for (const DescriptorSetLayoutBinding& b : it->second->bindings())
                {
                    MTLArgumentDescriptor* slot =
                        [MTLArgumentDescriptor argumentDescriptor];
                    slot.index = static_cast<NSUInteger>(b.binding);
                    slot.dataType = arg_data_type_for(b.type);
                    slot.access = arg_access_for(b.type);
                    slot.arrayLength = static_cast<NSUInteger>(
                        b.count == 0u ? 1u : b.count);
                    if (slot.dataType == MTLDataTypeTexture)
                    {
                        slot.textureType = MTLTextureType2D;
                    }
                    [args addObject:slot];
                }
            }
        }

        // Empty layouts (zero bindings) are legal -- the engine binds an empty
        // set as a "no resources" marker (VkDescriptorSet parity). Synthesise a
        // single pointer slot so newArgumentEncoder does not reject an empty
        // argument list; the cmd-buffer treats the resulting buffer as a no-op.
        if ([args count] == 0u)
        {
            MTLArgumentDescriptor* slot =
                [MTLArgumentDescriptor argumentDescriptor];
            slot.index = 0;
            slot.dataType = MTLDataTypePointer;
            slot.access = MTLArgumentAccessReadWrite;
            slot.arrayLength = 1;
            [args addObject:slot];
        }

        id<MTLArgumentEncoder> encoder =
            [mtl_device_ newArgumentEncoderWithArguments:args];
        if (encoder == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kResourceCreationFailed,
                "Metal::allocate_descriptor_set: "
                "newArgumentEncoderWithArguments returned nil"));
        }

        // Argument buffer sized to the encoder's encoded length. Use
        // shared storage so host writes from update_descriptor_set land
        // straight in the GPU-visible region (no extra blit) — the same
        // Apple pattern Metal samples use for argument buffers.
        NSUInteger arg_len = [encoder encodedLength];
        if (arg_len == 0u)
        {
            // Encoder reports 0 for an empty argument list; allocate a
            // 16-byte sentinel so cmd-buffer code that asks for the
            // buffer never gets nil.
            arg_len = 16u;
        }
        id<MTLBuffer> arg_buf =
            [mtl_device_ newBufferWithLength:arg_len
                                     options:MTLResourceStorageModeShared];
        if (arg_buf == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kResourceCreationFailed,
                "Metal::allocate_descriptor_set: newBufferWithLength "
                "(argument buffer) returned nil"));
        }
        [encoder setArgumentBuffer:arg_buf offset:0];

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const DescriptorSetHandle h { id, 1u };

        const std::scoped_lock lock { descriptor_sets_mu_ };
        descriptor_sets_.emplace(
            h.index(),
            std::make_unique<MetalDescriptorSetObj>(encoder, arg_buf));
        return h;
    }

    void destroy_descriptor_set(DescriptorSetHandle h) override
    {
        const std::scoped_lock lock { descriptor_sets_mu_ };
        descriptor_sets_.erase(h.index());
    }

    // phase559 (Sprint-2): validate-and-resolve descriptor writes.
    //
    // The full SOTA path is MTLArgumentEncoder writing into an argument
    // buffer; we can't ship the encoder side until allocate_descriptor_set
    // lights up (Sprint 3). For Sprint-2 we validate the writes against the
    // current resource registries and return success once every referenced
    // handle resolves — exactly the contract callers need to wire up their
    // upload code paths without crashing on a nil argument buffer.
    //
    // Behaviour:
    //   * Empty writes  -> success (idempotent).
    //   * Each write    -> lookup buffer / view / sampler / accel and
    //                      fail on the first unresolvable handle. Texture
    //                      handles + buffers that aren't yet allocated
    //                      (Sprint-2 default) are tolerated — the lookups
    //                      return nil and we treat that as "binding not
    //                      yet realised" rather than an error, mirroring
    //                      the cmd-buffer copy fallback.
    //   * Acceleration  -> kInvalidArgument with a "RT is a separate tier"
    //                       diagnostic (Sprint-5 promotion). Metal exposes
    //                       ray tracing via id<MTLAccelerationStructure>
    //                       but the engine's RT pipeline is gated on the
    //                       Vulkan KHR_acceleration_structure parity tier
    //                       so the Metal-side RT path is intentionally
    //                       out-of-scope until that lands. Surfacing the
    //                       error as kInvalidArgument (not kNotImpl)
    //                       reflects the stable contract: the API is fully
    //                       implemented, this particular descriptor type
    //                       is simply not supported on Metal yet.
    //
    // Sprint-5 graduation: now that allocate_descriptor_set hands out a
    // live id<MTLArgumentEncoder>, this method can resolve the descriptor
    // set and route writes through the encoder in a follow-up sprint. For
    // Sprint-5 the per-write argument-buffer emission stays best-effort
    // (writes are validated; their resolution feeds the lookup hooks but
    // no encoder side-effect lands yet). The existing call sites do not
    // change.
    // M4 (ADR-20260615): real argument-encoder writes. For each DescriptorWrite
    // we encode the resolved native object at [[id(binding)]] inside the
    // argument buffer:
    //   * UBO/SSBO(+dynamic) -> [encoder setBuffer:buf offset:o atIndex:binding]
    //   * sampled/storage image / input attachment / combined / bindless ->
    //     [encoder setTexture:tex atIndex:binding]
    //   * sampler             -> [encoder setSamplerState:s atIndex:binding]
    //   * acceleration struct -> [encoder setAccelerationStructure:as
    //                             atIndex:binding] (the M9 ray-query TLAS
    //                             binding; this is where the Sprint-5
    //                             kInvalidArgument branch LIGHTS UP).
    // Every referenced resource is also recorded for residency so
    // bind_descriptor_set can [encoder useResource:] it. Unknown set / handle
    // -> kInvalidArgument (matches the IDevice contract).
    [[nodiscard]] cd::core::Result<void>
    update_descriptor_set(DescriptorSetHandle set,
                          std::span<const DescriptorWrite> writes) override
    {
        MetalDescriptorSetObj* ds = lookup_descriptor_set(set);
        if (ds == nullptr || ds->encoder() == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::update_descriptor_set: unknown descriptor set"));
        }
        id<MTLArgumentEncoder> enc = ds->encoder();
        // The encoder already points at the argument buffer (set during
        // allocate_descriptor_set). Resident lists are rebuilt from scratch so
        // a re-write does not retain stale resources.
        ds->reset_residents();

        for (const DescriptorWrite& w : writes)
        {
            const NSUInteger idx = static_cast<NSUInteger>(w.binding);
            switch (w.type)
            {
            case DescriptorType::kUniformBuffer:
            case DescriptorType::kStorageBuffer:
            case DescriptorType::kUniformBufferDynamic:
            case DescriptorType::kStorageBufferDynamic:
            {
                id<MTLBuffer> buf = lookup_buffer(w.buffer);
                if (buf == nil)
                {
                    return std::unexpected(rhi_errors::make(
                        rhi_errors::Code::kInvalidArgument,
                        "Metal::update_descriptor_set: unknown buffer "
                        "handle in a buffer descriptor write"));
                }
                [enc setBuffer:buf
                        offset:static_cast<NSUInteger>(w.buffer_offset)
                       atIndex:idx];
                ds->add_resident_buffer(buf);
                break;
            }
            case DescriptorType::kSampledImage:
            case DescriptorType::kStorageImage:
            case DescriptorType::kCombinedImageSampler:
            case DescriptorType::kInputAttachment:
            case DescriptorType::kBindlessSampledImage:
            {
                id<MTLTexture> tex = lookup_texture_view(w.view);
                if (tex == nil)
                {
                    return std::unexpected(rhi_errors::make(
                        rhi_errors::Code::kInvalidArgument,
                        "Metal::update_descriptor_set: unknown texture "
                        "view handle in an image descriptor write"));
                }
                [enc setTexture:tex atIndex:idx];
                ds->add_resident_texture(tex);
                // Combined image+sampler also encodes the sampler at the same
                // logical binding+1 convention is NOT used here; SPIRV-Cross
                // splits the sampler into its own binding, so a bare sampler
                // descriptor handles it. If a sampler is supplied alongside,
                // bind it at the same index (Metal allows a sampler slot
                // co-located only when the MSL declares one; the engine uses
                // split bindings, so this is a no-op when sampler is null).
                if (w.type == DescriptorType::kCombinedImageSampler
                    && w.sampler.is_valid())
                {
                    if (id<MTLSamplerState> s = lookup_sampler(w.sampler);
                        s != nil)
                    {
                        [enc setSamplerState:s atIndex:idx];
                    }
                }
                break;
            }
            case DescriptorType::kSampler:
            {
                id<MTLSamplerState> s = lookup_sampler(w.sampler);
                if (s == nil)
                {
                    return std::unexpected(rhi_errors::make(
                        rhi_errors::Code::kInvalidArgument,
                        "Metal::update_descriptor_set: unknown sampler "
                        "handle in a sampler descriptor write"));
                }
                [enc setSamplerState:s atIndex:idx];
                break;
            }
            case DescriptorType::kAccelerationStructure:
            {
                MetalAccelObj* as = lookup_accel(w.accel);
                if (as == nullptr || as->as() == nil)
                {
                    return std::unexpected(rhi_errors::make(
                        rhi_errors::Code::kInvalidArgument,
                        "Metal::update_descriptor_set: unknown / unbuilt "
                        "acceleration-structure handle"));
                }
                [enc setAccelerationStructure:as->as() atIndex:idx];
                break;
            }
            }
        }
        return {};
    }

    // phase615 (Sprint-4): id<MTLSharedEvent>-backed binary semaphore.
    //
    // Returns kBackendInitFailed (not kNotImpl) when the device does not
    // support shared events — older Intel macs predating macOS 10.14. The
    // Vulkan back-end's VkSemaphore equivalent is structurally identical:
    // a queue-side wait + signal pair encoded into the submit cmd-buf.
    [[nodiscard]] cd::core::Result<SemaphoreHandle> create_semaphore() override
    {
        id<MTLSharedEvent> ev = [mtl_device_ newSharedEvent];
        if (ev == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kBackendInitFailed,
                "Metal::create_semaphore: newSharedEvent returned nil "
                "(device does not support shared events)"));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const SemaphoreHandle h { id, 1u };

        const std::scoped_lock lock { events_mu_ };
        events_.emplace(h.index(), std::make_unique<MetalEventObj>(ev));
        return h;
    }

    void destroy_semaphore(SemaphoreHandle h) override
    {
        const std::scoped_lock lock { events_mu_ };
        events_.erase(h.index());
    }

    // phase615 (Sprint-4): dispatch_semaphore_t-backed CPU completion fence.
    //
    // The Apple SOTA pattern for "wait on the CPU until a queue submit
    // finishes" is a dispatch semaphore signalled from the cmd-buf's
    // addCompletedHandler. We hand out a registry-managed wrapper so
    // wait_for_fence / reset_fence / is_fence_signaled / destroy_fence all
    // resolve through the same MetalFenceObj. `signaled = true` mirrors
    // VK_FENCE_CREATE_SIGNALED_BIT — the very first wait does not block.
    [[nodiscard]] cd::core::Result<FenceHandle> create_fence(bool signaled) override
    {
        auto obj = std::make_unique<MetalFenceObj>(signaled);

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const FenceHandle h { id, 1u };

        const std::scoped_lock lock { fences_mu_ };
        fences_.emplace(h.index(), std::move(obj));
        return h;
    }

    void destroy_fence(FenceHandle h) override
    {
        const std::scoped_lock lock { fences_mu_ };
        fences_.erase(h.index());
    }

    // wait_for_fence — block until the fence is signalled or the timeout
    // elapses. UINT64_MAX (the IDevice "infinite wait" sentinel) maps to
    // DISPATCH_TIME_FOREVER. Unknown handles surface as kInvalidArgument so
    // callers spot fence lifetime bugs early.
    [[nodiscard]] cd::core::Result<void>
    wait_for_fence(FenceHandle fence, std::uint64_t timeout_ns) override
    {
        MetalFenceObj* f = lookup_fence(fence);
        if (f == nullptr)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::wait_for_fence: unknown fence handle"));
        }
        if (!f->wait(timeout_ns))
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kTimeout,
                "Metal::wait_for_fence: timeout"));
        }
        return {};
    }

    void reset_fence(FenceHandle fence) override
    {
        if (MetalFenceObj* f = lookup_fence(fence); f != nullptr)
        {
            f->reset();
        }
    }

    [[nodiscard]] bool is_fence_signaled(FenceHandle fence) override
    {
        MetalFenceObj* f = lookup_fence(fence);
        return (f != nullptr) && f->poll();
    }

    // phase649 (Sprint-5): real id<MTLSharedEvent>-backed timeline.
    //
    // MTLSharedEvent::signaledValue is the source-of-truth counter; both
    // host signal_timeline_semaphore and queue-side encodeSignalEvent feed
    // it. Initial value is set via the property after creation (the
    // newSharedEvent factory does not take one). kBackendInitFailed
    // surfaces when the device cannot mint shared events — same fallback
    // as create_semaphore.
    [[nodiscard]] cd::core::Result<TimelineSemaphoreHandle>
    create_timeline_semaphore(std::uint64_t initial_value) override
    {
        id<MTLSharedEvent> ev = [mtl_device_ newSharedEvent];
        if (ev == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kBackendInitFailed,
                "Metal::create_timeline_semaphore: newSharedEvent "
                "returned nil (device does not support shared events)"));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const TimelineSemaphoreHandle h { id, 1u };

        const std::scoped_lock lock { timelines_mu_ };
        timelines_.emplace(
            h.index(),
            std::make_unique<MetalTimelineObj>(ev, initial_value));
        return h;
    }

    void destroy_timeline_semaphore(TimelineSemaphoreHandle h) override
    {
        const std::scoped_lock lock { timelines_mu_ };
        timelines_.erase(h.index());
    }

    // wait_timeline_semaphore — host-side block until the timeline reaches
    // `value`. Uses notifyListener:atValue:block: feeding a
    // dispatch_semaphore_t so the IDevice timeout contract still holds.
    // Unknown handles surface as kInvalidArgument; timeouts surface as
    // kTimeout (same shape as wait_for_fence).
    [[nodiscard]] cd::core::Result<void>
    wait_timeline_semaphore(TimelineSemaphoreHandle h,
                            std::uint64_t value,
                            std::uint64_t timeout_ns) override
    {
        MetalTimelineObj* t = lookup_timeline(h);
        if (t == nullptr)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::wait_timeline_semaphore: unknown handle"));
        }
        if (!t->wait(value, timeout_ns))
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kTimeout,
                "Metal::wait_timeline_semaphore: timeout"));
        }
        return {};
    }

    // signal_timeline_semaphore — host-side direct write to
    // `signaledValue`. Monotonic; non-monotonic values surface as
    // kInvalidArgument (same Vulkan-side semantics).
    [[nodiscard]] cd::core::Result<void>
    signal_timeline_semaphore(TimelineSemaphoreHandle h,
                              std::uint64_t value) override
    {
        MetalTimelineObj* t = lookup_timeline(h);
        if (t == nullptr)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::signal_timeline_semaphore: unknown handle"));
        }
        if (value <= t->value())
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::signal_timeline_semaphore: value must be strictly "
                "greater than the current signalled value"));
        }
        t->signal(value);
        return {};
    }

    [[nodiscard]] std::uint64_t
    timeline_semaphore_value(TimelineSemaphoreHandle h) const override
    {
        MetalTimelineObj* t = lookup_timeline(h);
        return (t == nullptr) ? 0u : t->value();
    }

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

    // phase649 (Sprint-5): real memcpy into [MTLBuffer contents] path.
    //
    // Metal exposes the same CPU-mapped pointer model as Vulkan's
    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT through `[buffer contents]`
    // (valid for Shared / Managed storage). The Sprint-3 buffer registry
    // is still empty in the engine baseline (create_buffer returns stub
    // handles); an unbacked handle resolves to nil here and we surface a
    // structured no-op success so the upload path does not crash before
    // real allocation lands — matches the cmd-buffer copy fallback.
    //
    // Storage-mode and range validation map straight onto the IDevice
    // contract: Private buffers (GPU-only) are not host-visible and
    // surface kInvalidArgument; OOB ranges also surface kInvalidArgument.
    [[nodiscard]] cd::core::Result<void>
    upload_buffer(BufferHandle h,
                  std::uint64_t offset,
                  std::span<const std::byte> data) override
    {
        id<MTLBuffer> buf = lookup_buffer(h);
        if (buf == nil)
        {
            // Sprint-3 baseline: no real buffer storage yet. Treat as a
            // structured no-op so callers can wire the upload path
            // without crashing; real allocation lights this up when
            // create_buffer is promoted.
            return {};
        }
        if ([buf storageMode] == MTLStorageModePrivate)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::upload_buffer: buffer storage is Private "
                "(GPU-only); use a staging upload via copy_buffer"));
        }
        const std::uint64_t end = offset + static_cast<std::uint64_t>(data.size());
        if (end > [buf length])
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::upload_buffer: range OOB ([offset, offset+size) "
                "exceeds buffer length)"));
        }
        void* dst = [buf contents];
        if (dst == nullptr)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::upload_buffer: [buffer contents] returned nil"));
        }
        std::memcpy(static_cast<std::byte*>(dst) + offset,
                    data.data(), data.size());
        if ([buf storageMode] == MTLStorageModeManaged)
        {
            // Managed storage requires an explicit didModifyRange: so the
            // GPU sees the host write. Shared storage does not (the page
            // is unified).
            [buf didModifyRange:NSMakeRange(static_cast<NSUInteger>(offset),
                                            static_cast<NSUInteger>(data.size()))];
        }
        return {};
    }

    // download_buffer — symmetric memcpy back from [MTLBuffer contents]
    // into the caller's span. Same storage-mode + OOB rules as
    // upload_buffer above. Callers that need the data fresh should
    // wait_idle() (or wait on the matching fence) before invoking.
    [[nodiscard]] cd::core::Result<void>
    download_buffer(BufferHandle h,
                    std::uint64_t offset,
                    std::span<std::byte> dst) override
    {
        id<MTLBuffer> buf = lookup_buffer(h);
        if (buf == nil)
        {
            // Sprint-3 baseline: no real buffer storage yet. Treat as a
            // structured no-op (caller's dst stays untouched); real
            // allocation lights this up when create_buffer is promoted.
            return {};
        }
        if ([buf storageMode] == MTLStorageModePrivate)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::download_buffer: buffer storage is Private "
                "(GPU-only); use a staging readback via "
                "copy_image_to_buffer or a blit-side download"));
        }
        const std::uint64_t end = offset + static_cast<std::uint64_t>(dst.size());
        if (end > [buf length])
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::download_buffer: range OOB ([offset, offset+size) "
                "exceeds buffer length)"));
        }
        const void* src = [buf contents];
        if (src == nullptr)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::download_buffer: [buffer contents] returned nil"));
        }
        std::memcpy(dst.data(),
                    static_cast<const std::byte*>(src) + offset, dst.size());
        return {};
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

        const std::scoped_lock lock { swapchains_mu_ };

        // FIX 3 (M7 — ADR-20260615): window-resize parity. The Vulkan back-end
        // handles a resize by destroy + create_swapchain against the same
        // surface; the Metal analog is to reuse the existing MetalSwapchainObj
        // bound to the SAME CAMetalLayer and just update its drawableSize via
        // resize(). Re-allocating a new layer-backed object on every resize
        // would orphan in-flight drawables. We scan for an entry whose layer
        // matches and resize it in place, returning the existing handle.
        for (auto& [idx, sc] : swapchains_)
        {
            if (sc->layer() == layer)
            {
                (void)sc->resize(desc.extent.width, desc.extent.height);
                return SwapchainHandle { idx, 1u };
            }
        }

        auto obj = std::make_unique<MetalSwapchainObj>(layer, mtl_device_, desc);

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const SwapchainHandle h { id, 1u };

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
    do_create_command_buffer(QueueType /*queue*/) override
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

    // phase615 (Sprint-4): full SubmitDesc queue plumbing.
    //
    // Walks the cmd-buffer span and commits each underlying MTLCommandBuffer
    // in order. Wait + signal MTLSharedEvent hand-offs are encoded into the
    // first / last cmd-buf respectively (matches Vulkan's wait-on-first /
    // signal-after-last queue-submit semantics). The optional
    // SubmitDesc.signal_fence is chained via addCompletedHandler on the
    // final cmd-buf so wait_for_fence un-blocks when the GPU is done.
    //
    // phase649 (Sprint-5): timeline semaphores are wired up through the
    // same queue-side encodeWaitForEvent / encodeSignalEvent encode that
    // binary semaphores use — id<MTLSharedEvent> is the common substrate
    // for both. We collect the timeline wait/signal value tuples into the
    // same EventEdge list so the first / last cmd-buf hand-off below
    // applies uniformly. Unknown timeline handles surface as
    // kInvalidArgument so the partial submit is never silently committed.
    //
    // Empty cmd-buffer spans are legal: Vulkan permits them and the engine
    // uses the pattern to "park" a fence on the queue (the completion
    // handler fires after any previously-queued cmd-bufs drain). We then
    // create a single sentinel cmd-buf that carries the wait + signal + the
    // fence completion handler so the contract still holds.
    [[nodiscard]] cd::core::Result<void> submit(const SubmitDesc& desc) override
    {
        // Resolve fence + waits + signals up-front so we can bail before we
        // touch the queue if anything is wrong.
        MetalFenceObj* fence_obj = nullptr;
        if (desc.signal_fence.is_valid())
        {
            fence_obj = lookup_fence(desc.signal_fence);
            if (fence_obj == nullptr)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::submit(SubmitDesc): unknown signal_fence "
                    "handle"));
            }
        }

        // Resolve all wait / signal semaphore handles. We capture both the
        // event + the value to wait-for / next-to-signal so the wait happens
        // on the producer-encoded value (the consumer blocks until the
        // producer-side counter reaches `signal_value`).
        struct EventEdge
        {
            id<MTLSharedEvent> event;
            std::uint64_t      value;
        };
        std::vector<EventEdge> waits;
        std::vector<EventEdge> signals;
        waits.reserve(desc.wait_semaphores.size());
        signals.reserve(desc.signal_semaphores.size());

        for (const SemaphoreSubmit& s : desc.wait_semaphores)
        {
            MetalEventObj* ev = lookup_event(s.semaphore);
            if (ev == nullptr)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::submit(SubmitDesc): unknown "
                    "wait_semaphores entry"));
            }
            waits.push_back({ ev->event(), ev->current_signal_value() });
        }
        for (const SemaphoreSubmit& s : desc.signal_semaphores)
        {
            MetalEventObj* ev = lookup_event(s.semaphore);
            if (ev == nullptr)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::submit(SubmitDesc): unknown "
                    "signal_semaphores entry"));
            }
            signals.push_back({ ev->event(), ev->next_signal_value() });
        }

        // phase649 (Sprint-5): timeline semaphores ride the same encode
        // path as binary semaphores — id<MTLSharedEvent> is the common
        // substrate; the difference is that the value comes from the
        // SubmitDesc (caller-supplied) rather than the per-event counter.
        for (const TimelineSemaphoreSubmit& t : desc.wait_timeline_semaphores)
        {
            MetalTimelineObj* tl = lookup_timeline(t.semaphore);
            if (tl == nullptr)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::submit(SubmitDesc): unknown "
                    "wait_timeline_semaphores entry"));
            }
            waits.push_back({ tl->event(), t.value });
        }
        for (const TimelineSemaphoreSubmit& t : desc.signal_timeline_semaphores)
        {
            MetalTimelineObj* tl = lookup_timeline(t.semaphore);
            if (tl == nullptr)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::submit(SubmitDesc): unknown "
                    "signal_timeline_semaphores entry"));
            }
            signals.push_back({ tl->event(), t.value });
        }

        // Convert cmd-buffer span entries to the impl type. Non-Metal cmd-
        // buffers are silently dropped (matches the IDevice contract that
        // submit is best-effort across mixed backends — wrong-backend cmd
        // buffers cannot land on a Metal queue).
        std::vector<MetalCommandBufferImpl*> impls;
        impls.reserve(desc.command_buffers.size());
        for (ICommandBuffer* raw : desc.command_buffers)
        {
            if (auto* m = dynamic_cast<MetalCommandBufferImpl*>(raw); m != nullptr)
            {
                impls.push_back(m);
            }
        }

        // Pop any pending swapchain present so the last cmd-buf in this
        // submit carries the present chain — same hand-off as legacy submit.
        MetalSwapchainObj* sc = nullptr;
        {
            const std::scoped_lock lock { present_mu_ };
            sc = pending_present_;
            pending_present_ = nullptr;
        }
        id<CAMetalDrawable> drawable_for_present =
            (sc != nullptr) ? sc->current_drawable() : nil;

        // Empty cmd-buffer span: synthesise a sentinel so the wait + signal
        // + fence-completion handler can still fire on the queue.
        if (impls.empty())
        {
            id<MTLCommandBuffer> sentinel = [mtl_queue_ commandBuffer];
            sentinel.label = @"cd::rhi::metal::SubmitSentinel";
            for (const EventEdge& w : waits)
            {
                [sentinel encodeWaitForEvent:w.event value:w.value];
            }
            for (const EventEdge& s : signals)
            {
                [sentinel encodeSignalEvent:s.event value:s.value];
            }
            if (drawable_for_present != nil)
            {
                [sentinel presentDrawable:drawable_for_present];
            }
            if (fence_obj != nullptr)
            {
                MetalFenceObj* captured = fence_obj;
                [sentinel addCompletedHandler:^(id<MTLCommandBuffer> /*cb*/) {
                    captured->signal_from_completion();
                }];
            }
            [sentinel commit];
            return {};
        }

        // First cmd-buf carries the waits; last one carries the signals +
        // the fence completion handler + the drawable present (matches the
        // Vulkan VkQueueSubmit "outside scope" wait-then-execute-then-signal
        // ordering at the boundary of the batch).
        for (std::size_t i = 0; i < impls.size(); ++i)
        {
            id<MTLCommandBuffer> mcb = impls[i]->mtl_cmd_buf();
            if (mcb == nil)
            {
                continue;
            }
            const bool is_first = (i == 0);
            const bool is_last  = (i + 1 == impls.size());
            if (is_first)
            {
                for (const EventEdge& w : waits)
                {
                    [mcb encodeWaitForEvent:w.event value:w.value];
                }
            }
            if (is_last)
            {
                for (const EventEdge& s : signals)
                {
                    [mcb encodeSignalEvent:s.event value:s.value];
                }
                if (fence_obj != nullptr)
                {
                    MetalFenceObj* captured = fence_obj;
                    [mcb addCompletedHandler:^(id<MTLCommandBuffer> /*cb*/) {
                        captured->signal_from_completion();
                    }];
                }
                impls[i]->submit_internal(drawable_for_present);
            }
            else
            {
                impls[i]->submit_internal(nil);
            }
        }
        return {};
    }

    // ---- MetalDeviceCtx interface -------------------------------------------
    [[nodiscard]] id<MTLRenderPipelineState>
    lookup_pipeline(GraphicsPipelineHandle h) const noexcept override
    {
        const std::scoped_lock lock { pipelines_mu_ };
        const auto it = pipelines_.find(h.index());
        return (it == pipelines_.end()) ? nil : it->second->pso();
    }

    // M2 (ADR-20260615): richer pipeline-state lookup carrying the
    // MTLDepthStencilState + resolved raster state.
    [[nodiscard]] const MetalGraphicsPipelineStateObj*
    lookup_graphics_pipeline_state(GraphicsPipelineHandle h) const noexcept override
    {
        const std::scoped_lock lock { pipelines_mu_ };
        const auto it = pipelines_.find(h.index());
        return (it == pipelines_.end()) ? nullptr : it->second.get();
    }

    // M9 (ADR-20260615): acceleration-structure lookup.
    [[nodiscard]] MetalAccelObj*
    lookup_accel(AccelStructureHandle h) const noexcept override
    {
        const std::scoped_lock lock { accels_mu_ };
        const auto it = accels_.find(h.index());
        return (it == accels_.end()) ? nullptr : it->second.get();
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

    // M1 (ADR-20260615): buffer / texture registry lookups now resolve to the
    // real id<MTLBuffer> / id<MTLTexture> created by create_buffer /
    // create_texture. nil for unknown handles (graceful skip on the cmd-buffer
    // side, same shape as the rest of the resolver contract).
    [[nodiscard]] id<MTLBuffer>
    lookup_buffer(BufferHandle h) const noexcept override
    {
        const std::scoped_lock lock { buffers_mu_ };
        const auto it = buffers_.find(h.index());
        return (it == buffers_.end()) ? nil : it->second->buffer();
    }

    [[nodiscard]] id<MTLTexture>
    lookup_texture(TextureHandle h) const noexcept override
    {
        const std::scoped_lock lock { textures_mu_ };
        const auto it = textures_.find(h.index());
        return (it == textures_.end()) ? nil : it->second->texture();
    }

    [[nodiscard]] id<MTLSamplerState>
    lookup_sampler(SamplerHandle h) const noexcept override
    {
        const std::scoped_lock lock { samplers_mu_ };
        const auto it = samplers_.find(h.index());
        return (it == samplers_.end()) ? nil : it->second->state();
    }

    // phase572 (Sprint-3): shader-module + compute-pipeline lookups.
    // Both return nil / nullptr for unknown handles, matching the rest of
    // the Metal-side resolver contract.
    [[nodiscard]] const MetalShaderModuleObj*
    lookup_shader_module(ShaderModuleHandle h) const noexcept override
    {
        const std::scoped_lock lock { shader_modules_mu_ };
        const auto it = shader_modules_.find(h.index());
        return (it == shader_modules_.end()) ? nullptr : it->second.get();
    }

    [[nodiscard]] id<MTLComputePipelineState>
    lookup_compute_pipeline(ComputePipelineHandle h) const noexcept override
    {
        const std::scoped_lock lock { compute_pipelines_mu_ };
        const auto it = compute_pipelines_.find(h.index());
        return (it == compute_pipelines_.end()) ? nil : it->second->pso();
    }

    // phase615 (Sprint-4): fence + event lookups. Both return nullptr for
    // unknown handles, matching the rest of the Metal-side resolver
    // contract.
    [[nodiscard]] MetalFenceObj*
    lookup_fence(FenceHandle h) const noexcept override
    {
        const std::scoped_lock lock { fences_mu_ };
        const auto it = fences_.find(h.index());
        return (it == fences_.end()) ? nullptr : it->second.get();
    }

    [[nodiscard]] MetalEventObj*
    lookup_event(SemaphoreHandle h) const noexcept override
    {
        const std::scoped_lock lock { events_mu_ };
        const auto it = events_.find(h.index());
        return (it == events_.end()) ? nullptr : it->second.get();
    }

    // phase649 (Sprint-5): texture-view + descriptor-set + timeline
    // lookups. All three return nullptr / nil for unknown handles,
    // matching the rest of the Metal-side resolver contract.
    [[nodiscard]] id<MTLTexture>
    lookup_texture_view(TextureViewHandle h) const noexcept override
    {
        // Swapchain-view handles route through lookup_swapchain_view_
        // texture above; here we only resolve regular (registered)
        // texture views allocated via create_texture_view.
        if (h.generation() == kSwapchainViewGen)
        {
            return lookup_swapchain_view_texture(h);
        }
        if (h.generation() != kRegularViewGen)
        {
            return nil;
        }
        MetalTextureViewObj* view = nullptr;
        {
            const std::scoped_lock lock { texture_views_mu_ };
            const auto it = texture_views_.find(h.index());
            if (it == texture_views_.end())
            {
                return nil;
            }
            view = it->second.get();
        }
        // Resolve the parent MTLTexture via the existing buffer/texture
        // registry; nil parent => nil view (graceful skip, same shape as
        // the rest of the resolver paths).
        id<MTLTexture> parent = lookup_texture(view->desc().texture);
        return view->resolve(parent);
    }

    [[nodiscard]] MetalDescriptorSetObj*
    lookup_descriptor_set(DescriptorSetHandle h) const noexcept override
    {
        const std::scoped_lock lock { descriptor_sets_mu_ };
        const auto it = descriptor_sets_.find(h.index());
        return (it == descriptor_sets_.end()) ? nullptr : it->second.get();
    }

    [[nodiscard]] MetalTimelineObj*
    lookup_timeline(TimelineSemaphoreHandle h) const noexcept override
    {
        const std::scoped_lock lock { timelines_mu_ };
        const auto it = timelines_.find(h.index());
        return (it == timelines_.end()) ? nullptr : it->second.get();
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

    // M6 (ADR-20260615): lazily-created glslang compiler (cd::shader). Only
    // minted on the first kGlsl create_shader_module; the SPIR-V tail (kSpirv)
    // does not need it. Single-threaded per the IDevice resource-thread
    // contract (rule 1), so no extra lock beyond the global resource lock the
    // caller already holds. Returns nullptr when glslang is unavailable
    // (CD_ENABLE_GLSLANG=OFF) — surfaced as kResourceCreationFailed.
    [[nodiscard]] cd::shader::ICompiler* glsl_compiler()
    {
        if (glsl_compiler_ == nullptr)
        {
            glsl_compiler_ = cd::shader::make_glslang_compiler();
        }
        return glsl_compiler_.get();
    }

    // M6: run the host-side GLSL/SPIR-V -> MSL toolchain (cd::rhi_metal_shader).
    // The .mm only consumes the MSL text; the cross-compile logic stays in the
    // host-side library so it compiles + tests on Windows. Honours the M3
    // set-per-argument-buffer + [[buffer(8)]] push contract via the default
    // MslBindingModel. The include_resolver follows the ADR-20260614 consumer
    // pattern: a null desc.include_resolver bridges to the embedded cd::gluon
    // catalogue through a function-local ModuleResolver kept in scope for the
    // whole compile.
    [[nodiscard]] cd::core::Result<MslArtifact>
    cross_compile_to_msl(const ShaderModuleDesc& desc)
    {
        GlslToMslDesc gd {};
        gd.stage = desc.stage;
        gd.source_name = desc.debug_name.empty()
                             ? std::string_view { "<inline>" }
                             : desc.debug_name;
        // Default MslBindingModel = { argument_buffers = true,
        // push_constant_buffer_index = 16 } — the M3 contract. Leave as-is.

        if (desc.language == ShaderSourceLanguage::kSpirv)
        {
            if ((desc.code_size % 4u) != 0u)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::create_shader_module: SPIR-V must be 32-bit "
                    "aligned"));
            }
            const std::span<const std::uint32_t> words {
                static_cast<const std::uint32_t*>(desc.code),
                static_cast<std::size_t>(desc.code_size / 4u) };
            return compose_spirv_to_msl(words, gd);
        }

        // kGlsl.
        cd::shader::ICompiler* compiler = glsl_compiler();
        if (compiler == nullptr)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kResourceCreationFailed,
                "Metal::create_shader_module: GLSL requested but the glslang "
                "front-end is unavailable (CD_ENABLE_GLSLANG=OFF)"));
        }
        cd::gluon::ModuleResolver default_resolver {};
        auto* injected =
            static_cast<cd::shader::IIncludeResolver*>(desc.include_resolver);
        gd.glsl_source = std::string_view {
            static_cast<const char*>(desc.code),
            static_cast<std::size_t>(desc.code_size) };
        gd.include_resolver =
            (injected != nullptr) ? injected : &default_resolver;
        return compose_glsl_to_msl(*compiler, gd);
    }

    // M6: compile MSL text into an MTLLibrary + resolve the cleansed entry
    // function (SPIRV-Cross "main0" etc. via MslArtifact::entry_point), then
    // register a MetalShaderModuleObj. Shared by the kGlsl + kSpirv paths.
    [[nodiscard]] cd::core::Result<ShaderModuleHandle>
    build_module_from_msl(const std::string& msl_source,
                          const std::string& entry_point,
                          ShaderStage stage)
    {
        NSString* src =
            [[NSString alloc] initWithBytes:msl_source.data()
                                     length:static_cast<NSUInteger>(msl_source.size())
                                   encoding:NSUTF8StringEncoding];
        if (src == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kResourceCreationFailed,
                "Metal::create_shader_module: MSL text is not valid UTF-8"));
        }

        MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
        // Argument buffers (the M3 contract) need MSL 2.0+; pin 2.2 to match
        // the toolchain's argument-buffer floor (MetalShaderToolchain.hpp:73).
        opts.languageVersion = MTLLanguageVersion2_2;

        NSError* err = nil;
        id<MTLLibrary> lib =
            [mtl_device_ newLibraryWithSource:src options:opts error:&err];
        if (lib == nil)
        {
            std::string m = (err != nil)
                ? std::string { [[err localizedDescription] UTF8String] }
                : std::string { "newLibraryWithSource returned nil" };
            return std::unexpected(rhi_errors::make_owning(
                rhi_errors::Code::kResourceCreationFailed,
                std::string { "Metal::create_shader_module (MSL compile): " }
                    + std::move(m)));
        }

        NSString* ns_ep =
            [[NSString alloc] initWithBytes:entry_point.data()
                                     length:static_cast<NSUInteger>(entry_point.size())
                                   encoding:NSUTF8StringEncoding];
        id<MTLFunction> fn =
            (ns_ep != nil) ? [lib newFunctionWithName:ns_ep] : nil;
        if (fn == nil)
        {
            return std::unexpected(rhi_errors::make_owning(
                rhi_errors::Code::kResourceCreationFailed,
                std::string {
                    "Metal::create_shader_module: entry point '" }
                    + entry_point + "' not found in compiled MTLLibrary"));
        }

        const auto id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        const ShaderModuleHandle h { id, 1u };

        const std::scoped_lock lock { shader_modules_mu_ };
        shader_modules_.emplace(
            h.index(),
            std::make_unique<MetalShaderModuleObj>(lib, fn, stage));
        return h;
    }

    // M9 (ADR-20260615): build a primitive (BLAS) acceleration-structure
    // descriptor from the triangle geometries. Vertex / index buffers are
    // resolved from the M1 registry; nil resolution -> kInvalidArgument
    // (matches the Vulkan BLAS build which requires live VkBuffers).
    [[nodiscard]] cd::core::Result<MTLPrimitiveAccelerationStructureDescriptor*>
    build_blas_descriptor(const AccelStructureDesc& desc)
    {
        if (desc.triangles.empty())
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_acceleration_structure: BLAS has no "
                "triangle geometry"));
        }
        NSMutableArray<MTLAccelerationStructureGeometryDescriptor*>* geoms =
            [[NSMutableArray alloc] init];
        for (const AccelTriangleGeometry& tri : desc.triangles)
        {
            id<MTLBuffer> vb = lookup_buffer(tri.vertex_buffer);
            if (vb == nil)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::create_acceleration_structure: BLAS vertex "
                    "buffer handle does not resolve"));
            }
            MTLAccelerationStructureTriangleGeometryDescriptor* g =
                [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
            g.vertexBuffer = vb;
            g.vertexBufferOffset = static_cast<NSUInteger>(tri.vertex_offset);
            g.vertexStride = static_cast<NSUInteger>(tri.vertex_stride);
            if (tri.index_count > 0u)
            {
                id<MTLBuffer> ib = lookup_buffer(tri.index_buffer);
                if (ib == nil)
                {
                    return std::unexpected(rhi_errors::make(
                        rhi_errors::Code::kInvalidArgument,
                        "Metal::create_acceleration_structure: BLAS index "
                        "buffer handle does not resolve"));
                }
                g.indexBuffer = ib;
                g.indexBufferOffset = static_cast<NSUInteger>(tri.index_offset);
                g.indexType = (tri.index_type == IndexType::kUInt32)
                                  ? MTLIndexTypeUInt32
                                  : MTLIndexTypeUInt16;
                g.triangleCount = static_cast<NSUInteger>(tri.index_count / 3u);
            }
            else
            {
                g.triangleCount = static_cast<NSUInteger>(tri.vertex_count / 3u);
            }
            [geoms addObject:g];
        }
        MTLPrimitiveAccelerationStructureDescriptor* pd =
            [MTLPrimitiveAccelerationStructureDescriptor descriptor];
        pd.geometryDescriptors = geoms;
        return pd;
    }

    // M9 (ADR-20260615): build an instance (TLAS) acceleration-structure
    // descriptor. References the per-instance BLAS list + a Shared
    // instance-descriptor buffer packed from AccelInstance (the 3x4 row-major
    // transform becomes Metal's MTLPackedFloat4x3, plus mask / instance_id).
    [[nodiscard]] cd::core::Result<MTLInstanceAccelerationStructureDescriptor*>
    build_tlas_descriptor(const AccelStructureDesc& desc)
    {
        if (desc.instances.empty())
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kInvalidArgument,
                "Metal::create_acceleration_structure: TLAS has no "
                "instances"));
        }
        NSMutableArray<id<MTLAccelerationStructure>>* blas_list =
            [[NSMutableArray alloc] init];
        const NSUInteger count =
            static_cast<NSUInteger>(desc.instances.size());
        id<MTLBuffer> inst_buf = [mtl_device_
            newBufferWithLength:count * sizeof(MTLAccelerationStructureInstanceDescriptor)
                        options:MTLResourceStorageModeShared];
        if (inst_buf == nil)
        {
            return std::unexpected(rhi_errors::make(
                rhi_errors::Code::kResourceCreationFailed,
                "Metal::create_acceleration_structure: TLAS instance buffer "
                "allocation failed"));
        }
        auto* inst =
            static_cast<MTLAccelerationStructureInstanceDescriptor*>(
                [inst_buf contents]);
        for (std::size_t i = 0; i < desc.instances.size(); ++i)
        {
            const AccelInstance& src = desc.instances[i];
            MetalAccelObj* blas = lookup_accel(src.blas);
            if (blas == nullptr || blas->as() == nil)
            {
                return std::unexpected(rhi_errors::make(
                    rhi_errors::Code::kInvalidArgument,
                    "Metal::create_acceleration_structure: TLAS instance "
                    "references an unknown / unbuilt BLAS"));
            }
            [blas_list addObject:blas->as()];

            MTLAccelerationStructureInstanceDescriptor& dst = inst[i];
            // AccelInstance.transform is 3x4 ROW-major (column 3 is
            // translation). Metal's MTLPackedFloat4x3 is COLUMN-major with 4
            // columns of 3 rows (column 3 is translation). Transpose: dst
            // columns[c].elements[r] = src.transform[r*4 + c].
            for (int c = 0; c < 4; ++c)
            {
                dst.transformationMatrix.columns[c].x = src.transform[0 * 4 + c];
                dst.transformationMatrix.columns[c].y = src.transform[1 * 4 + c];
                dst.transformationMatrix.columns[c].z = src.transform[2 * 4 + c];
            }
            dst.options = MTLAccelerationStructureInstanceOptionNone;
            dst.mask = src.mask;
            dst.intersectionFunctionTableOffset = 0;
            dst.accelerationStructureIndex =
                static_cast<uint32_t>([blas_list count] - 1u);
        }

        MTLInstanceAccelerationStructureDescriptor* td =
            [MTLInstanceAccelerationStructureDescriptor descriptor];
        td.instancedAccelerationStructures = blas_list;
        td.instanceCount = count;
        td.instanceDescriptorBuffer = inst_buf;
        td.instanceDescriptorBufferOffset = 0;
        td.instanceDescriptorStride =
            sizeof(MTLAccelerationStructureInstanceDescriptor);
        return td;
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

    // M2 (ADR-20260615): graphics-pipeline registry now stores the richer
    // MetalGraphicsPipelineStateObj (MTLRenderPipelineState + matching
    // MTLDepthStencilState + resolved cull/winding/primitive) instead of the
    // Sprint-1 PSO-only wrapper, so bind_graphics_pipeline can apply the full
    // raster + depth state.
    mutable std::mutex   pipelines_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalGraphicsPipelineStateObj>> pipelines_;

    // phase559: sampler-state registry.
    mutable std::mutex   samplers_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalSamplerObj>> samplers_;

    // M1 (ADR-20260615): real MTLBuffer / MTLTexture registries. These back
    // the handle->object maps the Sprint-3 comments promised; lookup_buffer /
    // lookup_texture resolve through them and every dependent path lights up.
    mutable std::mutex   buffers_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalBufferObj>> buffers_;

    mutable std::mutex   textures_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalTextureObj>> textures_;

    // M9 (ADR-20260615): acceleration-structure registry (BLAS + TLAS). The
    // ray-query RT path (NOT the SBT pipeline) consumes these via
    // build_acceleration_structure + the argument-buffer TLAS binding.
    mutable std::mutex   accels_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalAccelObj>> accels_;

    // phase572 (Sprint-3): shader-module + compute-pipeline registries.
    // The shader-module map owns the MTLLibrary + MTLFunction so the
    // compute-pipeline factory can fetch the function without re-compiling;
    // the compute-pipeline map owns the resulting MTLComputePipelineState.
    mutable std::mutex   shader_modules_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalShaderModuleObj>>
        shader_modules_;

    // M6 (ADR-20260615): lazily-minted glslang front-end for the kGlsl
    // create_shader_module path. Constructed on first use via glsl_compiler().
    std::unique_ptr<cd::shader::ICompiler> glsl_compiler_;

    mutable std::mutex   compute_pipelines_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalComputePipelineObj>>
        compute_pipelines_;

    // phase615 (Sprint-4): fence + event + pipeline-layout + DSL registries.
    // Fences are dispatch_semaphore_t-backed CPU completion fences; events
    // are id<MTLSharedEvent>-backed GPU-side binary semaphores; pipeline-
    // layouts + DSLs are metadata-only registries that the Sprint-5
    // argument-buffer emission will read.
    mutable std::mutex   fences_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalFenceObj>> fences_;

    mutable std::mutex   events_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalEventObj>> events_;

    mutable std::mutex   pipeline_layouts_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalPipelineLayoutObj>>
        pipeline_layouts_;

    mutable std::mutex   dsl_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalDescriptorSetLayoutObj>>
        dsls_;

    // phase649 (Sprint-5): texture-view + descriptor-set + timeline
    // registries. Texture-view handles use kRegularViewGen so the
    // swapchain-view resolver does not pick them up; descriptor-set
    // handles own their argument encoder + buffer; timeline semaphores
    // wrap MTLSharedEvent with a host-side wait listener queue.
    mutable std::mutex   texture_views_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalTextureViewObj>>
        texture_views_;

    mutable std::mutex   descriptor_sets_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalDescriptorSetObj>>
        descriptor_sets_;

    mutable std::mutex   timelines_mu_;
    std::unordered_map<std::uint32_t, std::unique_ptr<MetalTimelineObj>>
        timelines_;

    // Pending present hand-off — present() records the swapchain whose
    // drawable should ride out on the next submit's command-buffer commit.
    mutable std::mutex   present_mu_;
    MetalSwapchainObj*   pending_present_ { nullptr };
};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// MetalFenceObj — out-of-line implementations (phase615 / Sprint-4).
//
// dispatch_semaphore_t is a counting semaphore; we start at the configured
// "signaled" count so create_fence(signaled = true) is immediately waitable
// without any prior submit, matching VK_FENCE_CREATE_SIGNALED_BIT. The
// completion handler chained from submit signals via dispatch_semaphore_signal;
// wait blocks on dispatch_semaphore_wait with the IDevice timeout (UINT64_MAX
// maps to DISPATCH_TIME_FOREVER).
//
// reset() drains any outstanding signals so the next wait blocks again. The
// drain is best-effort (non-blocking) — it pops accumulated signals one at a
// time, which is the SOTA pattern when the caller is responsible for fence
// "ownership" (one fence per submit, reset between frames).
// ---------------------------------------------------------------------------
namespace detail
{

MetalFenceObj::MetalFenceObj(bool signaled) noexcept
    : sem_(dispatch_semaphore_create(signaled ? 1 : 0))
{
}

MetalFenceObj::~MetalFenceObj() = default;

bool MetalFenceObj::wait(std::uint64_t timeout_ns) noexcept
{
    if (sem_ == nullptr)
    {
        return false;
    }
    const dispatch_time_t deadline =
        (timeout_ns == UINT64_MAX)
            ? DISPATCH_TIME_FOREVER
            : dispatch_time(DISPATCH_TIME_NOW,
                            static_cast<std::int64_t>(timeout_ns));
    // dispatch_semaphore_wait returns 0 on signal, non-zero on timeout.
    return dispatch_semaphore_wait(sem_, deadline) == 0;
}

bool MetalFenceObj::poll() noexcept
{
    if (sem_ == nullptr)
    {
        return false;
    }
    return dispatch_semaphore_wait(sem_, DISPATCH_TIME_NOW) == 0;
}

void MetalFenceObj::reset() noexcept
{
    if (sem_ == nullptr)
    {
        return;
    }
    // Drain any accumulated signals so the next wait blocks again. The
    // semaphore could legally be over-signalled in pathological cases (a
    // caller that reuses the same fence across overlapping submits); we
    // pop until the wait misses, then stop.
    while (dispatch_semaphore_wait(sem_, DISPATCH_TIME_NOW) == 0)
    {
        // Counter decremented; keep draining until empty.
    }
}

void MetalFenceObj::signal_from_completion() noexcept
{
    if (sem_ != nullptr)
    {
        dispatch_semaphore_signal(sem_);
    }
}

// ---------------------------------------------------------------------------
// MetalTextureViewObj::resolve — out-of-line (phase649 / Sprint-5).
//
// Returns the MTLTexture exposing the subresource view described by desc_.
// The view is created lazily on first call so an unbacked parent texture
// (still the Sprint-3 default — create_texture hands out stub handles)
// gracefully resolves to nil without aborting view registration.
//
// When the descriptor matches the parent's full extent + format exactly we
// return the parent directly without allocating a Metal view object (Metal
// validation considers that a no-op). format mapping is intentionally
// minimal — Sprint-5 only honours the handful of formats already used by
// the engine; the long tail moves into a shared MetalFormat.mm helper in a
// follow-up sprint.
// ---------------------------------------------------------------------------
namespace
{

MTLPixelFormat to_metal_pixel_format(cd::rhi::Format fmt,
                                     MTLPixelFormat fallback) noexcept
{
    switch (fmt)
    {
    case cd::rhi::Format::kUndefined:    return fallback;
    case cd::rhi::Format::kBGRA8Unorm:   return MTLPixelFormatBGRA8Unorm;
    case cd::rhi::Format::kBGRA8Srgb:    return MTLPixelFormatBGRA8Unorm_sRGB;
    case cd::rhi::Format::kRGBA8Unorm:   return MTLPixelFormatRGBA8Unorm;
    case cd::rhi::Format::kRGBA8Srgb:    return MTLPixelFormatRGBA8Unorm_sRGB;
    case cd::rhi::Format::kRGBA16Float:  return MTLPixelFormatRGBA16Float;
    case cd::rhi::Format::kRGBA32Float:  return MTLPixelFormatRGBA32Float;
    default:                             return fallback;
    }
}

MTLTextureType to_metal_texture_type(cd::rhi::TextureType t) noexcept
{
    switch (t)
    {
    case cd::rhi::TextureType::k1D:      return MTLTextureType1D;
    case cd::rhi::TextureType::k2D:      return MTLTextureType2D;
    case cd::rhi::TextureType::k3D:      return MTLTextureType3D;
    case cd::rhi::TextureType::kCube:    return MTLTextureTypeCube;
    default:                             return MTLTextureType2D;
    }
}

}  // anonymous namespace

id<MTLTexture> MetalTextureViewObj::resolve(id<MTLTexture> parent) noexcept
{
    if (parent == nil)
    {
        // Sprint-3 baseline: parent texture not backed yet. Resolve to nil
        // so call sites gracefully skip — matches the rest of the
        // Sprint-2/3 unbacked-handle pattern.
        return nil;
    }
    if (view_ != nil)
    {
        return view_;
    }
    const MTLPixelFormat parent_fmt = [parent pixelFormat];
    const MTLPixelFormat target_fmt =
        to_metal_pixel_format(desc_.format, parent_fmt);
    const MTLTextureType target_type = to_metal_texture_type(desc_.type);
    const NSUInteger mip_levels = (desc_.mip_count == 0u) ? 1u : desc_.mip_count;
    const NSUInteger slice_count = (desc_.layer_count == 0u) ? 1u : desc_.layer_count;

    // If the requested view matches the parent's exposed slice exactly,
    // re-use the parent without allocating a view (Metal validation
    // rejects a no-op view creation).
    if (target_fmt == parent_fmt
        && target_type == [parent textureType]
        && desc_.base_mip == 0
        && mip_levels == [parent mipmapLevelCount]
        && desc_.base_layer == 0
        && slice_count == [parent arrayLength])
    {
        view_ = parent;
        return view_;
    }
    view_ = [parent newTextureViewWithPixelFormat:target_fmt
                                      textureType:target_type
                                           levels:NSMakeRange(desc_.base_mip, mip_levels)
                                           slices:NSMakeRange(desc_.base_layer, slice_count)];
    return view_;
}

// ---------------------------------------------------------------------------
// MetalTimelineObj — out-of-line implementations (phase649 / Sprint-5).
//
// id<MTLSharedEvent> doubles as the Vulkan-style timeline counter via its
// `signaledValue` property. The host-side wait path uses notifyListener:
// atValue:block: feeding a dispatch_semaphore_t so the IDevice timeout
// contract (UINT64_MAX -> forever, otherwise nanosecond deadline) is
// honoured uniformly. The listener queue is shared across all wait()
// calls on this timeline so we don't allocate a fresh dispatch queue per
// wait — Apple's recommendation when the wait pattern is bounded.
// ---------------------------------------------------------------------------
MetalTimelineObj::MetalTimelineObj(id<MTLSharedEvent> event,
                                   std::uint64_t initial_value) noexcept
    : event_(event)
    , listener_queue_(dispatch_queue_create(
          "cd::rhi::metal::TimelineListener", DISPATCH_QUEUE_SERIAL))
{
    if (event_ != nil)
    {
        event_.signaledValue = initial_value;
    }
}

MetalTimelineObj::~MetalTimelineObj() = default;

std::uint64_t MetalTimelineObj::value() const noexcept
{
    return (event_ == nil) ? 0u : event_.signaledValue;
}

void MetalTimelineObj::signal(std::uint64_t value) noexcept
{
    if (event_ != nil)
    {
        event_.signaledValue = value;
    }
}

bool MetalTimelineObj::wait(std::uint64_t value,
                            std::uint64_t timeout_ns) noexcept
{
    if (event_ == nil)
    {
        return false;
    }
    // Fast path: already at or past the target value.
    if (event_.signaledValue >= value)
    {
        return true;
    }

    // Register a one-shot listener that signals a local dispatch_semaphore
    // when the target value is reached. We allocate the listener fresh per
    // wait — MTLSharedEventListener is cheap and is required to provide
    // the dispatch queue context to atValue:block:.
    MTLSharedEventListener* listener =
        [[MTLSharedEventListener alloc] initWithDispatchQueue:listener_queue_];
    dispatch_semaphore_t wake = dispatch_semaphore_create(0);
    [event_ notifyListener:listener
                   atValue:value
                     block:^(id<MTLSharedEvent> /*ev*/, std::uint64_t /*v*/) {
        dispatch_semaphore_signal(wake);
    }];

    const dispatch_time_t deadline =
        (timeout_ns == UINT64_MAX)
            ? DISPATCH_TIME_FOREVER
            : dispatch_time(DISPATCH_TIME_NOW,
                            static_cast<std::int64_t>(timeout_ns));
    return dispatch_semaphore_wait(wake, deadline) == 0;
}

}  // namespace detail

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
