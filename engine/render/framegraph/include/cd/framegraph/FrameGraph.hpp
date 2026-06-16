// =============================================================================
// CHROMODYNAMIC — cd/framegraph/FrameGraph.hpp
// Sprint S3.10 — render-tier frame graph.
//
// A frame graph is a declarative description of one frame's GPU work. The
// caller registers passes via `add_pass(...)`, declares which transient
// (frame-scoped) and imported (externally-owned) textures each pass reads
// and writes, and supplies an execute callback. `compile()` walks the
// declarations and:
//
//   1. Allocates transient textures from the device (created on first use,
//      destroyed when the graph is reset).
//   2. Computes per-pass resource-state transitions: every read/write
//      switches the texture's pre- and post-pass state, and the compiler
//      emits an `ICommandBuffer::barrier()` call before each pass to
//      take the resource from its current state to the one this pass
//      requires. Identity transitions are elided.
//
// `execute()` then drives the command buffer: barrier → pass.execute().
//
// Design notes (SOTA references: Granite [Sjölander 2019], O'Donnell's
// "FrameGraph: Extensible Rendering Architecture in Frostbite" [GDC 2017]):
//   * The MVP is *linear* — passes execute in registration order. A
//     topological sort over the DAG is a follow-up that lets the graph
//     reorder passes for resource reuse and prune unused passes.
//   * Resource lifetimes are per-pass; aliasing across non-overlapping
//     lifetimes is a follow-up (it requires a real DAG).
//   * No automatic pipeline-stage tracking yet: barriers use the coarse
//     `ResourceState` mapping that the RHI exposes today.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Barriers.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::rhi
{
class ICommandBuffer;
} // namespace cd::rhi

namespace cd::framegraph
{

// ---- Error domain -----------------------------------------------------------

namespace fg_errors
{
inline constexpr std::uint32_t kDomain = 0x000D;

enum class Code : std::uint32_t
{
    kOk = 0,
    kInvalidArgument = 1,
    kAllocationFailed = 2,
    kAlreadyCompiled = 3,
    kNotCompiled = 4,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace fg_errors

// ---- Resource handles ------------------------------------------------------

/// Resource handle issued by the graph. Opaque integer; the graph keeps the
/// authoritative descriptor + RHI handle mapping internally.
struct ResourceHandle
{
    std::uint32_t index { 0 };
    std::uint32_t generation { 0 };

    [[nodiscard]] bool is_valid() const noexcept
    {
        return generation != 0;
    }

    [[nodiscard]] friend bool operator==(ResourceHandle a, ResourceHandle b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
};

/// Description of a transient texture. The graph allocates and destroys
/// the underlying RHI texture; the lifetime is tied to FrameGraph::reset().
struct TransientTextureDesc
{
    cd::rhi::Format format { cd::rhi::Format::kRGBA8Unorm };
    cd::rhi::Extent3D extent { 1, 1, 1 };
    std::uint32_t mip_levels { 1 };
    std::uint32_t array_layers { 1 };
    cd::rhi::TextureType type { cd::rhi::TextureType::k2D };
    cd::rhi::TextureUsage usage { cd::rhi::TextureUsage::kColorAttachment };
};

/// Reference to an externally-owned texture (e.g. swapchain image). The
/// caller is responsible for the texture's lifetime; the graph only tracks
/// its state across passes.
struct ImportedTextureDesc
{
    cd::rhi::TextureHandle texture {};
    cd::rhi::ResourceState initial_state { cd::rhi::ResourceState::kUndefined };
    cd::rhi::ResourceState final_state { cd::rhi::ResourceState::kUndefined };
};

// ---- Pass description ------------------------------------------------------

struct PassResource
{
    ResourceHandle resource {};
    /// State this pass needs the resource in (e.g. kColorAttachment for a
    /// write to a render target, kShaderResource for a read).
    cd::rhi::ResourceState state { cd::rhi::ResourceState::kCommon };
};

using PassExecuteFn = std::function<void(cd::rhi::ICommandBuffer&)>;

struct PassDesc
{
    std::string_view name { "pass" };
    /// Resources this pass reads (texture must be in or transitioned to the
    /// declared `state` before execute()).
    std::span<const PassResource> reads {};
    /// Resources this pass writes (transitioned to `state`, post-pass state
    /// stays at `state` until a subsequent pass demands otherwise).
    std::span<const PassResource> writes {};
    /// Recording callback. Receives the command buffer with all required
    /// barriers already emitted; the callback should only record the actual
    /// draw/dispatch commands.
    PassExecuteFn execute {};
};

// ---- FrameGraph ------------------------------------------------------------

class FrameGraph
{
public:
    explicit FrameGraph(cd::rhi::IDevice& device) noexcept
        : device_ { &device }
    {
    }

    ~FrameGraph();
    FrameGraph(const FrameGraph&) = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;
    FrameGraph(FrameGraph&& other) noexcept;
    FrameGraph& operator=(FrameGraph&& other) noexcept;

    // ---- Resource declaration --------------------------------------------

    /// Declare a transient texture. Returns a graph-scoped handle that can be
    /// referenced in subsequent `add_pass` calls. The RHI texture is created
    /// during `compile()`.
    [[nodiscard]] ResourceHandle create_texture(const TransientTextureDesc& desc,
                                                std::string_view name = "transient");

    /// Import an externally-owned texture. The graph tracks state across
    /// passes; the texture itself is not created or destroyed by the graph.
    [[nodiscard]] ResourceHandle import_texture(const ImportedTextureDesc& desc,
                                                std::string_view name = "imported");

    // ---- Pass declaration -------------------------------------------------

    /// Register a pass. Passes execute in registration order (MVP).
    void add_pass(const PassDesc& desc);

    // ---- Instrumentation hook -------------------------------------------

    /// Per-pass CPU timing callback.  Delivered once per executed pass when
    /// set_instrumentation_callback() has been called with a non-empty fn.
    ///
    ///   pass_name       — label from PassDesc::name.
    ///   cpu_start_ms    — steady_clock offset from start of execute() (ms).
    ///   cpu_duration_ms — wall-clock time spent in the execute callback (ms).
    ///   pass_index      — 0-based index in registration order.
    ///
    /// Called synchronously on the execute() thread.
    using InstrumentationCallback =
        std::function<void(std::string_view pass_name,
                           double           cpu_start_ms,
                           double           cpu_duration_ms,
                           std::uint32_t    pass_index)>;

    /// Register (or replace) the per-pass timing callback.
    /// Passing a default-constructed function disables timing.
    void set_instrumentation_callback(InstrumentationCallback cb) noexcept
    {
        instrumentation_cb_ = std::move(cb);
    }

    // ---- Optimization toggles --------------------------------------------

    /// Enable dead-pass culling (phase: band2-render-core).
    ///
    /// When enabled, `compile()` prunes passes whose written resources are
    /// never consumed by a "live" sink — i.e. a pass contributes to the frame
    /// output ONLY if (a) it writes an imported resource with a meaningful
    /// `final_state` (the swapchain / an externally-observed target), or (b)
    /// one of its written resources is later read or written by another live
    /// pass (transitive backward reachability over the read-after-write
    /// dependency graph). Passes that fail both are dropped before execute().
    ///
    /// DEFAULT: OFF. Off preserves the original "execute every registered
    /// pass in order" v1 contract byte-for-byte (no consumer/golden change).
    /// A pass with NO declared writes is always kept (its side effects —
    /// clears, queries, copies expressed through the execute callback — are
    /// not visible to the write-dependency analysis, so culling it would be
    /// unsound). Toggle BEFORE compile(); calling after compile() has no
    /// effect on the already-pruned set.
    void set_dead_pass_culling(bool enabled) noexcept
    {
        dead_pass_culling_ = enabled;
    }

    [[nodiscard]] bool dead_pass_culling_enabled() const noexcept
    {
        return dead_pass_culling_;
    }

    // ---- Compile / Execute -----------------------------------------------

    /// Materialize transient resources. Must be called once before execute().
    [[nodiscard]] cd::core::Result<void> compile();

    /// Drive the command buffer: for each registered pass, emit any required
    /// barriers and invoke the execute callback. The command buffer must
    /// already be in the recording state.
    [[nodiscard]] cd::core::Result<void> execute(cd::rhi::ICommandBuffer& cmd);

    /// Release transient textures and reset bookkeeping. Resource handles
    /// from before the reset become invalid.
    void reset();

    // ---- Introspection (for tests) ---------------------------------------

    [[nodiscard]] std::size_t pass_count() const noexcept
    {
        return passes_.size();
    }

    [[nodiscard]] std::size_t resource_count() const noexcept
    {
        return resources_.size();
    }

    /// Passes dropped by dead-pass culling during the last compile(). Zero
    /// when culling is disabled or every pass was live. (Introspection for
    /// tests / profiling overlays.)
    [[nodiscard]] std::size_t culled_pass_count() const noexcept
    {
        return culled_pass_count_;
    }

    /// Resolve a graph handle to its underlying RHI texture (post-compile).
    [[nodiscard]] cd::rhi::TextureHandle texture_handle(ResourceHandle h) const noexcept;
    /// Last-known resource state (after the most recent execute()).
    [[nodiscard]] cd::rhi::ResourceState current_state(ResourceHandle h) const noexcept;

private:
    void release_() noexcept;

    enum class ResourceKind : std::uint8_t
    {
        kTransient,
        kImported
    };

    struct Resource
    {
        ResourceKind kind { ResourceKind::kTransient };
        TransientTextureDesc transient_desc {};
        ImportedTextureDesc imported_desc {};
        cd::rhi::TextureHandle rhi_handle {};
        cd::rhi::ResourceState state { cd::rhi::ResourceState::kUndefined };
        cd::rhi::ResourceState final_state { cd::rhi::ResourceState::kUndefined };
        std::string name {};
        std::uint32_t generation { 0 };
        /// True for transient resources that the graph owns and must destroy.
        bool owned_by_graph { false };
    };

    struct Pass
    {
        std::string name {};
        std::vector<PassResource> reads {};
        std::vector<PassResource> writes {};
        PassExecuteFn execute {};
    };

    /// Backward-reachability dead-pass cull. Returns the surviving passes in
    /// their original registration order; `culled_pass_count_` is updated.
    /// Pure (no device touch); operates on `passes_` + `resources_`.
    void cull_dead_passes_();

    cd::rhi::IDevice*       device_ { nullptr };
    std::vector<Resource>   resources_ {};
    std::vector<Pass>       passes_ {};
    InstrumentationCallback instrumentation_cb_ {};
    bool                    compiled_ { false };
    bool                    dead_pass_culling_ { false };
    std::size_t             culled_pass_count_ { 0 };
    std::uint32_t           next_generation_ { 1 };
};

}  // namespace cd::framegraph
