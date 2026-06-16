// =============================================================================
// CHROMODYNAMIC — cd/rhi/ICommandBuffer.hpp
// ADR-001 (Sprint S3.1) — RHI command-buffer recording surface.
//
// A command buffer captures GPU work between `begin()` and `end()`, then is
// submitted via IDevice::submit(). Recording is single-threaded per buffer;
// multiple buffers may be recorded in parallel and submitted in order.
//
// The interface intentionally mirrors Vulkan's structure (passes, dynamic
// state, pipeline binding) so the Vulkan back-end is a direct translation
// and the D3D12 back-end translates state transitions to barriers.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/Pipeline.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

namespace cd::rhi
{

struct ColorAttachmentInfo
{
    TextureViewHandle view {};
    LoadOp load_op { LoadOp::kClear };
    StoreOp store_op { StoreOp::kStore };
    ClearColor clear_color {};
    /// V-MSAA-RESOLVE (Backend-to-100 Wave 3c) — optional single-sample resolve
    /// target. When `view` names a multisample texture and this names a 1x
    /// texture of the same format/extent, the backend resolves (averages) the
    /// MSAA samples into it at pass end: Vulkan wires
    /// VkRenderingAttachmentInfo.resolveMode = AVERAGE + resolveImageView;
    /// D3D12 records ResolveSubresource after the pass; Metal sets the
    /// colorAttachment's resolveTexture + MultisampleResolve store action.
    /// Default-invalid (`is_valid() == false`) = no resolve, byte-identical to
    /// the pre-Wave-3c single-sample path.
    TextureViewHandle resolve_view {};
};

/// A-RESULT-DIAG (Backend-to-100 Wave 3c) — process-wide toggle for the debug
/// recording-error ASSERT. The recording-error contract (see ICommandBuffer)
/// asserts at a lookup-miss site in debug builds so a stale handle is LOUD.
/// A test that DELIBERATELY feeds a bad handle (to prove recording_error()
/// latches) disables the assert around that one call so the deliberate miss does
/// not abort the process; the queryable flag is still set either way. Default
/// enabled — real (unintended) misses stay loud. Implemented as a function-local
/// static so the single definition lives in the header (no extra TU).
[[nodiscard]] inline std::atomic<bool>& detail_recording_error_assert_enabled() noexcept
{
    static std::atomic<bool> enabled { true };
    return enabled;
}
inline void set_recording_error_assert_enabled(bool on) noexcept
{
    detail_recording_error_assert_enabled().store(on, std::memory_order_relaxed);
}
[[nodiscard]] inline bool recording_error_assert_enabled() noexcept
{
    return detail_recording_error_assert_enabled().load(std::memory_order_relaxed);
}

/// A-BINDPOINT (Backend-to-100 Wave 3c) — the pipeline bind point a command
/// buffer is currently routing descriptor-set / push-constant binds to. Tracked
/// EXPLICITLY per command buffer (set by bind_graphics_pipeline -> kGraphics,
/// bind_compute_pipeline -> kCompute, bind_rt_pipeline -> kRayTracing) instead
/// of inferring it from "which native layout handle is non-null", which made a
/// compute bind after a render pass silently pick the GRAPHICS bind point when
/// end_render_pass forgot to clear the stale graphics layout (the DDGI bug class,
/// phase1213). CONVENTION: ray tracing ALIASES compute for descriptor binding —
/// DXR root arguments and Vulkan RT descriptors both bind through the compute
/// path — so a kRayTracing bind point resolves descriptors the same way kCompute
/// does. end_render_pass() resets the bind point to a safe non-graphics state.
enum class BindPoint : std::uint8_t
{
    kGraphics,
    kCompute,
    kRayTracing,
};

struct DepthStencilAttachmentInfo
{
    TextureViewHandle view {};
    LoadOp depth_load { LoadOp::kClear };
    StoreOp depth_store { StoreOp::kStore };
    LoadOp stencil_load { LoadOp::kDontCare };
    StoreOp stencil_store { StoreOp::kDontCare };
    ClearDepthStencil clear {};
};

struct RenderPassBeginInfo
{
    Rect2D render_area {};
    std::span<const ColorAttachmentInfo> color_attachments;
    const DepthStencilAttachmentInfo* depth_stencil { nullptr };
};

struct BufferCopyRegion
{
    std::uint64_t src_offset { 0 };
    std::uint64_t dst_offset { 0 };
    std::uint64_t size { 0 };
};

/// One sub-region of a buffer→image copy. The buffer is treated as a flat
/// byte blob; the image side is addressed by mip / array layer / 3D extent.
/// The engine assumes tightly-packed input (`buffer_row_length = 0`,
/// `buffer_image_height = 0` in Vulkan terms) so the driver derives the
/// row pitch from the texel extent — adequate for the common case of
/// uploading a decoded PNG/JPG image.
struct BufferImageCopyRegion
{
    std::uint64_t buffer_offset { 0 };
    std::uint32_t mip_level { 0 };
    std::uint32_t base_layer { 0 };
    std::uint32_t layer_count { 1 };
    Offset3D image_offset { 0, 0, 0 };
    Extent3D image_extent { 1, 1, 1 };
};

/// V-COPY-IMG (Backend-to-100 Wave 3c) — one sub-region of an image→image copy.
/// Both sides are addressed by mip / array layer / 3D offset+extent. Used by
/// mip-chain generation and image blits (the buffer round-trip of
/// copy_image_to_buffer + copy_buffer_to_image is the slow alternative this
/// avoids). Maps to Vulkan vkCmdCopyImage (or vkCmdBlitImage when the extents
/// differ), D3D12 CopyTextureRegion, Metal copyFromTexture:toTexture:. The
/// caller owns the surrounding barriers: src in kTransferSrc, dst in
/// kTransferDst, symmetric with the buffer↔image copies.
struct TextureCopyRegion
{
    std::uint32_t src_mip_level { 0 };
    std::uint32_t src_base_layer { 0 };
    Offset3D src_offset { 0, 0, 0 };
    std::uint32_t dst_mip_level { 0 };
    std::uint32_t dst_base_layer { 0 };
    Offset3D dst_offset { 0, 0, 0 };
    std::uint32_t layer_count { 1 };
    Extent3D extent { 1, 1, 1 };
};

/// phase1115 (X1-FU-F) — the DRAW-SUBSET recording surface: exactly
/// the calls that are legal inside a render pass from a parallel
/// recording lane. ICommandBuffer extends this with lifecycle, pass
/// scope, compute, copies, barriers and RT — so an
/// IParallelPassRecorder::lane() can hand out an IDrawRecorder& and
/// the TYPE SYSTEM forbids pass/lifecycle calls from lanes (the
/// misuse class the X1FUF surface review flagged).
class IDrawRecorder
{
public:
    IDrawRecorder() noexcept = default;
    virtual ~IDrawRecorder() = default;
    IDrawRecorder(const IDrawRecorder&) = delete;
    IDrawRecorder& operator=(const IDrawRecorder&) = delete;
    IDrawRecorder(IDrawRecorder&&) = delete;
    IDrawRecorder& operator=(IDrawRecorder&&) = delete;

    virtual void bind_graphics_pipeline(GraphicsPipelineHandle pipeline) = 0;
    /// Phase 135 — RT pipeline binding. Non-pure-virtual default
    /// no-op so non-RT backends compile unchanged.
    virtual void bind_rt_pipeline(RtPipelineHandle /*pipeline*/) {}

    virtual void bind_descriptor_set(std::uint32_t set_index, DescriptorSetHandle set) = 0;

    /// D10 — bind a dedicated bindless texture array to a root descriptor-table
    /// slot (the engine's set-1 `register(t0, space1)[i]`). The backend binds the
    /// persistent shader-visible bindless heap and points root parameter
    /// `set_index` at the array's GPU base, so the shader dynamic-indexes the
    /// array. Non-pure default no-op so backends without a dedicated bindless
    /// pool (and the Null reference) compile unchanged; D3D12 overrides it.
    virtual void bind_bindless_texture_array(std::uint32_t /*set_index*/,
                                             BindlessTextureArrayHandle /*array*/) {}

    virtual void bind_vertex_buffer(std::uint32_t binding, BufferHandle buffer, std::uint64_t offset) = 0;
    virtual void bind_index_buffer(BufferHandle buffer, std::uint64_t offset, IndexType type) = 0;

    virtual void push_constants(
        PipelineLayoutHandle layout,
        ShaderStage stages,
        std::uint32_t offset,
        std::uint32_t size,
        const void* data
    ) = 0;

    // ---- Dynamic state -----------------------------------------------------
    virtual void set_viewport(const Viewport& vp) = 0;
    virtual void set_scissor(const Rect2D& rect) = 0;

    // ---- Draw ---------------------------------------------------------------
    virtual void draw(
        std::uint32_t vertex_count,
        std::uint32_t instance_count,
        std::uint32_t first_vertex,
        std::uint32_t first_instance
    ) = 0;
    virtual void draw_indexed(
        std::uint32_t index_count,
        std::uint32_t instance_count,
        std::uint32_t first_index,
        std::int32_t vertex_offset,
        std::uint32_t first_instance
    ) = 0;

    // ---- Indirect draw (A-INDIRECT, Backend-to-100 Wave 3a) ----------------
    //
    // GPU-driven draws: the draw parameters live in a buffer (created with
    // BufferUsage::kIndirect) instead of being passed by value, so a compute
    // pass can author the call without a CPU round-trip. `args` holds one or
    // more packed argument records starting at `offset`; `draw_count` records
    // are consumed `stride` bytes apart. The record layout matches the bound
    // backend's native struct (Vulkan VkDrawIndirectCommand /
    // VkDrawIndexedIndirectCommand, D3D12 D3D12_DRAW_ARGUMENTS /
    // D3D12_DRAW_INDEXED_ARGUMENTS — the two are field-for-field identical).
    //
    // Default no-op so backends without an indirect path (and the Null
    // reference) compile unchanged; the GPU backends override. Mirrors the
    // draw() / draw_indexed() contract: the caller binds the pipeline (and,
    // for draw_indexed_indirect, the index buffer) first.
    virtual void draw_indirect(BufferHandle /*args*/,
                               std::uint64_t /*offset*/,
                               std::uint32_t /*draw_count*/,
                               std::uint32_t /*stride*/) {}
    virtual void draw_indexed_indirect(BufferHandle /*args*/,
                                       std::uint64_t /*offset*/,
                                       std::uint32_t /*draw_count*/,
                                       std::uint32_t /*stride*/) {}

    // ---- Mesh shader ----------------------------------------------------------
    // Default no-op so backends without mesh-shader support compile
    // unchanged (full contract notes preserved from the pre-split
    // ICommandBuffer declaration).
    virtual void draw_mesh_tasks(std::uint32_t /*group_x*/,
                                 std::uint32_t /*group_y*/,
                                 std::uint32_t /*group_z*/) {}

    // ---- Debug ------------------------------------------------------------------
    /// Lifetime contract: `name` only needs to outlive *this call* —
    /// backends copy it into per-command-buffer storage because the
    /// native APIs may hold the pointer until GPU completion.
    virtual void push_debug_group(std::string_view name) = 0;
    virtual void pop_debug_group() = 0;
};

/// phase1115 (X1-FU-F) — a render pass whose draw recording is split
/// across thread-confined lanes. Obtain via
/// ICommandBuffer::begin_parallel_render_pass(); record on lane(i)
/// from AT MOST one thread each; finish() joins lanes into the primary
/// IN LANE ORDER (lane order == submission order, so same-input frames
/// replay identically regardless of worker scheduling) and ends the
/// pass. Lane creation follows IDevice threading-contract rule 1.
///
/// LIFETIME / THREADING CONTRACT (phase1119, audit A2+B2):
///   * finish() MUST be called before the recorder is destroyed. A
///     recorder abandoned without finish() leaves the primary's render
///     pass scope OPEN (begun with secondary-contents semantics): the
///     primary can neither record serial draws nor end() legally.
///   * Every lane recording MUST happens-before finish() (join your
///     workers first). Destroying or finishing the recorder while a
///     lane thread is still recording races the driver's command-pool
///     external-synchronisation requirement (device-lost).
///   * lane(i) with i >= lane_count() is a caller bug: it asserts in
///     debug builds and clamps to the last lane in release (two
///     threads aliasing one lane is the race described above).
class IParallelPassRecorder
{
public:
    IParallelPassRecorder() noexcept = default;
    virtual ~IParallelPassRecorder() = default;
    IParallelPassRecorder(const IParallelPassRecorder&) = delete;
    IParallelPassRecorder& operator=(const IParallelPassRecorder&) = delete;
    IParallelPassRecorder(IParallelPassRecorder&&) = delete;
    IParallelPassRecorder& operator=(IParallelPassRecorder&&) = delete;

    [[nodiscard]] virtual std::uint32_t lane_count() const noexcept = 0;
    /// Lane i recording surface — draw subset only, enforced by type.
    [[nodiscard]] virtual IDrawRecorder& lane(std::uint32_t i) noexcept = 0;
    /// Join lanes into the primary in lane order and end the pass.
    virtual void finish() = 0;
};

class ICommandBuffer : public IDrawRecorder
{
public:
    ICommandBuffer() noexcept = default;

    // ---- Lifecycle ---------------------------------------------------------
    virtual void begin() = 0;
    virtual void end() = 0;

    // ---- Render pass -------------------------------------------------------
    virtual void begin_render_pass(const RenderPassBeginInfo& info) = 0;
    virtual void end_render_pass() = 0;

    /// phase1115 (X1-FU-F): begin a render pass whose draws are recorded
    /// across parallel lanes. Default nullptr = backend has no
    /// parallel-recording support yet; callers fall back to the serial
    /// begin_render_pass() path.
    [[nodiscard]] virtual std::unique_ptr<IParallelPassRecorder>
    begin_parallel_render_pass(const RenderPassBeginInfo& /*info*/,
                               std::uint32_t /*lane_count*/)
    {
        return nullptr;
    }

    // ---- Compute -------------------------------------------------------------
    virtual void bind_compute_pipeline(ComputePipelineHandle pipeline) = 0;
    virtual void dispatch(std::uint32_t group_x, std::uint32_t group_y, std::uint32_t group_z) = 0;

    /// A-INDIRECT (Backend-to-100 Wave 3a) — GPU-driven dispatch. The group
    /// counts (x,y,z) are read from `args` at `offset` (a 3×u32 record matching
    /// Vulkan VkDispatchIndirectCommand / D3D12 D3D12_DISPATCH_ARGUMENTS).
    /// `args` must be created with BufferUsage::kIndirect. Default no-op so
    /// backends without an indirect path (and Null) compile unchanged; the
    /// caller binds the compute pipeline first, exactly like dispatch().
    virtual void dispatch_indirect(BufferHandle /*args*/, std::uint64_t /*offset*/) {}

    // ---- Copies / clears ---------------------------------------------------
    virtual void copy_buffer(BufferHandle src, BufferHandle dst, std::span<const BufferCopyRegion> regions) = 0;

    /// Stream bytes from a host-visible buffer into an image. The image is
    /// expected to already be in `kTransferDst` (the user emits the matching
    /// barrier before this call and another one after, back to the layout
    /// the shader expects, typically `kShaderResource`). Used for one-shot
    /// texture uploads — the dominant pattern in every asset pipeline.
    virtual void
    copy_buffer_to_image(BufferHandle src, TextureHandle dst, std::span<const BufferImageCopyRegion> regions) = 0;

    /// Stream bytes from an image into a host-visible buffer. The source
    /// image is expected to already be in `kTransferSrc` (the user emits
    /// the matching barriers around this call). The buffer must be sized
    /// `width * height * bytes_per_pixel` for an RGBA8 capture, etc. Used
    /// for screenshot / golden-image capture (Phase 11 Track A) and for
    /// any readback-style operation a compute or render pass requires.
    /// Symmetric with `copy_buffer_to_image`.
    virtual void
    copy_image_to_buffer(TextureHandle src, BufferHandle dst, std::span<const BufferImageCopyRegion> regions) = 0;

    /// V-COPY-IMG (Backend-to-100 Wave 3c) — copy region(s) image→image. Both
    /// textures must already be in the expected transfer states (src in
    /// kTransferSrc, dst in kTransferDst); the caller owns those barriers,
    /// symmetric with the buffer↔image copies. The backend uses a same-size
    /// copy (vkCmdCopyImage / CopyTextureRegion / copyFromTexture:toTexture:)
    /// per region; Vulkan additionally promotes to vkCmdBlitImage when a
    /// region's src and dst extents differ (a scaling blit). Default no-op so
    /// backends without an image-copy path (and the Null reference, which only
    /// counts it) compile unchanged; the GPU backends override.
    virtual void
    copy_texture_to_texture(TextureHandle /*src*/, TextureHandle /*dst*/,
                            std::span<const TextureCopyRegion> /*regions*/) {}

    // ---- Barriers ----------------------------------------------------------
    virtual void
    barrier(std::span<const BufferBarrier> buffer_barriers, std::span<const TextureBarrier> texture_barriers) = 0;

    // ---- Ray tracing (Phase 14.G — API shape only at v0.40.0) --------------
    //
    // Default non-pure-virtual implementations let backends without RT
    // support compile cleanly. The marathon-shippable v0.40.0 lands the
    // *type system* shape; backend wiring (Vulkan
    // vkBuildAccelerationStructuresKHR + vkCmdTraceRaysKHR) is queued
    // for the follow-up wave where the shader-binding-table surface
    // lands too.

    virtual void build_acceleration_structure(AccelStructureHandle /*as*/) {}

    /// A-REFIT (Backend-to-100 Wave 3b) — in-place update (refit) of an AS
    /// whose source data (vertex positions / instance transforms) changed but
    /// whose TOPOLOGY (primitive/instance count) did not. This is the cheap
    /// per-frame path for the skinned-mesh BLAS (CesiumMan) that otherwise
    /// rebuilds every frame. Maps to Vulkan MODE_UPDATE (src == dst) using the
    /// update-scratch size / D3D12 PERFORM_UPDATE / Metal refitAccelerationStructure.
    ///
    /// Contract: the AS must have been created with
    /// `AccelBuildFlags::kAllowUpdate`. When it was NOT, the implementation
    /// SAFELY FALLS BACK to a full rebuild (identical to
    /// build_acceleration_structure) — so a caller can always issue refit and
    /// get a valid AS; only the cost differs. Backends without RT no-op.
    virtual void refit_acceleration_structure(AccelStructureHandle /*as*/) {}

    /// Phase 251 — pipeline barrier between an AS build (BLAS or TLAS)
    /// and a subsequent AS build/use on the same command buffer. Required
    /// when a BLAS is rebuilt in-place every frame (e.g. CPU-LBS skinning
    /// updates the vertex buffer that backs a BLAS) and the TLAS that
    /// references it is rebuilt later in the same submission. Maps to
    /// `vkCmdPipelineBarrier2` with
    /// `srcStageMask = dstStageMask = ACCELERATION_STRUCTURE_BUILD_KHR`
    /// and `srcAccessMask = ACCELERATION_STRUCTURE_WRITE_KHR`,
    /// `dstAccessMask = ACCELERATION_STRUCTURE_READ_KHR |
    /// ACCELERATION_STRUCTURE_WRITE_KHR`. Backends without RT no-op.
    virtual void acceleration_structure_barrier() {}

    virtual void dispatch_rays(const DispatchRaysDesc& /*desc*/) {}

    // ---- GPU queries (A-QUERY, Backend-to-100 Wave 3a) ---------------------
    //
    // Record into a query pool created via IDevice::create_query_pool. The
    // pool MUST be reset (reset_query_pool, or the device's create-time reset)
    // before the first write in a submission — Vulkan requires every query be
    // reset before use, and the D3D12/Metal backends treat the reset as a
    // no-op so a single contract works on all three.
    //
    //   * write_timestamp — record the GPU clock at `index` (kTimestamp pool).
    //     Issued OUTSIDE begin_query/end_query (a timestamp is a point sample).
    //   * begin_query / end_query — bracket the work whose occlusion /
    //     pipeline-statistics counters land in slot `index` (kOcclusion /
    //     kPipelineStatistics pools).
    //   * reset_query_pool — clear `count` slots from `first` so they can be
    //     written again in this submission.
    //
    // Default no-ops so backends without a query path (and Null) compile
    // unchanged; the GPU backends override.
    virtual void write_timestamp(QueryPoolHandle /*pool*/, std::uint32_t /*index*/) {}
    virtual void begin_query(QueryPoolHandle /*pool*/, std::uint32_t /*index*/) {}
    virtual void end_query(QueryPoolHandle /*pool*/, std::uint32_t /*index*/) {}
    virtual void reset_query_pool(QueryPoolHandle /*pool*/,
                                  std::uint32_t /*first*/,
                                  std::uint32_t /*count*/) {}

    // ---- Test/diagnostic observability ------------------------------------
    /// Returns the current debug-group nesting depth for this recording.
    /// Primarily used by tests to assert that begin() resets the counter
    /// when a command buffer is recycled (phase1189 B5 fix).
    /// Default returns 0; backends that track depth override this.
    [[nodiscard]] virtual std::uint32_t debug_group_depth() const noexcept { return 0; }

    // ---- A-RESULT-DIAG (Backend-to-100 Wave 3c) ---------------------------
    /// RECORDING-ERROR CONTRACT. The recording API is `void` by design — a
    /// record call cannot return a Result because the GPU error is deferred to
    /// submit time. Historically a stale/invalid handle passed to a record call
    /// (bind_*, copy_*, dispatch, draw_*, ...) was a SILENT no-op: the call did
    /// nothing and nothing surfaced (the silent-failure class). This contract
    /// makes such a miss LOUD without changing the void signature:
    ///   * in a debug build (NDEBUG undefined) any lookup-miss in a recording
    ///     call fires an assert() at the miss site, and
    ///   * on EVERY build the per-command-buffer error flag below is latched
    ///     true, so a caller / test can query it after recording (the call
    ///     itself stays a safe no-op in release, never a crash).
    /// The flag mirrors the debug_group_depth() member pattern: latched on any
    /// recording-time lookup miss, reset in begin() so a recycled buffer starts
    /// clean, queryable here. Default false; backends that track it override.
    [[nodiscard]] virtual bool recording_error() const noexcept { return false; }
};

}  // namespace cd::rhi
