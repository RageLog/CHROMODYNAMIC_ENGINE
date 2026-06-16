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
};

}  // namespace cd::rhi
