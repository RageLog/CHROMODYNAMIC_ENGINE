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
// Everything else (descriptor sets allocation, fences, RT) lives in
// subsequent sprints; the remaining kNotImpl call surface in
// MetalDevice.mm shrinks accordingly each sprint.
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
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cd::rhi::metal::detail
{

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
    // provide a drawable (off-screen, minimised, GPU stalled). Caller
    // owns the returned reference under ARC.
    [[nodiscard]] id<CAMetalDrawable> acquire_drawable() noexcept;

    // Bookkeeping for the most recently acquired drawable, so present()
    // can locate it from the swapchain handle alone.
    void set_current_drawable(id<CAMetalDrawable> d) noexcept { current_ = d; }
    [[nodiscard]] id<CAMetalDrawable> current_drawable() const noexcept { return current_; }

    [[nodiscard]] CAMetalLayer* layer() const noexcept { return layer_; }
    [[nodiscard]] std::uint32_t image_count() const noexcept { return image_count_; }
    [[nodiscard]] MTLPixelFormat pixel_format() const noexcept { return pixel_format_; }

private:
    CAMetalLayer*       layer_ { nil };
    id<CAMetalDrawable> current_ { nil };
    std::uint32_t       image_count_ { 0 };
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
    MetalShaderModuleObj(id<MTLLibrary> lib, id<MTLFunction> fn, ShaderStage stage) noexcept
        : lib_(lib), fn_(fn), stage_(stage) {}
    ~MetalShaderModuleObj() = default;
    MetalShaderModuleObj(const MetalShaderModuleObj&) = delete;
    MetalShaderModuleObj& operator=(const MetalShaderModuleObj&) = delete;
    MetalShaderModuleObj(MetalShaderModuleObj&&) = delete;
    MetalShaderModuleObj& operator=(MetalShaderModuleObj&&) = delete;

    [[nodiscard]] id<MTLLibrary>  lib() const noexcept { return lib_; }
    [[nodiscard]] id<MTLFunction> fn() const noexcept { return fn_; }
    [[nodiscard]] ShaderStage     stage() const noexcept { return stage_; }

private:
    id<MTLLibrary>  lib_ { nil };
    id<MTLFunction> fn_ { nil };
    ShaderStage     stage_ { ShaderStage::kNone };
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
// MetalComputePipelineObj — phase572 / Sprint-3.
//
// Wraps id<MTLComputePipelineState> built via
// [device newComputePipelineStateWithFunction:error:]. The PSO is opaque
// once created; the cmd-buffer binds it via setComputePipelineState: on
// the active MTLComputeCommandEncoder.
// ---------------------------------------------------------------------------
class MetalComputePipelineObj final
{
public:
    explicit MetalComputePipelineObj(id<MTLComputePipelineState> pso) noexcept
        : pso_(pso) {}
    ~MetalComputePipelineObj() = default;
    MetalComputePipelineObj(const MetalComputePipelineObj&) = delete;
    MetalComputePipelineObj& operator=(const MetalComputePipelineObj&) = delete;
    MetalComputePipelineObj(MetalComputePipelineObj&&) = delete;
    MetalComputePipelineObj& operator=(MetalComputePipelineObj&&) = delete;

    [[nodiscard]] id<MTLComputePipelineState> pso() const noexcept { return pso_; }

private:
    id<MTLComputePipelineState> pso_ { nil };
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
    [[nodiscard]] std::uint64_t next_signal_value() noexcept
    {
        return ++signal_counter_;
    }

    // Return the highest value that has been encoded for signalling so far.
    // The matching wait_semaphores entry needs to block until that value
    // is reached (the producer queue raises it; the consumer waits on it).
    [[nodiscard]] std::uint64_t current_signal_value() const noexcept
    {
        return signal_counter_;
    }

private:
    id<MTLSharedEvent> event_ { nil };
    std::uint64_t      signal_counter_ { 0 };
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

private:
    std::vector<DescriptorSetLayoutBinding> bindings_;
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
    void bind_descriptor_set(std::uint32_t /*set_index*/, DescriptorSetHandle /*set*/) override {}

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
    // phase572 (Sprint-3): MTLComputeCommandEncoder dispatchThreadgroups
    // path. Threads-per-threadgroup is taken from the bound compute PSO's
    // maxTotalThreadsPerThreadgroup property when not specified by the
    // caller (Sprint-3 surface uses 1x1x1 threadgroup size — real compute
    // shaders override via SPIRV-Cross attributes when they land).
    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;

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

    void barrier(std::span<const BufferBarrier> /*bb*/,
                 std::span<const TextureBarrier> /*tb*/) override {}

    void push_debug_group(std::string_view name) override;
    void pop_debug_group() override;

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

    id<MTLCommandQueue>          queue_ { nil };
    id<MTLCommandBuffer>         cmd_ { nil };
    id<MTLRenderCommandEncoder>  encoder_ { nil };
    id<MTLBlitCommandEncoder>    blit_ { nil };
    id<MTLComputeCommandEncoder> compute_ { nil };
    MetalDeviceCtx*              ctx_ { nullptr };  // observer, not owning
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

    // phase572 (Sprint-3): currently-bound compute PSO. Used to recover
    // threads-per-threadgroup at dispatch time when the caller does not
    // pre-supply a workgroup size (Sprint-3 surface uses 1x1x1).
    id<MTLComputePipelineState>  current_compute_pso_ { nil };
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

    // phase615 (Sprint-4): fence + event lookups. Consumed by
    // submit(SubmitDesc) for completion-handler fence signal + queue-side
    // event encodes (encodeSignalEvent / encodeWaitForEvent). Both return
    // nullptr for unknown / invalid handles.
    [[nodiscard]] virtual MetalFenceObj*
    lookup_fence(FenceHandle h) const noexcept = 0;

    [[nodiscard]] virtual MetalEventObj*
    lookup_event(SemaphoreHandle h) const noexcept = 0;
};

}  // namespace cd::rhi::metal::detail

#endif  // __APPLE__
