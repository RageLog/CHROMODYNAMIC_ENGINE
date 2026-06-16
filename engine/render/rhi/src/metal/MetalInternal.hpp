// =============================================================================
// CHROMODYNAMIC — engine/render/rhi/src/metal/MetalInternal.hpp
// phase548 — Metal backend Sprint-1 shared internals.
// phase559 — Metal backend Sprint-2 extensions (sampler registry +
//            blit-encoder helpers + push-constant fast path).
// phase572 — Metal backend Sprint-3 extensions (shader-module + compute
//            pipeline registries + compute encoder + vertex/index binding
//            cache + draw_indexed path).
// phase615 — Metal backend Sprint-4 extensions (fence + event + pipeline-
//            layout + descriptor-set-layout registries + submit(SubmitDesc)
//            queue plumbing).
// phase649 — Metal backend Sprint-5 extensions (texture-view registry +
//            descriptor-set argument-encoder registry + timeline-semaphore
//            registry + upload/download_buffer shared-storage memcpy +
//            SubmitDesc timeline-encode hand-off). Closes the remaining
//            9 kNotImpl sites and brings the Metal backend to 27/27
//            implemented coverage on the IDevice surface (RT-related
//            DescriptorWrite::kAccelerationStructure still surfaces
//            kInvalidArgument since Metal RT is a separate roadmap item).
//
// This header is INTERNAL to the cd_rhi_metal target. It is only included
// from the four .mm translation units (MetalDevice.mm, MetalCommandBuffer.mm,
// MetalSwapchain.mm, MetalPipeline.mm) so it can rely on Objective-C++
// (`id<...>`) types and ARC. Never expose this header to consumers.
//
// Sprint-1 surface (phase548) — exactly enough to draw 1 clear+triangle
// frame end-to-end on macOS:
//   * MetalSwapchainObj         — CAMetalLayer + drawable acquire/present
//   * MetalGraphicsPipelineObj  — MTLRenderPipelineState (inline MSL)
//   * MetalCommandBufferImpl    — ICommandBuffer wrapper around
//                                 id<MTLCommandBuffer> + render encoder
//
// Sprint-2 additions (phase559):
//   * MetalSamplerObj           — id<MTLSamplerState> wrapper
//   * MetalDeviceCtx::lookup_sampler / lookup_buffer / lookup_texture —
//     handle resolution hooks consumed by the cmd-buffer copy + descriptor
//     paths. Buffer/texture maps are intentionally empty in Sprint-2; real
//     MTLBuffer/MTLTexture allocation lands in Sprint 3 alongside
//     upload_buffer + create_buffer / create_texture promotion.
//   * MetalCommandBufferImpl::copy_buffer / copy_buffer_to_image           —
//     MTLBlitCommandEncoder path; gracefully no-ops when handles are not
//     yet backed by real Metal resources (Sprint 3 lights them up).
//   * MetalCommandBufferImpl::push_constants                                —
//     setVertexBytes / setFragmentBytes inline-arg path (≤4 KB).
//
// Sprint-3 additions (phase572):
//   * MetalShaderModuleObj      — id<MTLLibrary> + id<MTLFunction> + stage
//                                 wrapper. MSL source compiled via
//                                 [device newLibraryWithSource:] from the
//                                 ShaderModuleDesc::code byte blob (treated
//                                 as a UTF-8 MSL source string for Sprint-3;
//                                 SPIRV-Cross MSL translation lands later).
//   * MetalComputePipelineObj   — id<MTLComputePipelineState> wrapper for
//                                 the create_compute_pipeline factory.
//   * MetalDeviceCtx::lookup_shader_module / lookup_compute_pipeline —
//     handle resolution hooks for the create_compute_pipeline + the
//     cmd-buffer bind_compute_pipeline paths.
//   * MetalCommandBufferImpl::bind_compute_pipeline / dispatch —
//     MTLComputeCommandEncoder path with the same lazy-open / encoder-
//     transition discipline as the blit encoder. Render + blit encoders
//     are closed before a compute encoder opens (Metal forbids nested
//     encoders on a single cmd-buf).
//   * MetalCommandBufferImpl::bind_vertex_buffer                           —
//     [encoder setVertexBuffer:offset:atIndex:] direct call. Vertex
//     buffer table indices are taken from VertexBinding::binding.
//   * MetalCommandBufferImpl::bind_index_buffer                            —
//     Caches the buffer + offset + MTLIndexType for draw_indexed.
//   * MetalCommandBufferImpl::draw_indexed                                 —
//     [encoder drawIndexedPrimitives:..:indexBuffer:..:instanceCount:
//     baseVertex:baseInstance:] using the cached index state.
//
// Sprint-5 additions (phase649):
//   * MetalTextureViewObj       — TextureViewDesc + resolved id<MTLTexture>
//                                 (subresource view via newTextureViewWith*
//                                 when backing texture is live; nil
//                                 gracefully when the texture handle is
//                                 still unbacked).
//   * MetalDescriptorSetObj     — id<MTLArgumentEncoder> + the underlying
//                                 argument id<MTLBuffer> sized via
//                                 [encoder encodedLength]. Sprint-5 stores
//                                 the encoder/buffer per DescriptorSetHandle
//                                 so update_descriptor_set + future
//                                 bind_descriptor_set call sites can write
//                                 into the argument buffer directly.
//   * MetalTimelineObj          — id<MTLSharedEvent>-backed timeline
//                                 semaphore. signaledValue is the source
//                                 of truth; host wait uses notifyListener:
//                                 atValue:block: + dispatch_semaphore_t
//                                 to keep the IDevice timeout contract.
//   * MetalDeviceCtx::lookup_texture_view / lookup_descriptor_set /
//     lookup_timeline — handle resolution hooks (Sprint-5 closes the
//     final kNotImpl surface for these).
//
// With Sprint-5 in place every IDevice method that was returning
// kNotImpl now returns either a real result or a structured
// kInvalidArgument / kBackendInitFailed — the Metal backend is
// feature-complete on the IDevice contract modulo RT (which is gated
// on a separate Metal Ray-Tracing tier roadmap item and reported via
// kInvalidArgument from update_descriptor_set's acceleration-structure
// branch).
// =============================================================================
#pragma once

#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cd::rhi::metal::detail
{

// ---------------------------------------------------------------------------
// Metal buffer-index binding contract (FIX 1 — ADR-20260615 namespace fix,
// hardened phase1122 for SPIRV-Cross aux-buffer disjointness).
//
// Metal shares ONE [[buffer(N)]] index namespace per shader stage across FOUR
// distinct resource classes, all capped at Metal's per-stage limit of 31 slots
// ([[buffer(0..30)]]):
//   (a) descriptor-set argument buffers (M3 set-per-argument-buffer contract),
//   (b) the push-constant block,
//   (c) vertex-input (stage_in) buffers,
//   (d) SPIRV-Cross CompilerMSL's OWN auxiliary buffers (swizzle / buffer-size /
//       indirect-params / view-mask / output / tess / dynamic-offsets / input /
//       index — emitted by CompilerMSL under argument_buffers / multi-stage).
// If ANY two overlap a buffer bound for one class silently aliases another and
// the scene fails to render OR Metal PSO validation rejects the program.
//
// SPIRV-Cross's aux buffers DEFAULT to the TOP of the namespace, [20..30]
// (spirv_msl.hpp Options: shader_patch_input=20, shader_index=21,
// shader_input=22, dynamic_offsets=23, view_mask=24, buffer_size=25,
// shader_tess_factor=26, shader_patch_output=27, shader_output=28,
// indirect_params=29, swizzle=30). The PRE-FIX vertex base of 24 put
// vertex-input buffers at [24..30], DIRECTLY aliasing seven of those aux slots
// (e.g. the buffer-size buffer at 25, commonly emitted under argument_buffers).
//
// FIX: pin SPIRV-Cross's aux buffers to their canonical top range [20..30]
// (Translate.cpp translate_msl_impl sets them EXPLICITLY so they are
// deterministic and cannot drift) and relocate vertex-input DOWN to [9..15],
// strictly below the aux range. The MTLVertexDescriptor.layouts[binding]
// .bufferIndex (MetalPipeline.mm) and bind_vertex_buffer (MetalCommandBuffer.mm)
// BOTH add kVertexBufferBaseIndex so the PSO vertex layout and the runtime bind
// agree, and the emitted MSL never places any class in the vertex range.
//
//   Full per-stage buffer-index map (DISJOINT by construction):
//   ┌────────────┬───────────────────────────────────────────────────────┐
//   │ [[buffer]] │ owner                                                   │
//   ├────────────┼───────────────────────────────────────────────────────┤
//   │ 0 .. 7     │ descriptor-set argument buffers (set index == buffer)   │
//   │            │ K = kMaxDescriptorSetSlots-1 = 7 (engine uses sets 0,1) │
//   │ 8          │ push_constant block (kMetalPushConstantBufferIndex, M3) │
//   │ 9 .. 15    │ vertex-input buffers (kVertexBufferBaseIndex + binding) │
//   │ 16 .. 19   │ HEADROOM (unowned — future use)                         │
//   │ 20 .. 30   │ SPIRV-Cross CompilerMSL aux buffers (pinned, Translate) │
//   └────────────┴───────────────────────────────────────────────────────┘
//
// The four classes are pairwise disjoint: sets [0..7], push [8], vertex [9..15],
// aux [20..30], with [16..19] left as headroom. Vertex is STRICTLY BELOW the aux
// floor of 20 so it can never alias an aux buffer. Every value stays within the
// Metal per-stage cap of 31 slots. This is the engine's canonical Metal binding
// contract; the .mm bind sites + the SPIRV-Cross aux pin + the host test
// (test_metal_shader_toolchain.cpp) ALL reference these shared constants.
// ---------------------------------------------------------------------------

// Highest-plus-one descriptor-set argument-buffer slot the engine reserves. The
// prim pipeline declares sets 0 (per-prim) and 1 (bindless); we reserve [0..7]
// so future sets have head-room while keeping the whole map under the 31-slot
// cap. Argument buffers therefore live in [[buffer(0..7)]].
inline constexpr std::uint32_t kMaxDescriptorSetSlots = 8U;

// Push-constant block slot — mirror of cd::rhi::metal::kPushConstantBufferIndex
// (MetalShaderToolchain.hpp). Re-declared here so the .mm binding code reads it
// from the same internal contract header without a public-header round trip.
// Sits at [8], directly above the set range and below the vertex range.
inline constexpr std::uint32_t kMetalPushConstantBufferIndex = 8U;

// Base [[buffer(N)]] index for vertex-input (stage_in) buffers. Vertex binding
// B is bound at kVertexBufferBaseIndex + B in BOTH the MTLVertexDescriptor
// layout (MetalPipeline.mm) and the bind_vertex_buffer call (MetalCommandBuffer
// .mm). Chosen above the argument-buffer range [0..7] and the push slot [8],
// and STRICTLY BELOW the SPIRV-Cross aux floor of 20. Range [9..15] = 7 streams.
inline constexpr std::uint32_t kVertexBufferBaseIndex = 9U;

// Number of vertex-input buffer slots available [9..15].
inline constexpr std::uint32_t kMaxVertexBufferSlots = 7U;

// Base [[buffer(N)]] index SPIRV-Cross CompilerMSL's auxiliary buffers are
// pinned to (Translate.cpp translate_msl_impl). CompilerMSL's defaults already
// occupy [20..30]; pinning makes them DETERMINISTIC + provably disjoint from the
// vertex range [9..15]. The .mm side never binds host resources in [20..30].
inline constexpr std::uint32_t kSpirvCrossAuxBufferBaseIndex = 20U;

// Forward declaration — MetalCommandBufferImpl holds a non-owning pointer
// to a MetalDeviceCtx supplied by the device that created it. The concrete
// definition appears further down this header.
class MetalDeviceCtx;

// ---------------------------------------------------------------------------
// MetalSwapchainObj — CAMetalLayer-backed swapchain.
//
// Sprint-1 contract:
//   * window_handle in SwapchainDesc must be a `CAMetalLayer*` (already
//     attached to an NSView / UIView by the platform window layer).
//   * acquire_drawable() returns the current frame's drawable via
//     [layer nextDrawable]. Metal owns the drawable lifecycle; we hold one
//     reference for the lifetime of the frame.
//   * present() schedules [cmdbuf presentDrawable:].
//
// Image count is informational: Metal manages its own pool internally. We
// report `maximumDrawableCount` as the image_count.
// ---------------------------------------------------------------------------
class MetalSwapchainObj final
{
public:
    MetalSwapchainObj(CAMetalLayer* layer, id<MTLDevice> device,
                      const SwapchainDesc& desc) noexcept;
    ~MetalSwapchainObj() = default;
    MetalSwapchainObj(const MetalSwapchainObj&) = delete;
    MetalSwapchainObj& operator=(const MetalSwapchainObj&) = delete;
    MetalSwapchainObj(MetalSwapchainObj&&) = delete;
    MetalSwapchainObj& operator=(MetalSwapchainObj&&) = delete;

    // Acquire the next frame's drawable. Returns nil if the layer cannot
    // provide a drawable (off-screen, minimised, GPU stalled, OR the layer's
    // drawableSize is zero after a minimise). Caller owns the returned
    // reference under ARC; the device maps nil -> kSwapchainOutOfDate so the
    // frame loop can recreate/resize (FIX 3 — Vulkan acquire-out-of-date
    // parity).
    [[nodiscard]] id<CAMetalDrawable> acquire_drawable() noexcept;

    // FIX 3 (M7 — ADR-20260615): update the layer's drawableSize to a new
    // extent (window resize). Mirrors the Vulkan swapchain-recreate path: the
    // CAMetalLayer reallocates its drawable pool to the new size on the next
    // nextDrawable. A zero-area extent is rejected (returns false) so a
    // minimise does not push a degenerate drawableSize that would make every
    // subsequent nextDrawable return nil. Returns true when the size changed.
    [[nodiscard]] bool resize(std::uint32_t width, std::uint32_t height) noexcept;

    // Bookkeeping for the most recently acquired drawable, so present()
    // can locate it from the swapchain handle alone.
    void set_current_drawable(id<CAMetalDrawable> d) noexcept { current_ = d; }
    [[nodiscard]] id<CAMetalDrawable> current_drawable() const noexcept { return current_; }

    [[nodiscard]] CAMetalLayer* layer() const noexcept { return layer_; }
    [[nodiscard]] std::uint32_t image_count() const noexcept { return image_count_; }
    [[nodiscard]] MTLPixelFormat pixel_format() const noexcept { return pixel_format_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

private:
    CAMetalLayer*       layer_ { nil };
    id<CAMetalDrawable> current_ { nil };
    std::uint32_t       image_count_ { 0 };
    std::uint32_t       width_ { 0 };
    std::uint32_t       height_ { 0 };
    MTLPixelFormat      pixel_format_ { MTLPixelFormatBGRA8Unorm_sRGB };
};

// ---------------------------------------------------------------------------
// MetalGraphicsPipelineObj — MTLRenderPipelineState wrapper.
//
// Sprint-1 contract:
//   * The pipeline is built from an inline-MSL one-triangle vertex+fragment
//     program (see MetalPipeline.mm). Real shader-module compilation lands
//     in Sprint 2 alongside SPIRV-Cross MSL translation.
//   * Colour attachment 0 format is taken from
//     desc.color_attachment_formats[0] when provided, otherwise it
//     defaults to MTLPixelFormatBGRA8Unorm_sRGB to match the swapchain.
// ---------------------------------------------------------------------------
class MetalGraphicsPipelineObj final
{
public:
    explicit MetalGraphicsPipelineObj(id<MTLRenderPipelineState> pso) noexcept
        : pso_(pso) {}
    ~MetalGraphicsPipelineObj() = default;
    MetalGraphicsPipelineObj(const MetalGraphicsPipelineObj&) = delete;
    MetalGraphicsPipelineObj& operator=(const MetalGraphicsPipelineObj&) = delete;
    MetalGraphicsPipelineObj(MetalGraphicsPipelineObj&&) = delete;
    MetalGraphicsPipelineObj& operator=(MetalGraphicsPipelineObj&&) = delete;

    [[nodiscard]] id<MTLRenderPipelineState> pso() const noexcept { return pso_; }

private:
    id<MTLRenderPipelineState> pso_ { nil };
};

// Build a 1-triangle inline-MSL graphics pipeline (Sprint-1 only).
// Returns nil on failure (caller maps to kResourceCreationFailed).
[[nodiscard]] id<MTLRenderPipelineState>
build_sprint1_triangle_pipeline(id<MTLDevice> device, MTLPixelFormat color_format,
                                std::string* error_out) noexcept;

// ---------------------------------------------------------------------------
// build_metal_graphics_pipeline — M2 (ADR-20260615) desc-driven PSO.
//
// Builds a real MTLRenderPipelineState + MTLDepthStencilState from a
// GraphicsPipelineDesc, mirroring the Vulkan VkGraphicsPipelineCreateInfo
// translation method-by-method:
//   * vertexFunction / fragmentFunction  <- the M6-resolved MTLFunctions.
//   * vertexDescriptor (MTLVertexDescriptor) <- VertexAttribute (format /
//     offset / buffer-index) + VertexBinding (stride / step function).
//   * colorAttachments[i] pixelFormat + blend <- color_attachment_formats[i]
//     + BlendAttachmentState[i].
//   * depthAttachmentPixelFormat / stencilAttachmentPixelFormat <-
//     depth_attachment_format / stencil_attachment_format.
//   * rasterSampleCount <- samples (MSAA).
// The matching depth-stencil state object (depthCompareFunction +
// depthWriteEnabled, gated on a present depth attachment exactly like the
// Vulkan back-end) is emitted via `dss_out`. The resolved cull mode,
// front-facing winding, and primitive type are emitted so the command
// buffer can apply them on the render encoder (Metal carries these on the
// encoder, not the PSO). Returns nil PSO on failure with `error_out`
// populated.
//
// `vertex_fn` / `fragment_fn` are resolved by the caller from the bound
// shader-module registry. `fragment_fn` may be nil for a depth-only pass.
[[nodiscard]] id<MTLRenderPipelineState>
build_metal_graphics_pipeline(id<MTLDevice> device,
                              const GraphicsPipelineDesc& desc,
                              id<MTLFunction> vertex_fn,
                              id<MTLFunction> fragment_fn,
                              id<MTLDepthStencilState>* dss_out,
                              MTLPrimitiveType* primitive_out,
                              MTLCullMode* cull_out,
                              MTLWinding* winding_out,
                              std::string* error_out) noexcept;

// ---------------------------------------------------------------------------
// build_metal_mesh_pipeline — M10 (B2 — ADR-20260615) mesh-shader PSO.
//
// Builds a Metal-3 MTLMeshRenderPipelineState from a MeshPipelineDesc, the
// mesh-shader analog of build_metal_graphics_pipeline. The classic
// vertex-input assembler is replaced by an OBJECT (task) -> MESH -> FRAGMENT
// chain, so there is no MTLVertexDescriptor; instead the descriptor carries
// the resolved object/mesh/fragment MTLFunctions:
//   * objectFunction   <- `object_fn` (the GL_EXT_mesh_shader task stage;
//                          optional — a mesh-only pipeline passes nil).
//   * meshFunction     <- `mesh_fn` (required — the primitive-output stage).
//   * fragmentFunction <- `fragment_fn` (required for rasterized output).
//   * colorAttachments[i] pixelFormat + blend  <- color_attachment_formats[i]
//                                                  + BlendAttachmentState[i].
//   * depthAttachmentPixelFormat / stencil...  <- depth/stencil format.
//   * rasterSampleCount                         <- samples (MSAA).
// The matching MTLDepthStencilState + resolved cull mode / front-facing
// winding / (mesh pipelines drive the rasterizer directly, so the primitive
// type is fixed to Triangle for the encoder draw path) are emitted exactly
// like the graphics builder so the command buffer applies them on the encoder.
//
// The whole call is @available(macOS 13.0/iOS 16.0)-gated by the caller
// (MTLMeshRenderPipelineDescriptor is Metal 3); on older OSes the caller
// surfaces kNotImplemented BEFORE reaching this builder, so it can assume the
// API exists. Returns nil PSO on failure with `error_out` populated.
[[nodiscard]] id<MTLRenderPipelineState>
build_metal_mesh_pipeline(id<MTLDevice> device,
                          const MeshPipelineDesc& desc,
                          id<MTLFunction> object_fn,
                          id<MTLFunction> mesh_fn,
                          id<MTLFunction> fragment_fn,
                          id<MTLDepthStencilState>* dss_out,
                          MTLCullMode* cull_out,
                          MTLWinding* winding_out,
                          std::string* error_out) noexcept API_AVAILABLE(macos(13.0), ios(16.0));

// ---------------------------------------------------------------------------
// MetalShaderModuleObj — phase572 / Sprint-3.
//
// Holds the id<MTLLibrary> compiled from MSL source plus the id<MTLFunction>
// resolved against the ShaderModuleDesc::entry_point. The stage is recorded
// so create_compute_pipeline can validate the bound module before kicking
// off PSO creation (which would otherwise produce a confusing Metal error
// when a vertex/fragment function is mis-bound to a compute pipeline).
//
// ShaderModuleDesc::code is treated as a UTF-8 MSL source string of length
// code_size in Sprint-3; SPIRV-Cross MSL translation lands when the engine
// gets a unified shader pipeline. The Vulkan back-end's SPIR-V byte-code
// path is unaffected.
// ---------------------------------------------------------------------------
class MetalShaderModuleObj final
{
public:
    // M6 (ADR-20260615): a compute module carries its reflected
    // threads-per-threadgroup (GLSL local_size_x/y/z, surfaced host-side via
    // MslArtifact::workgroup). create_compute_pipeline reads it and stamps it
    // onto the MetalComputePipelineObj so dispatch() uses the real threadgroup
    // size. Non-compute / raw-MSL modules default to (1,1,1) — harmless, since
    // they never feed a compute pipeline.
    MetalShaderModuleObj(id<MTLLibrary> lib, id<MTLFunction> fn, ShaderStage stage,
                         MTLSize workgroup = MTLSizeMake(1, 1, 1)) noexcept
        : lib_(lib), fn_(fn), stage_(stage), workgroup_(workgroup) {}
    ~MetalShaderModuleObj() = default;
    MetalShaderModuleObj(const MetalShaderModuleObj&) = delete;
    MetalShaderModuleObj& operator=(const MetalShaderModuleObj&) = delete;
    MetalShaderModuleObj(MetalShaderModuleObj&&) = delete;
    MetalShaderModuleObj& operator=(MetalShaderModuleObj&&) = delete;

    [[nodiscard]] id<MTLLibrary>  lib() const noexcept { return lib_; }
    [[nodiscard]] id<MTLFunction> fn() const noexcept { return fn_; }
    [[nodiscard]] ShaderStage     stage() const noexcept { return stage_; }
    // M6: reflected threads-per-threadgroup for a compute module.
    [[nodiscard]] MTLSize workgroup() const noexcept { return workgroup_; }

private:
    id<MTLLibrary>  lib_ { nil };
    id<MTLFunction> fn_ { nil };
    ShaderStage     stage_ { ShaderStage::kNone };
    MTLSize         workgroup_ { 1, 1, 1 };
};

// Compile MSL source into an MTLLibrary and resolve the named entry-point
// function. Sprint-3 contract: desc.code is UTF-8 MSL source of length
// desc.code_size; desc.entry_point selects the function inside the library
// (defaults to "main" per ShaderModuleDesc).
[[nodiscard]] id<MTLFunction>
build_metal_shader_function(id<MTLDevice> device,
                            const ShaderModuleDesc& desc,
                            id<MTLLibrary>* lib_out,
                            std::string* error_out) noexcept;

// ---------------------------------------------------------------------------
// MetalComputePipelineObj — phase572 / Sprint-3, M6 (ADR-20260615) extension.
//
// Wraps id<MTLComputePipelineState> built via
// [device newComputePipelineStateWithFunction:error:]. The PSO is opaque
// once created; the cmd-buffer binds it via setComputePipelineState: on
// the active MTLComputeCommandEncoder.
//
// M6: the PSO ALSO carries the threads-per-threadgroup MTLSize reflected from
// the compute shader's GLSL layout(local_size_*) (surfaced host-side through
// MslArtifact::workgroup and captured here at create_compute_pipeline time).
// dispatch() needs it because Metal's
// [encoder dispatchThreadgroups:threadsPerThreadgroup:] takes BOTH the group
// count (the engine dispatch(x,y,z) argument — matches Vulkan vkCmdDispatch /
// D3D12 Dispatch group semantics) AND the threads-per-group. Without it the
// Sprint-3 baseline hardcoded (1,1,1), running 1 thread per group — a
// local_size>1 shader was under-dispatched by the product of its local size.
// ---------------------------------------------------------------------------
class MetalComputePipelineObj final
{
public:
    MetalComputePipelineObj(id<MTLComputePipelineState> pso,
                            MTLSize threads_per_threadgroup) noexcept
        : pso_(pso), threads_per_threadgroup_(threads_per_threadgroup) {}
    ~MetalComputePipelineObj() = default;
    MetalComputePipelineObj(const MetalComputePipelineObj&) = delete;
    MetalComputePipelineObj& operator=(const MetalComputePipelineObj&) = delete;
    MetalComputePipelineObj(MetalComputePipelineObj&&) = delete;
    MetalComputePipelineObj& operator=(MetalComputePipelineObj&&) = delete;

    [[nodiscard]] id<MTLComputePipelineState> pso() const noexcept { return pso_; }

    // M6: reflected threads-per-threadgroup (GLSL local_size_x/y/z). Consumed
    // by bind_compute_pipeline -> cached on the cmd buffer -> applied by
    // dispatch().
    [[nodiscard]] MTLSize threads_per_threadgroup() const noexcept
    {
        return threads_per_threadgroup_;
    }

private:
    id<MTLComputePipelineState> pso_ { nil };
    MTLSize                     threads_per_threadgroup_ { 1, 1, 1 };
};

// ---------------------------------------------------------------------------
// MetalFenceObj — phase615 / Sprint-4.
//
// CHROMODYNAMIC's FenceHandle contract is a CPU-side completion signal that
// the queue raises when an associated submit finishes. Metal does not expose
// a 1:1 equivalent at the device-fence layer (MTLFence is a GPU-internal
// barrier primitive — different shape entirely). The SOTA Apple pattern is
// a dispatch_semaphore_t signaled from -[MTLCommandBuffer addCompletedHandler:]
// and waited on from the CPU via dispatch_semaphore_wait. The signaled flag
// is a manual counter so create_fence(signaled = true) starts at 1 (waitable
// without any submit), matching VkFence's VK_FENCE_CREATE_SIGNALED_BIT and
// the NullDevice headless contract.
//
// reset_fence drains any outstanding signals so the next wait_for_fence
// blocks again — same semantics as vkResetFences. is_fence_signaled does a
// non-blocking poll (timeout = DISPATCH_TIME_NOW). Concurrent submits that
// share the same fence increment a pending counter; each completion handler
// decrements it back to zero before signalling. This matches the Vulkan
// "one-submit-per-fence" idiomatic usage while tolerating accidental reuse.
// ---------------------------------------------------------------------------
class MetalFenceObj final
{
public:
    explicit MetalFenceObj(bool signaled) noexcept;
    ~MetalFenceObj();
    MetalFenceObj(const MetalFenceObj&) = delete;
    MetalFenceObj& operator=(const MetalFenceObj&) = delete;
    MetalFenceObj(MetalFenceObj&&) = delete;
    MetalFenceObj& operator=(MetalFenceObj&&) = delete;

    // Block the calling thread until the dispatch semaphore is signalled or
    // the timeout (nanoseconds) elapses. UINT64_MAX maps to DISPATCH_TIME_FOREVER.
    // Returns true on signal, false on timeout.
    [[nodiscard]] bool wait(std::uint64_t timeout_ns) noexcept;

    // Non-blocking poll. Returns true if the fence is already signalled
    // (drains one signal in the process — matches vkGetFenceStatus +
    // vkWaitForFences with a zero timeout). Re-armed via the next submit
    // completion handler.
    [[nodiscard]] bool poll() noexcept;

    // Discard any outstanding signal so the next wait blocks again.
    void reset() noexcept;

    // Called from the submit completion handler. Releases one waiter.
    void signal_from_completion() noexcept;

    [[nodiscard]] dispatch_semaphore_t sem() const noexcept { return sem_; }

private:
    dispatch_semaphore_t sem_ { nullptr };
};

// ---------------------------------------------------------------------------
// MetalEventObj — phase615 / Sprint-4.
//
// CHROMODYNAMIC's SemaphoreHandle is a queue-to-queue (GPU-side) sync
// primitive; the SOTA Metal mapping is id<MTLSharedEvent>. SubmitDesc
// wait_semaphores and signal_semaphores hand off via
// -[MTLCommandBuffer encodeWaitForEvent:value:] +
// -[MTLCommandBuffer encodeSignalEvent:value:] on the queue's command buffer.
//
// Each MetalEventObj keeps an internal counter so the wire-up (which only
// passes a SemaphoreHandle, no value) maps to an ever-incrementing
// "submitted" tag in a way that matches the Vulkan binary-semaphore
// contract (each signal must be paired with exactly one wait before the
// next signal). The counter is incremented on every signal-encode call.
// ---------------------------------------------------------------------------
class MetalEventObj final
{
public:
    explicit MetalEventObj(id<MTLSharedEvent> event) noexcept
        : event_(event) {}
    ~MetalEventObj() = default;
    MetalEventObj(const MetalEventObj&) = delete;
    MetalEventObj& operator=(const MetalEventObj&) = delete;
    MetalEventObj(MetalEventObj&&) = delete;
    MetalEventObj& operator=(MetalEventObj&&) = delete;

    [[nodiscard]] id<MTLSharedEvent> event() const noexcept { return event_; }

    // Bump and return the next signal value (caller passes it to
    // encodeSignalEvent:value:). Symmetric with next_wait_value below.
    //
    // M-EVENT-ATOMIC: next_signal_value() is called from submit() on
    // arbitrary submitting threads (the engine submits from worker threads),
    // so the counter must be incremented atomically. fetch_add returns the
    // PRE-increment value, so +1 reproduces the prior `++counter` semantics
    // (the first call yields 1, matching the old pre-increment). relaxed
    // ordering suffices: the only invariant is monotonic uniqueness of the
    // returned value, not ordering against other memory.
    [[nodiscard]] std::uint64_t next_signal_value() noexcept
    {
        return signal_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    // Return the highest value that has been encoded for signalling so far.
    // The matching wait_semaphores entry needs to block until that value
    // is reached (the producer queue raises it; the consumer waits on it).
    [[nodiscard]] std::uint64_t current_signal_value() const noexcept
    {
        return signal_counter_.load(std::memory_order_relaxed);
    }

private:
    id<MTLSharedEvent>         event_ { nil };
    std::atomic<std::uint64_t> signal_counter_ { 0 };
};

// ---------------------------------------------------------------------------
// MetalPipelineLayoutObj — phase615 / Sprint-4.
//
// Metal does not have an explicit pipeline-layout object; the binding
// topology lives inside the MTLRenderPipelineState / MTLComputePipelineState
// via argument indices and -[MTLArgumentEncoder] tables. We still need a
// handle to satisfy the IDevice / GraphicsPipelineDesc::layout contract.
// The owned descriptor metadata (set_layouts + push_constant_ranges) is
// preserved so the future argument-buffer emission in Sprint 5 can read
// it back to size MTLArgumentEncoders. Sprint-4 stores it as a flat copy.
// ---------------------------------------------------------------------------
class MetalPipelineLayoutObj final
{
public:
    MetalPipelineLayoutObj() noexcept = default;
    ~MetalPipelineLayoutObj() = default;
    MetalPipelineLayoutObj(const MetalPipelineLayoutObj&) = delete;
    MetalPipelineLayoutObj& operator=(const MetalPipelineLayoutObj&) = delete;
    MetalPipelineLayoutObj(MetalPipelineLayoutObj&&) = delete;
    MetalPipelineLayoutObj& operator=(MetalPipelineLayoutObj&&) = delete;

    void add_set_layout(DescriptorSetLayoutHandle h) { set_layouts_.push_back(h); }
    void add_push_constant_range(PushConstantRange r) { push_constants_.push_back(r); }

    [[nodiscard]] std::size_t set_layout_count() const noexcept { return set_layouts_.size(); }
    [[nodiscard]] std::size_t push_constant_range_count() const noexcept { return push_constants_.size(); }

private:
    // Flat owned copies — Sprint-5 argument-buffer emission reads these.
    std::vector<DescriptorSetLayoutHandle> set_layouts_;
    std::vector<PushConstantRange>         push_constants_;
};

// ---------------------------------------------------------------------------
// MetalDescriptorSetLayoutObj — phase615 / Sprint-4.
//
// Metal expresses descriptor sets via MTLArgumentEncoder + an argument
// buffer (id<MTLBuffer> with [[argument_buffer]] qualified MSL). The actual
// encoder is created at allocate_descriptor_set time in Sprint 5. For
// Sprint-4 we record the binding table here so create_pipeline_layout +
// allocate_descriptor_set can resolve set indices to descriptor counts
// when the time comes. Storage is intentionally flat; argument-buffer
// layout decisions are deferred.
// ---------------------------------------------------------------------------
class MetalDescriptorSetLayoutObj final
{
public:
    MetalDescriptorSetLayoutObj() noexcept = default;
    ~MetalDescriptorSetLayoutObj() = default;
    MetalDescriptorSetLayoutObj(const MetalDescriptorSetLayoutObj&) = delete;
    MetalDescriptorSetLayoutObj& operator=(const MetalDescriptorSetLayoutObj&) = delete;
    MetalDescriptorSetLayoutObj(MetalDescriptorSetLayoutObj&&) = delete;
    MetalDescriptorSetLayoutObj& operator=(MetalDescriptorSetLayoutObj&&) = delete;

    void add_binding(const DescriptorSetLayoutBinding& b) { bindings_.push_back(b); }

    [[nodiscard]] std::size_t binding_count() const noexcept { return bindings_.size(); }

    // M4 (ADR-20260615): expose the binding table so allocate_descriptor_set
    // can build a per-binding [MTLArgumentDescriptor] array (index = binding,
    // dataType from DescriptorType) instead of the Sprint-5 single-slot
    // encoder. Honours the M3 set-per-argument-buffer contract.
    [[nodiscard]] const std::vector<DescriptorSetLayoutBinding>&
    bindings() const noexcept { return bindings_; }

private:
    std::vector<DescriptorSetLayoutBinding> bindings_;
};

// ---------------------------------------------------------------------------
// MetalBufferObj — id<MTLBuffer> wrapper (M1 — ADR-20260615 registry).
//
// Backs a cd::rhi::BufferHandle with a real MTLBuffer created via
// [device newBufferWithLength:options:]. The storage mode is chosen from
// BufferDesc::memory (kGpuOnly -> Private; host-visible variants -> Shared)
// and cached so upload_buffer / download_buffer can validate host
// visibility without re-querying. The original cd::rhi::MemoryUsage is kept
// too so the device can mirror the Vulkan back-end's host_visible
// precondition logic exactly.
// ---------------------------------------------------------------------------
class MetalBufferObj final
{
public:
    MetalBufferObj(id<MTLBuffer> buffer, MemoryUsage memory) noexcept
        : buffer_(buffer), memory_(memory) {}
    ~MetalBufferObj() = default;
    MetalBufferObj(const MetalBufferObj&) = delete;
    MetalBufferObj& operator=(const MetalBufferObj&) = delete;
    MetalBufferObj(MetalBufferObj&&) = delete;
    MetalBufferObj& operator=(MetalBufferObj&&) = delete;

    [[nodiscard]] id<MTLBuffer> buffer() const noexcept { return buffer_; }
    [[nodiscard]] MemoryUsage   memory() const noexcept { return memory_; }

private:
    id<MTLBuffer> buffer_ { nil };
    MemoryUsage   memory_ { MemoryUsage::kAuto };
};

// ---------------------------------------------------------------------------
// MetalTextureObj — id<MTLTexture> wrapper (M1 — ADR-20260615 registry).
//
// Backs a cd::rhi::TextureHandle with a real MTLTexture created via
// [device newTextureWithDescriptor:]. The originating cd::rhi::Format is
// cached so texture-view resolution + copy-region row-stride derivation can
// read it back without round-tripping through MTLPixelFormat.
// ---------------------------------------------------------------------------
class MetalTextureObj final
{
public:
    MetalTextureObj(id<MTLTexture> texture, Format format) noexcept
        : texture_(texture), format_(format) {}
    ~MetalTextureObj() = default;
    MetalTextureObj(const MetalTextureObj&) = delete;
    MetalTextureObj& operator=(const MetalTextureObj&) = delete;
    MetalTextureObj(MetalTextureObj&&) = delete;
    MetalTextureObj& operator=(MetalTextureObj&&) = delete;

    [[nodiscard]] id<MTLTexture> texture() const noexcept { return texture_; }
    [[nodiscard]] Format         format() const noexcept { return format_; }

private:
    id<MTLTexture> texture_ { nil };
    Format         format_ { Format::kUndefined };
};

// ---------------------------------------------------------------------------
// MetalDepthStencilStateObj — id<MTLDepthStencilState> wrapper (M2 —
// ADR-20260615 pipeline). Metal carries depth-test / depth-write / compare
// in a SEPARATE state object from the render-pipeline-state (unlike Vulkan
// which folds VkPipelineDepthStencilStateCreateInfo into the PSO). The
// graphics-pipeline registry therefore owns BOTH the MTLRenderPipelineState
// and the matching MTLDepthStencilState; the command buffer binds the
// depth-stencil state with [encoder setDepthStencilState:] right after
// [encoder setRenderPipelineState:]. We also carry the resolved cull mode /
// winding / primitive type so the draw path can apply them per the bound
// pipeline (Metal sets these on the encoder, not the PSO).
// ---------------------------------------------------------------------------
class MetalGraphicsPipelineStateObj final
{
public:
    MetalGraphicsPipelineStateObj(id<MTLRenderPipelineState> pso,
                                  id<MTLDepthStencilState> dss,
                                  MTLPrimitiveType primitive,
                                  MTLCullMode cull,
                                  MTLWinding winding) noexcept
        : pso_(pso)
        , dss_(dss)
        , primitive_(primitive)
        , cull_(cull)
        , winding_(winding) {}
    ~MetalGraphicsPipelineStateObj() = default;
    MetalGraphicsPipelineStateObj(const MetalGraphicsPipelineStateObj&) = delete;
    MetalGraphicsPipelineStateObj& operator=(const MetalGraphicsPipelineStateObj&) = delete;
    MetalGraphicsPipelineStateObj(MetalGraphicsPipelineStateObj&&) = delete;
    MetalGraphicsPipelineStateObj& operator=(MetalGraphicsPipelineStateObj&&) = delete;

    [[nodiscard]] id<MTLRenderPipelineState> pso() const noexcept { return pso_; }
    [[nodiscard]] id<MTLDepthStencilState>   dss() const noexcept { return dss_; }
    [[nodiscard]] MTLPrimitiveType primitive() const noexcept { return primitive_; }
    [[nodiscard]] MTLCullMode      cull() const noexcept { return cull_; }
    [[nodiscard]] MTLWinding       winding() const noexcept { return winding_; }

    // M10 (B2 — ADR-20260615): for a MESH-shader pipeline, the threads per
    // object (task) threadgroup + threads per mesh threadgroup, reflected from
    // the GLSL layout(local_size_*) of each stage (surfaced host-side via
    // MslArtifact::workgroup, captured at create_mesh_pipeline time). The Metal
    // [renderEncoder drawMeshThreadgroups:threadsPerObjectThreadgroup:
    // threadsPerMeshThreadgroup:] call needs BOTH (the group COUNT comes from
    // draw_mesh_tasks args; the threads-per-group from the shader). A classic
    // graphics pipeline leaves these at (1,1,1) — harmless, draw_mesh_tasks is
    // never issued against one. is_mesh marks whether this record is a mesh PSO
    // (so draw_mesh_tasks can refuse to drive a non-mesh pipeline).
    void set_mesh_threadgroups(MTLSize object_tg, MTLSize mesh_tg) noexcept
    {
        is_mesh_ = true;
        object_threads_per_threadgroup_ = object_tg;
        mesh_threads_per_threadgroup_ = mesh_tg;
    }
    [[nodiscard]] bool    is_mesh() const noexcept { return is_mesh_; }
    [[nodiscard]] MTLSize object_threads_per_threadgroup() const noexcept
    {
        return object_threads_per_threadgroup_;
    }
    [[nodiscard]] MTLSize mesh_threads_per_threadgroup() const noexcept
    {
        return mesh_threads_per_threadgroup_;
    }

private:
    id<MTLRenderPipelineState> pso_ { nil };
    id<MTLDepthStencilState>   dss_ { nil };
    MTLPrimitiveType           primitive_ { MTLPrimitiveTypeTriangle };
    MTLCullMode                cull_ { MTLCullModeNone };
    MTLWinding                 winding_ { MTLWindingClockwise };
    bool                       is_mesh_ { false };
    MTLSize                    object_threads_per_threadgroup_ { 1, 1, 1 };
    MTLSize                    mesh_threads_per_threadgroup_ { 1, 1, 1 };
};

// ---------------------------------------------------------------------------
// MetalSamplerObj — id<MTLSamplerState> wrapper (Sprint-2 / phase559).
//
// Backed by a single MTLSamplerState created on device-side via
// [device newSamplerStateWithDescriptor:]. The descriptor is built from the
// cd::rhi::SamplerDesc fields by the device factory; we keep only the
// resolved state here so the cmd-buffer descriptor / argument-buffer paths
// can look it up by SamplerHandle in constant time.
// ---------------------------------------------------------------------------
class MetalSamplerObj final
{
public:
    explicit MetalSamplerObj(id<MTLSamplerState> state) noexcept
        : state_(state) {}
    ~MetalSamplerObj() = default;
    MetalSamplerObj(const MetalSamplerObj&) = delete;
    MetalSamplerObj& operator=(const MetalSamplerObj&) = delete;
    MetalSamplerObj(MetalSamplerObj&&) = delete;
    MetalSamplerObj& operator=(MetalSamplerObj&&) = delete;

    [[nodiscard]] id<MTLSamplerState> state() const noexcept { return state_; }

private:
    id<MTLSamplerState> state_ { nil };
};

// Translate a cd::rhi::SamplerDesc to an MTLSamplerDescriptor + create the
// MTLSamplerState. Returns nil on failure. SOTA mapping for the address
// modes / filters / compare op / anisotropy / lod range / border colour;
// matches the Vulkan back-end's behaviour for the parity test suite.
[[nodiscard]] id<MTLSamplerState>
build_metal_sampler(id<MTLDevice> device, const SamplerDesc& desc,
                    std::string* error_out) noexcept;

// ---------------------------------------------------------------------------
// MetalTextureViewObj — phase649 / Sprint-5.
//
// CHROMODYNAMIC's TextureViewHandle is the subresource view abstraction: a
// (texture, format-override, mip range, layer range, type) tuple. The SOTA
// Metal expression is -[id<MTLTexture> newTextureViewWithPixelFormat:
// textureType:levels:slices:] which lets you re-interpret the underlying
// pixel store as a different format / subset. We materialise the view
// lazily on lookup so that an unbacked source TextureHandle (the Sprint-2
// stub-handle baseline) results in `nil` rather than a hard error — same
// graceful-skip pattern that the buffer / texture registries already use.
//
// When the descriptor matches the parent texture's full extent + format
// exactly we return the parent MTLTexture directly without allocating a
// view (Metal validation rejects newTextureViewWith* in that case as a
// no-op anyway).
// ---------------------------------------------------------------------------
class MetalTextureViewObj final
{
public:
    explicit MetalTextureViewObj(const TextureViewDesc& desc) noexcept
        : desc_(desc) {}
    ~MetalTextureViewObj() = default;
    MetalTextureViewObj(const MetalTextureViewObj&) = delete;
    MetalTextureViewObj& operator=(const MetalTextureViewObj&) = delete;
    MetalTextureViewObj(MetalTextureViewObj&&) = delete;
    MetalTextureViewObj& operator=(MetalTextureViewObj&&) = delete;

    [[nodiscard]] const TextureViewDesc& desc() const noexcept { return desc_; }

    // Resolve the live MTLTexture for this view. `parent` is the parent
    // texture's MTLTexture (looked up via MetalDeviceCtx::lookup_texture);
    // when nil the view also resolves to nil. The cached view is created
    // on first call so repeated lookups are O(1).
    [[nodiscard]] id<MTLTexture> resolve(id<MTLTexture> parent) noexcept;

private:
    TextureViewDesc desc_;
    id<MTLTexture>  view_ { nil };
};

// ---------------------------------------------------------------------------
// MetalDescriptorSetObj — phase649 / Sprint-5.
//
// Metal's SOTA equivalent of VkDescriptorSet is the argument-buffer model:
// an MTLArgumentEncoder describes the binding table, and an MTLBuffer
// stores the encoded handles + offsets. Sprint-5 wires up the allocation
// path so descriptor-set handles map to a real (encoder, buffer) pair.
// Layouts with zero bindings are legal — the engine occasionally uses
// them as a "no resources" marker (Vulkan VkDescriptorSetLayout parity);
// we still allocate an empty argument buffer so the cmd-buffer
// bind_descriptor_set path can lookup the handle and treat it as a no-op
// without an extra null check.
// ---------------------------------------------------------------------------
class MetalDescriptorSetObj final
{
public:
    MetalDescriptorSetObj(id<MTLArgumentEncoder> encoder,
                          id<MTLBuffer> arg_buffer) noexcept
        : encoder_(encoder)
        , arg_buffer_(arg_buffer)
        , resident_buffers_([[NSMutableArray alloc] init])
        , resident_textures_([[NSMutableArray alloc] init])
        , resident_accels_([[NSMutableArray alloc] init]) {}
    ~MetalDescriptorSetObj() = default;
    MetalDescriptorSetObj(const MetalDescriptorSetObj&) = delete;
    MetalDescriptorSetObj& operator=(const MetalDescriptorSetObj&) = delete;
    MetalDescriptorSetObj(MetalDescriptorSetObj&&) = delete;
    MetalDescriptorSetObj& operator=(MetalDescriptorSetObj&&) = delete;

    [[nodiscard]] id<MTLArgumentEncoder> encoder() const noexcept { return encoder_; }
    [[nodiscard]] id<MTLBuffer>          arg_buffer() const noexcept { return arg_buffer_; }

    // M4 (ADR-20260615): every resource an argument buffer REFERENCES must be
    // made resident with [encoder useResource:usage:] before a draw, otherwise
    // the GPU cannot fault it in (the W8-BE cross-encoder visibility rule,
    // ADR-20260530 §8.5.1). update_descriptor_set records the referenced
    // MTLBuffer / MTLTexture here; bind_descriptor_set replays them on the
    // active encoder. We reset the lists at the start of each update so a
    // re-write does not accumulate stale residents.
    void reset_residents() noexcept
    {
        [resident_buffers_ removeAllObjects];
        [resident_textures_ removeAllObjects];
        [resident_accels_ removeAllObjects];
    }
    void add_resident_buffer(id<MTLBuffer> b)
    {
        if (b != nil) { [resident_buffers_ addObject:b]; }
    }
    void add_resident_texture(id<MTLTexture> t)
    {
        if (t != nil) { [resident_textures_ addObject:t]; }
    }
    // M9 (ADR-20260615): an MTLAccelerationStructure referenced through an
    // argument buffer (the ray-query TLAS binding) MUST be made resident with
    // [encoder useResource:usage:Read] before the ray-query draw, exactly like
    // the buffers / textures above — otherwise the GPU cannot fault the TLAS in
    // and the rayQueryEXT walk reads garbage. bind_descriptor_set replays this
    // list on the active encoder alongside the buffer / texture residents.
    void add_resident_accel(id<MTLAccelerationStructure> a)
    {
        if (a != nil) { [resident_accels_ addObject:a]; }
    }
    [[nodiscard]] NSArray<id<MTLBuffer>>* resident_buffers() const noexcept
    {
        return resident_buffers_;
    }
    [[nodiscard]] NSArray<id<MTLTexture>>* resident_textures() const noexcept
    {
        return resident_textures_;
    }
    [[nodiscard]] NSArray<id<MTLAccelerationStructure>>*
    resident_accels() const noexcept
    {
        return resident_accels_;
    }

private:
    id<MTLArgumentEncoder>           encoder_ { nil };
    id<MTLBuffer>                    arg_buffer_ { nil };
    NSMutableArray<id<MTLBuffer>>*   resident_buffers_ { nil };
    NSMutableArray<id<MTLTexture>>*  resident_textures_ { nil };
    NSMutableArray<id<MTLAccelerationStructure>>* resident_accels_ { nil };
};

// ---------------------------------------------------------------------------
// MetalBindlessArrayObj — M11 (B2 — ADR-20260615) bindless texture array.
//
// CHROMODYNAMIC's BindlessTextureArrayHandle is a runtime-indexed sampler2D
// array (the chrome-Sponza ray-side texture-sampling path, ADR W8-BE). The SOTA
// Metal expression is a SINGLE argument buffer holding an unbounded texture
// array: an MTLArgumentEncoder built from one MTLArgumentDescriptor whose
// dataType=MTLDataTypeTexture + arrayLength=slot_count, backed by an MTLBuffer
// sized to [encoder encodedLength]. write_bindless_texture_slot encodes a
// texture at [[id(slot)]]; bind_bindless_texture_array binds the arg buffer at
// the DEDICATED bindless set slot (its own descriptor set, per Memory rule 9 —
// bindless lives on its own set, not the shared per-prim set) and makes every
// POPULATED slot's texture resident (the M9-residency lesson: an argument-buffer
// resource not made resident is faulted as garbage).
//
// The bound sampler is per-array (BindlessTextureArrayDesc::sampler, one sampler
// for all slots in v1 — per-slot sampler variation is not supported); the .mm
// device looks it up and the shader's static sampler/`constexpr sampler`
// (SPIRV-Cross splits combined image+sampler) covers the sample. We retain the
// SamplerHandle so a future per-array sampler bind can read it.
//
// Residency list: write_bindless_texture_slot records each populated slot's
// id<MTLTexture> in `populated_` (a dictionary keyed by slot so a re-write of
// the same slot replaces, not duplicates). bind_bindless_texture_array replays
// `populated_.allValues` via [encoder useResource:usage:Read stages:...].
// ---------------------------------------------------------------------------
class MetalBindlessArrayObj final
{
public:
    MetalBindlessArrayObj(id<MTLArgumentEncoder> encoder,
                          id<MTLBuffer> arg_buffer,
                          std::uint32_t slot_count,
                          SamplerHandle sampler) noexcept
        : encoder_(encoder)
        , arg_buffer_(arg_buffer)
        , slot_count_(slot_count)
        , sampler_(sampler)
        , populated_([[NSMutableDictionary alloc] init]) {}
    ~MetalBindlessArrayObj() = default;
    MetalBindlessArrayObj(const MetalBindlessArrayObj&) = delete;
    MetalBindlessArrayObj& operator=(const MetalBindlessArrayObj&) = delete;
    MetalBindlessArrayObj(MetalBindlessArrayObj&&) = delete;
    MetalBindlessArrayObj& operator=(MetalBindlessArrayObj&&) = delete;

    [[nodiscard]] id<MTLArgumentEncoder> encoder() const noexcept { return encoder_; }
    [[nodiscard]] id<MTLBuffer>          arg_buffer() const noexcept { return arg_buffer_; }
    [[nodiscard]] std::uint32_t          slot_count() const noexcept { return slot_count_; }
    [[nodiscard]] SamplerHandle          sampler() const noexcept { return sampler_; }

    // Record the texture that now occupies `slot` for residency replay. The
    // per-slot dictionary (slot -> texture) is the source of truth so a re-write
    // of the SAME slot REPLACES rather than appends — bind_bindless_texture_array
    // makes each value resident exactly once. NSMutableDictionary matches the
    // NSMutableArray residency idiom used by MetalDescriptorSetObj (ARC-managed).
    void record_slot(std::uint32_t slot, id<MTLTexture> tex)
    {
        if (slot >= slot_count_ || tex == nil)
        {
            return;
        }
        populated_[@(slot)] = tex;
    }
    // The unique populated textures, for the useResource residency replay.
    [[nodiscard]] NSArray<id<MTLTexture>>* resident_textures() const noexcept
    {
        return [populated_ allValues];
    }

private:
    id<MTLArgumentEncoder>          encoder_ { nil };
    id<MTLBuffer>                   arg_buffer_ { nil };
    std::uint32_t                   slot_count_ { 0 };
    SamplerHandle                   sampler_ {};
    // slot -> id<MTLTexture> for every populated slot. allValues feeds the
    // residency replay (each texture made resident once).
    NSMutableDictionary<NSNumber*, id<MTLTexture>>* populated_ { nil };
};

// ---------------------------------------------------------------------------
// MetalTimelineObj — phase649 / Sprint-5.
//
// id<MTLSharedEvent> doubles as Vulkan-style timeline semaphore: the
// monotonic `signaledValue` property maps 1:1 onto VkSemaphoreType
// VK_SEMAPHORE_TYPE_TIMELINE. Host wait uses
// -[id<MTLSharedEvent> notifyListener:atValue:block:] feeding a
// dispatch_semaphore_t so the IDevice timeout contract still holds
// (DISPATCH_TIME_FOREVER for UINT64_MAX, otherwise nanosecond deadline).
// Host signal pokes `signaledValue` directly; we keep a guard so
// non-monotonic writes (value <= current) surface as kInvalidArgument
// from the device factory rather than tripping a Metal validation assert.
//
// Submit-side encode-wait / encode-signal hand-offs are issued by
// MetalDevice::submit(SubmitDesc) against the cached MTLSharedEvent — the
// matching call sites pass an explicit `value`, so we don't bump the
// counter here.
// ---------------------------------------------------------------------------
class MetalTimelineObj final
{
public:
    MetalTimelineObj(id<MTLSharedEvent> event, std::uint64_t initial_value) noexcept;
    ~MetalTimelineObj();
    MetalTimelineObj(const MetalTimelineObj&) = delete;
    MetalTimelineObj& operator=(const MetalTimelineObj&) = delete;
    MetalTimelineObj(MetalTimelineObj&&) = delete;
    MetalTimelineObj& operator=(MetalTimelineObj&&) = delete;

    [[nodiscard]] id<MTLSharedEvent> event() const noexcept { return event_; }

    // Read the current signalled value. Reads `signaledValue` directly so
    // host signal_timeline_semaphore + queue-side encodeSignalEvent both
    // converge on the same source of truth.
    [[nodiscard]] std::uint64_t value() const noexcept;

    // Bump `signaledValue` to `value`. Monotonic — the caller must ensure
    // value > current; the device factory enforces this and returns
    // kInvalidArgument when violated.
    void signal(std::uint64_t value) noexcept;

    // Block the calling thread until `signaledValue >= value` or the
    // timeout elapses. Returns true on signal, false on timeout. Uses
    // -[id<MTLSharedEvent> notifyListener:atValue:block:] internally with
    // a dispatch_semaphore_t to honour the IDevice timeout contract.
    [[nodiscard]] bool wait(std::uint64_t value, std::uint64_t timeout_ns) noexcept;

private:
    id<MTLSharedEvent> event_ { nil };
    // Notifier listener queue — shared across all wait() calls on this
    // timeline so we don't allocate a fresh dispatch queue per wait.
    dispatch_queue_t listener_queue_ { nullptr };
};

// ---------------------------------------------------------------------------
// MetalAccelObj — id<MTLAccelerationStructure> wrapper (M9 — ADR-20260615).
//
// Backs a cd::rhi::AccelStructureHandle with a real MTLAccelerationStructure.
// The descriptor (MTLPrimitiveAccelerationStructureDescriptor for a BLAS,
// MTLInstanceAccelerationStructureDescriptor for a TLAS) is retained so the
// command-buffer-side build (buildAccelerationStructure:descriptor:scratch
// Buffer:scratchBufferOffset:) can run without re-deriving geometry. The
// scratch buffer is sized from [device accelerationStructureSizesWithDescriptor:]
// .buildScratchBufferSize and kept alive for the rebuild path. `kind`
// distinguishes BLAS vs TLAS so the command buffer can pick the right barrier
// scope. This mirrors the Vulkan VkAccelerationStructureKHR + the
// vkCmdBuildAccelerationStructures ray-query path (NOT the SBT pipeline path,
// which stays kNotImplemented on Metal exactly as it is on Vulkan).
// ---------------------------------------------------------------------------
class MetalAccelObj final
{
public:
    MetalAccelObj(id<MTLAccelerationStructure> as,
                  MTLAccelerationStructureDescriptor* descriptor,
                  id<MTLBuffer> scratch,
                  AccelStructureKind kind,
                  AccelBuildFlags build_flags) noexcept
        : as_(as), descriptor_(descriptor), scratch_(scratch), kind_(kind),
          build_flags_(build_flags) {}
    ~MetalAccelObj() = default;
    MetalAccelObj(const MetalAccelObj&) = delete;
    MetalAccelObj& operator=(const MetalAccelObj&) = delete;
    MetalAccelObj(MetalAccelObj&&) = delete;
    MetalAccelObj& operator=(MetalAccelObj&&) = delete;

    [[nodiscard]] id<MTLAccelerationStructure> as() const noexcept { return as_; }
    [[nodiscard]] MTLAccelerationStructureDescriptor* descriptor() const noexcept
    {
        return descriptor_;
    }
    [[nodiscard]] id<MTLBuffer> scratch() const noexcept { return scratch_; }
    [[nodiscard]] AccelStructureKind kind() const noexcept { return kind_; }
    // A-AS-FLAGS / A-REFIT (Backend-to-100 Wave 3b): the build flags this AS
    // was created with. refit_acceleration_structure picks
    // refitAccelerationStructure: only when kAllowUpdate is set; otherwise it
    // falls back to a full buildAccelerationStructure:.
    [[nodiscard]] AccelBuildFlags build_flags() const noexcept { return build_flags_; }
    // A-COMPACTION: the device-reported result size, set at create time and
    // overwritten by the compacted size on a compacted copy.
    [[nodiscard]] std::uint64_t as_size() const noexcept { return as_size_; }
    void set_as_size(std::uint64_t s) noexcept { as_size_ = s; }

private:
    id<MTLAccelerationStructure>        as_ { nil };
    MTLAccelerationStructureDescriptor* descriptor_ { nil };
    id<MTLBuffer>                       scratch_ { nil };
    AccelStructureKind                  kind_ { AccelStructureKind::kBottomLevel };
    AccelBuildFlags                     build_flags_ { AccelBuildFlags::kPreferFastTrace };
    std::uint64_t                       as_size_ { 0 };
};

// ---------------------------------------------------------------------------
// MetalQueryPoolObj — MTLCounterSampleBuffer wrapper (A-QUERY, Backend-to-100
// Wave 3a, gated-off / structural).
//
// Backs a cd::rhi::QueryPoolHandle for the kTimestamp case with an
// MTLCounterSampleBuffer created against the device's MTLCommonCounterSet
// "timestamp" counter set. The command buffer samples the GPU timestamp at a
// slot via [encoder sampleCountersInBuffer:atSampleIndex:withBarrier:] (or
// [cmd sampleTimestamps:...]); the device resolves nanosecond deltas via
// [device sampleTimestamps:gpuTimestamp:] correlation in get_query_results.
// Occlusion / pipeline-statistics have no MTLCounterSampleBuffer analogue —
// the device's create_query_pool returns kNotImplemented for those, gated on
// counter support, so this object is only ever constructed for kTimestamp.
// ---------------------------------------------------------------------------
class MetalQueryPoolObj final
{
public:
    MetalQueryPoolObj(id<MTLCounterSampleBuffer> sample_buffer,
                      QueryType type,
                      std::uint32_t count) noexcept
        : sample_buffer_(sample_buffer), type_(type), count_(count) {}
    ~MetalQueryPoolObj() = default;
    MetalQueryPoolObj(const MetalQueryPoolObj&) = delete;
    MetalQueryPoolObj& operator=(const MetalQueryPoolObj&) = delete;
    MetalQueryPoolObj(MetalQueryPoolObj&&) = delete;
    MetalQueryPoolObj& operator=(MetalQueryPoolObj&&) = delete;

    [[nodiscard]] id<MTLCounterSampleBuffer> sample_buffer() const noexcept { return sample_buffer_; }
    [[nodiscard]] QueryType type() const noexcept { return type_; }
    [[nodiscard]] std::uint32_t count() const noexcept { return count_; }

private:
    id<MTLCounterSampleBuffer> sample_buffer_ { nil };
    QueryType                  type_ { QueryType::kTimestamp };
    std::uint32_t              count_ { 0 };
};

// ---------------------------------------------------------------------------
// MetalCommandBufferImpl — ICommandBuffer wrapper around
// id<MTLCommandBuffer> + the active id<MTLRenderCommandEncoder>.
//
// Sprint-1 surface:
//   * begin/end                — allocate / commit (commit happens on submit)
//   * begin_render_pass        — build MTLRenderPassDescriptor, open encoder
//   * end_render_pass          — [encoder endEncoding]
//   * bind_graphics_pipeline   — [encoder setRenderPipelineState:]
//   * draw                     — [encoder drawPrimitives:]
//   * set_viewport / set_scissor — direct MTLViewport / MTLScissorRect calls
//   * push_debug_group / pop_debug_group — [encoder pushDebugGroup:]
// Every other ICB method falls back to a silent no-op for Sprint-1; later
// sprints fill those in.
// ---------------------------------------------------------------------------
class MetalCommandBufferImpl final : public ICommandBuffer
{
public:
    explicit MetalCommandBufferImpl(id<MTLCommandQueue> queue,
                                    MetalDeviceCtx* ctx) noexcept;
    ~MetalCommandBufferImpl() override = default;

    void begin() override;
    void end() override;

    void begin_render_pass(const RenderPassBeginInfo& info) override;
    void end_render_pass() override;

    void bind_graphics_pipeline(GraphicsPipelineHandle pipeline) override;
    // phase572 (Sprint-3): real setComputePipelineState path. Opens a
    // lazy MTLComputeCommandEncoder via ensure_compute_encoder_open(),
    // closing any active render / blit encoder first (Metal disallows
    // nested encoders on a single cmd-buf).
    void bind_compute_pipeline(ComputePipelineHandle pipeline) override;
    // M4 (ADR-20260615): bind a descriptor set as a Metal argument buffer.
    // Vulkan descriptor set N -> Metal [[buffer(N)]] (M3 contract); the
    // argument buffer is bound to BOTH the vertex + fragment slots so a set
    // declared for either stage resolves, and every resource the argument
    // buffer references is made resident via [encoder useResource:usage:]
    // (mandatory for argument buffers — otherwise the GPU cannot see the
    // referenced MTLBuffer/MTLTexture). On a compute encoder the set is bound
    // via [computeEncoder setBuffer:offset:atIndex:].
    void bind_descriptor_set(std::uint32_t set_index, DescriptorSetHandle set) override;

    // M11 (B2 — ADR-20260615): bind a bindless texture array's argument buffer
    // at the DEDICATED bindless set slot (its own descriptor set, Memory rule 9)
    // and make every populated slot's texture resident via [encoder useResource:]
    // (the M9-residency lesson — an arg-buffer resource not made resident reads
    // as garbage). Render encoder: bound to BOTH vertex + fragment stages (the
    // ray-side sample is fragment-stage, but spanning both is the never-under-
    // resident direction). Compute encoder: bound + made resident for the
    // compute stage. Lookup miss / no encoder -> graceful skip.
    void bind_bindless_texture_array(std::uint32_t set_index,
                                     BindlessTextureArrayHandle array) override;

    // phase572 (Sprint-3): real setVertexBuffer path. Vertex buffer table
    // indices are taken from the `binding` parameter (which maps to the
    // VertexBinding::binding index in the engine's vertex layout). When
    // the lookup misses (Sprint-2 still hands out unbacked stub handles),
    // the call is gracefully skipped, mirroring the copy-buffer fallback.
    void bind_vertex_buffer(std::uint32_t binding, BufferHandle buffer,
                            std::uint64_t offset) override;
    // phase572 (Sprint-3): caches the index buffer + offset + MTLIndexType
    // for the next draw_indexed call. Metal binds the index buffer at
    // draw-call time (not as a pre-bound state) so we have to stash here
    // until the drawIndexedPrimitives:..:indexBuffer:.. is issued.
    void bind_index_buffer(BufferHandle buffer, std::uint64_t offset,
                           IndexType type) override;

    // phase559 (Sprint-2): real setVertexBytes / setFragmentBytes path.
    // PipelineLayoutHandle is ignored — Metal does not consume a layout
    // object for inline-byte arg buffers; the ShaderStage mask selects
    // which encoder slot receives the bytes (vertex / fragment / both).
    // `offset` is taken as the Metal `index` of the buffer-argument slot
    // (the canonical Vulkan→MSL convention via SPIRV-Cross is
    // `[[buffer(n)]]` where n is the push-constant set's binding index).
    void push_constants(PipelineLayoutHandle layout, ShaderStage stages,
                        std::uint32_t offset, std::uint32_t size,
                        const void* data) override;

    void set_viewport(const Viewport& vp) override;
    void set_scissor(const Rect2D& rect) override;

    void draw(std::uint32_t vertex_count, std::uint32_t instance_count,
              std::uint32_t first_vertex, std::uint32_t first_instance) override;
    // phase572 (Sprint-3): drawIndexedPrimitives via the cached index buffer
    // state captured by bind_index_buffer. Gracefully no-ops when the
    // cached index buffer is nil (handle still unbacked under the Sprint-3
    // baseline) so the cmd-buffer doesn't crash before allocation lights up.
    void draw_indexed(std::uint32_t index_count, std::uint32_t instance_count,
                      std::uint32_t first_index, std::int32_t vertex_offset,
                      std::uint32_t first_instance) override;
    // M10 (B2 — ADR-20260615): [renderEncoder drawMeshThreadgroups:
    // threadsPerObjectThreadgroup:threadsPerMeshThreadgroup:]. The (x,y,z) args
    // are the THREADGROUP COUNTS — matching Vulkan vkCmdDrawMeshTasksEXT and
    // D3D12 DispatchMesh group semantics; the per-group thread counts come from
    // the bound mesh PSO's reflected object/mesh local sizes (cached by
    // bind_graphics_pipeline). Gracefully no-ops when no MESH pipeline is bound
    // (a classic graphics pipeline cannot service drawMeshThreadgroups).
    void draw_mesh_tasks(std::uint32_t group_x, std::uint32_t group_y,
                         std::uint32_t group_z) override;
    // phase572 (Sprint-3): MTLComputeCommandEncoder dispatchThreadgroups
    // path. Threads-per-threadgroup is taken from the bound compute PSO's
    // maxTotalThreadsPerThreadgroup property when not specified by the
    // caller (Sprint-3 surface uses 1x1x1 threadgroup size — real compute
    // shaders override via SPIRV-Cross attributes when they land).
    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;

    // A-INDIRECT (Backend-to-100 Wave 3a, gated-off / structural): GPU-driven
    // draw/dispatch via Metal indirect-buffer encoder calls
    // ([encoder drawPrimitives:indirectBuffer:indirectBufferOffset:],
    //  [encoder drawIndexedPrimitives:...indirectBuffer:...],
    //  [computeEncoder dispatchThreadgroupsWithIndirectBuffer:...]). The bound
    // primitive / index buffer state is honoured exactly like the by-value
    // draw paths; a lookup miss (unbacked handle) gracefully skips. draw_count
    // > 1 issues one indirect draw per record (Metal indirect draws consume a
    // single record), advancing the offset by `stride` each iteration.
    void draw_indirect(BufferHandle args, std::uint64_t offset,
                       std::uint32_t draw_count, std::uint32_t stride) override;
    void draw_indexed_indirect(BufferHandle args, std::uint64_t offset,
                               std::uint32_t draw_count, std::uint32_t stride) override;
    void dispatch_indirect(BufferHandle args, std::uint64_t offset) override;

    // A-QUERY (Backend-to-100 Wave 3a, gated-off / structural): timestamp
    // queries via MTLCounterSampleBuffer. write_timestamp samples the GPU
    // timestamp counter at `index` ([encoder sampleCountersInBuffer:...] on the
    // active encoder, or sampleTimestamps on the cmd buffer); occlusion +
    // pipeline-statistics have no direct MTLCounterSampleBuffer analogue and
    // are handled by the device (create_query_pool returns kNotImplemented for
    // those gated on counter support), so begin/end/reset are parity no-ops.
    void write_timestamp(QueryPoolHandle pool, std::uint32_t index) override;
    void begin_query(QueryPoolHandle pool, std::uint32_t index) override;
    void end_query(QueryPoolHandle pool, std::uint32_t index) override;
    void reset_query_pool(QueryPoolHandle pool, std::uint32_t first,
                          std::uint32_t count) override;

    // phase559 (Sprint-2): real MTLBlitCommandEncoder paths.
    //
    // The encoder is lazily opened on first copy call inside the current
    // cmd-buf and closed in submit_internal() (or before a render encoder
    // is opened — Metal disallows nested encoders on one cmd-buf). When a
    // BufferHandle / TextureHandle is not yet backed by a real Metal
    // resource (still the Sprint-2 default; create_buffer/create_texture
    // return stub handles), the lookup returns nil and the copy is
    // gracefully skipped, mirroring the Sprint-1 render-pass fallback.
    void copy_buffer(BufferHandle src, BufferHandle dst,
                     std::span<const BufferCopyRegion> regions) override;
    void copy_buffer_to_image(BufferHandle src, TextureHandle dst,
                              std::span<const BufferImageCopyRegion> regions) override;
    void copy_image_to_buffer(TextureHandle src, BufferHandle dst,
                              std::span<const BufferImageCopyRegion> regions) override;
    // V-COPY-IMG (Backend-to-100 Wave 3c): image→image copy via the blit encoder
    // [blit copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:
    //  toTexture:destinationSlice:destinationLevel:destinationOrigin:]. Caller
    // owns the surrounding barriers; lookup miss latches recording_error_ and
    // gracefully skips, mirroring the buffer↔image copies.
    void copy_texture_to_texture(TextureHandle src, TextureHandle dst,
                                 std::span<const TextureCopyRegion> regions) override;

    // M5 (ADR-20260615): Metal auto-tracks most synchronisation at encoder
    // boundaries for tracked resources; an explicit barrier is only needed
    // for ordering/visibility WITHIN an open encoder. We translate the
    // buffer/texture barrier spans into [renderEncoder memoryBarrierWith
    // Scope:afterStages:beforeStages:] / [computeEncoder memoryBarrierWith
    // Scope:] on the active encoder. Cross-encoder / queue ordering is
    // already covered by Metal's automatic hazard tracking + the submit-time
    // MTLSharedEvent hand-off (no MTLFence needed for the engine's tracked
    // resources). Layout transitions do not exist on Metal (storageMode is
    // fixed) so only the scope (buffers/textures) is mapped.
    void barrier(std::span<const BufferBarrier> bb,
                 std::span<const TextureBarrier> tb) override;

    // M9 (ADR-20260615): build a BLAS/TLAS on an MTLAccelerationStructure
    // CommandEncoder (closes any open render/blit/compute encoder first, as
    // Metal forbids nested encoders). Ray-query path — NOT the SBT pipeline.
    void build_acceleration_structure(AccelStructureHandle as) override;
    // A-REFIT (Backend-to-100 Wave 3b): in-place refit on the AS encoder via
    // refitAccelerationStructure:descriptor:destination:scratchBuffer: when the
    // AS opted into kAllowUpdate; falls back to a full build otherwise.
    void refit_acceleration_structure(AccelStructureHandle as) override;
    // M9: barrier between an AS build and a subsequent AS build/use on the
    // same command buffer (TLAS rebuild after an in-place BLAS rebuild). On
    // Metal the AS encoder boundary + the render/compute encoder that consumes
    // the TLAS via ray-query is the synchronisation point; we issue a buffer-
    // scope memory barrier on the active encoder when one is open.
    void acceleration_structure_barrier() override;

    void push_debug_group(std::string_view name) override;
    void pop_debug_group() override;

    // A-RESULT-DIAG (Backend-to-100 Wave 3c) — queryable recording-error flag.
    [[nodiscard]] bool recording_error() const noexcept override { return recording_error_; }

    // Sprint-1 commit/submit wiring. submit() is called by MetalDevice::submit;
    // it commits the underlying MTLCommandBuffer to the queue. If a swapchain
    // drawable was bound (via the active render pass), it is presented here.
    void submit_internal(id<CAMetalDrawable> drawable_to_present) noexcept;

    [[nodiscard]] id<MTLCommandBuffer> mtl_cmd_buf() const noexcept { return cmd_; }

private:
    // phase559: lazy-open the blit encoder on first copy call and close it
    // before any render encoder is opened (Metal disallows nested encoders
    // on a single cmd-buf). Closed automatically by submit_internal.
    void ensure_blit_encoder_open();
    void close_blit_encoder_if_open() noexcept;

    // phase572 (Sprint-3): same lazy-open / encoder-transition discipline
    // for the compute encoder. ensure_compute_encoder_open() closes any
    // active render / blit encoder before opening the compute encoder;
    // close_compute_encoder_if_open() is called from begin_render_pass +
    // ensure_blit_encoder_open + submit_internal.
    void ensure_compute_encoder_open();
    void close_compute_encoder_if_open() noexcept;

    // M9 (ADR-20260615): lazy-open the acceleration-structure encoder for AS
    // builds; same encoder-transition discipline (closes render/blit/compute
    // first). Closed by submit_internal + any other encoder-open helper.
    void ensure_accel_encoder_open();
    void close_accel_encoder_if_open() noexcept;

    // M8 (ADR-20260615): resolve a render-target / depth attachment view to a
    // live MTLTexture (swapchain drawable OR M1-backed offscreen target).
    [[nodiscard]] id<MTLTexture>
    resolve_attachment_texture(TextureViewHandle view) noexcept;

    id<MTLCommandQueue>          queue_ { nil };
    id<MTLCommandBuffer>         cmd_ { nil };
    id<MTLRenderCommandEncoder>  encoder_ { nil };
    id<MTLBlitCommandEncoder>    blit_ { nil };
    id<MTLComputeCommandEncoder> compute_ { nil };
    id<MTLAccelerationStructureCommandEncoder> accel_ { nil };
    MetalDeviceCtx*              ctx_ { nullptr };  // observer, not owning

    // M2 (ADR-20260615): primitive type of the currently-bound graphics
    // pipeline, applied to draw() / draw_indexed(). Sourced from the bound
    // MetalGraphicsPipelineStateObj (replaces the hard-coded Triangle in the
    // Sprint-1 baseline). Defaults to Triangle so an un-bound draw matches the
    // legacy behaviour.
    MTLPrimitiveType             bound_primitive_ { MTLPrimitiveTypeTriangle };

    // M10 (B2 — ADR-20260615): mesh-pipeline draw state, cached by
    // bind_graphics_pipeline from the bound MetalGraphicsPipelineStateObj.
    // bound_is_mesh_ gates draw_mesh_tasks (a classic graphics pipeline cannot
    // service drawMeshThreadgroups); the two threadgroup sizes are the reflected
    // object/mesh local sizes fed to drawMeshThreadgroups:... Reset to the
    // non-mesh default whenever a classic graphics pipeline is bound.
    bool                         bound_is_mesh_ { false };
    MTLSize                      bound_object_threads_per_tg_ { 1, 1, 1 };
    MTLSize                      bound_mesh_threads_per_tg_ { 1, 1, 1 };
    // Cached attachment view for the active render pass — used to derive
    // the colour-target texture when no real TextureViewHandle registry
    // exists yet (Sprint-1 ties views to swapchain drawables only).

    // phase572 (Sprint-3): index buffer cache for draw_indexed. Metal
    // binds the index buffer at draw-call time, so we have to stash the
    // last bind_index_buffer values here and replay them when
    // drawIndexedPrimitives: fires.
    id<MTLBuffer>                index_buf_ { nil };
    NSUInteger                   index_offset_ { 0 };
    MTLIndexType                 index_type_ { MTLIndexTypeUInt16 };

    // phase572 (Sprint-3): currently-bound compute PSO. M6 (ADR-20260615):
    // bind_compute_pipeline also caches the bound PSO's reflected
    // threads-per-threadgroup (GLSL local_size_x/y/z) here so dispatch() applies
    // the real threadgroup size instead of the Sprint-3 (1,1,1). Reset to
    // (1,1,1) whenever no PSO is bound.
    id<MTLComputePipelineState>  current_compute_pso_ { nil };
    MTLSize                      current_threads_per_threadgroup_ { 1, 1, 1 };

    // A-BINDPOINT (Backend-to-100 Wave 3c): on Metal the descriptor-set bind
    // point is ROBUST BY CONSTRUCTION — a render encoder and a compute encoder
    // are mutually exclusive on one command buffer (Metal forbids nested
    // encoders), so bind_descriptor_set routes to whichever encoder is open
    // (encoder_ => graphics, compute_ => compute). The phase1213 DDGI bug class
    // (a compute bind after a render pass silently picking graphics) CANNOT occur
    // here because begin_render_pass / ensure_compute_encoder_open close the
    // other encoder first. We still track an EXPLICIT bind point for the
    // cross-backend contract + diagnostics: set by bind_graphics_pipeline
    // (kGraphics) / bind_compute_pipeline (kCompute) / a future RT pipeline
    // (kRayTracing); reset to kCompute in end_render_pass to mirror Vulkan/D3D12.
    cd::rhi::BindPoint           bound_point_ { cd::rhi::BindPoint::kCompute };

    // A-RESULT-DIAG (Backend-to-100 Wave 3c): latched true on any recording-time
    // lookup miss; reset in begin(); queried via recording_error(). Latched via
    // note_recording_error_ which also fires the gated debug assert (LOUD in
    // debug, queryable in release) — same contract as Vulkan/D3D12.
    bool                         recording_error_ { false };
    void note_recording_error_(const char* where) noexcept
    {
        recording_error_ = true;
        assert((!cd::rhi::recording_error_assert_enabled() || false) && where);
        (void)where;
    }
};

// ---------------------------------------------------------------------------
// MetalDeviceCtx — opaque interface from MetalCommandBuffer back into the
// device for swapchain-image and pipeline-state lookup. Concrete definition
// lives in MetalDevice.mm; here we forward-declare so other TUs see only
// what they need.
// ---------------------------------------------------------------------------
class MetalDeviceCtx
{
public:
    virtual ~MetalDeviceCtx() = default;

    // Look up the pipeline-state object bound by GraphicsPipelineHandle.
    // Returns nil for unknown / invalid handles.
    [[nodiscard]] virtual id<MTLRenderPipelineState>
    lookup_pipeline(GraphicsPipelineHandle h) const noexcept = 0;

    // M2 (ADR-20260615): the richer graphics-pipeline-state object carrying
    // the MTLRenderPipelineState + the matching MTLDepthStencilState + the
    // resolved cull/winding/primitive. bind_graphics_pipeline consumes this
    // so the depth-stencil state + raster state apply on the encoder. Returns
    // nullptr for unknown / invalid handles.
    [[nodiscard]] virtual const MetalGraphicsPipelineStateObj*
    lookup_graphics_pipeline_state(GraphicsPipelineHandle h) const noexcept = 0;

    // M9 (ADR-20260615): the acceleration-structure object bound by an
    // AccelStructureHandle. build_acceleration_structure consumes it; the
    // argument-buffer update path binds the TLAS into a descriptor set via
    // [argEncoder setAccelerationStructure:atIndex:]. nullptr for unknown
    // handles.
    [[nodiscard]] virtual MetalAccelObj*
    lookup_accel(AccelStructureHandle h) const noexcept = 0;

    // Look up the drawable's MTLTexture for the colour attachment named by
    // a TextureViewHandle that came from swapchain_image_view(). Returns nil
    // for unknown handles. The drawable itself is set on the swapchain
    // object and must be presented by the cmd-buffer at submit time.
    [[nodiscard]] virtual id<MTLTexture>
    lookup_swapchain_view_texture(TextureViewHandle h) const noexcept = 0;

    // Look up the swapchain that owns view `h`, so the command buffer can
    // present the drawable on submit. nullptr when `h` is not a swapchain
    // view.
    [[nodiscard]] virtual MetalSwapchainObj*
    lookup_swapchain_for_view(TextureViewHandle h) const noexcept = 0;

    // phase559 (Sprint-2): resource lookups for the cmd-buffer copy + the
    // descriptor / argument-buffer write paths. Buffer + texture maps are
    // empty in Sprint-2 (create_buffer / create_texture still hand out
    // unbacked stub handles); the lookups return nil so the cmd-buffer
    // gracefully skips the work, matching Sprint-1's render-pass fallback.
    // Sprint 3 lights up the maps by routing creation through real
    // [device newBufferWithLength:] / [device newTextureWithDescriptor:].
    [[nodiscard]] virtual id<MTLBuffer>
    lookup_buffer(BufferHandle h) const noexcept = 0;

    [[nodiscard]] virtual id<MTLTexture>
    lookup_texture(TextureHandle h) const noexcept = 0;

    [[nodiscard]] virtual id<MTLSamplerState>
    lookup_sampler(SamplerHandle h) const noexcept = 0;

    // phase572 (Sprint-3): shader-module + compute-pipeline lookups.
    // The shader-module lookup is consumed by create_compute_pipeline; the
    // compute-pipeline lookup is consumed by bind_compute_pipeline. Both
    // return nil for unknown / invalid handles.
    [[nodiscard]] virtual const MetalShaderModuleObj*
    lookup_shader_module(ShaderModuleHandle h) const noexcept = 0;

    [[nodiscard]] virtual id<MTLComputePipelineState>
    lookup_compute_pipeline(ComputePipelineHandle h) const noexcept = 0;

    // M6 (ADR-20260615): the richer compute-pipeline object carrying the PSO
    // PLUS the reflected threads-per-threadgroup MTLSize. bind_compute_pipeline
    // reads the workgroup size from here so dispatch() can apply the real
    // threads-per-group instead of the Sprint-3 (1,1,1). nullptr for unknown
    // handles.
    [[nodiscard]] virtual const MetalComputePipelineObj*
    lookup_compute_pipeline_obj(ComputePipelineHandle h) const noexcept = 0;

    // phase615 (Sprint-4): fence + event lookups. Consumed by
    // submit(SubmitDesc) for completion-handler fence signal + queue-side
    // event encodes (encodeSignalEvent / encodeWaitForEvent). Both return
    // nullptr for unknown / invalid handles.
    [[nodiscard]] virtual MetalFenceObj*
    lookup_fence(FenceHandle h) const noexcept = 0;

    [[nodiscard]] virtual MetalEventObj*
    lookup_event(SemaphoreHandle h) const noexcept = 0;

    // phase649 (Sprint-5): texture-view + descriptor-set + timeline lookups.
    // The descriptor-set lookup is consumed by update_descriptor_set + the
    // future bind_descriptor_set path; the timeline lookup is consumed by
    // submit(SubmitDesc) for the host-encoded wait / signal value hand-off.
    // All three return nullptr for unknown / invalid handles, matching the
    // rest of the Metal-side resolver contract.
    [[nodiscard]] virtual id<MTLTexture>
    lookup_texture_view(TextureViewHandle h) const noexcept = 0;

    [[nodiscard]] virtual MetalDescriptorSetObj*
    lookup_descriptor_set(DescriptorSetHandle h) const noexcept = 0;

    [[nodiscard]] virtual MetalTimelineObj*
    lookup_timeline(TimelineSemaphoreHandle h) const noexcept = 0;

    // M11 (B2 — ADR-20260615): bindless texture-array lookup. Consumed by
    // bind_bindless_texture_array (binds the arg buffer + replays the populated
    // slots' residency). nullptr for unknown handles, matching the resolver
    // contract.
    [[nodiscard]] virtual MetalBindlessArrayObj*
    lookup_bindless_array(BindlessTextureArrayHandle h) const noexcept = 0;

    // A-QUERY (Backend-to-100 Wave 3a, gated-off / structural): query-pool
    // lookup. Consumed by write_timestamp (samples the MTLCounterSampleBuffer
    // slot). nullptr for unknown handles, matching the resolver contract.
    [[nodiscard]] virtual MetalQueryPoolObj*
    lookup_query_pool(QueryPoolHandle h) const noexcept = 0;
};

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
