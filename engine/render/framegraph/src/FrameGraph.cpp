// =============================================================================
// CHROMODYNAMIC — cd/framegraph/FrameGraph.cpp
// =============================================================================
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cd::framegraph
{

namespace
{

/// Helper to write a TextureBarrier in the form barrier() expects (a
/// single-slice subresource, color aspect implied by the RHI for now).
[[nodiscard]] cd::rhi::TextureBarrier
make_barrier(cd::rhi::TextureHandle tex,
             cd::rhi::ResourceState from,
             cd::rhi::ResourceState to) noexcept
{
    cd::rhi::TextureBarrier b {};
    b.texture = tex;
    b.from    = from;
    b.to      = to;
    b.range   = cd::rhi::TextureSubresourceRange { 0, 1, 0, 1 };
    return b;
}

}  // namespace

// ---- Move / destroy --------------------------------------------------------

FrameGraph::~FrameGraph()
{
    release_();
}

FrameGraph::FrameGraph(FrameGraph&& other) noexcept
    : device_ { other.device_ }
    , resources_ { std::move(other.resources_) }
    , passes_ { std::move(other.passes_) }
    , instrumentation_cb_ { std::move(other.instrumentation_cb_) }
    , compiled_ { other.compiled_ }
    , dead_pass_culling_ { other.dead_pass_culling_ }
    , culled_pass_count_ { other.culled_pass_count_ }
    , next_generation_ { other.next_generation_ }
{
    other.device_        = nullptr;
    other.compiled_      = false;
}

FrameGraph& FrameGraph::operator=(FrameGraph&& other) noexcept
{
    if (this != &other)
    {
        release_();
        device_              = other.device_;
        resources_           = std::move(other.resources_);
        passes_              = std::move(other.passes_);
        instrumentation_cb_  = std::move(other.instrumentation_cb_);
        compiled_            = other.compiled_;
        dead_pass_culling_   = other.dead_pass_culling_;
        culled_pass_count_   = other.culled_pass_count_;
        next_generation_     = other.next_generation_;
        other.device_        = nullptr;
        other.compiled_      = false;
    }
    return *this;
}

void FrameGraph::release_() noexcept
{
    if (device_ == nullptr)
        return;
    for (auto& r : resources_)
    {
        if (r.owned_by_graph && r.rhi_handle.is_valid())
        {
            device_->destroy_texture(r.rhi_handle);
        }
    }
    resources_.clear();
    passes_.clear();
    compiled_ = false;
}

// ---- Resource declaration --------------------------------------------------

ResourceHandle FrameGraph::create_texture(const TransientTextureDesc& desc,
                                          std::string_view name)
{
    Resource r;
    r.kind          = ResourceKind::kTransient;
    r.transient_desc = desc;
    r.state         = cd::rhi::ResourceState::kUndefined;
    r.final_state   = cd::rhi::ResourceState::kUndefined;
    r.name.assign(name);
    r.generation    = next_generation_++;
    r.owned_by_graph = true;
    const auto idx  = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back(std::move(r));
    return ResourceHandle { idx, resources_[idx].generation };
}

ResourceHandle FrameGraph::import_texture(const ImportedTextureDesc& desc,
                                          std::string_view name)
{
    Resource r;
    r.kind          = ResourceKind::kImported;
    r.imported_desc  = desc;
    r.rhi_handle     = desc.texture;
    r.state         = desc.initial_state;
    r.final_state   = desc.final_state;
    r.name.assign(name);
    r.generation    = next_generation_++;
    r.owned_by_graph = false;
    const auto idx  = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back(std::move(r));
    return ResourceHandle { idx, resources_[idx].generation };
}

void FrameGraph::add_pass(const PassDesc& desc)
{
    Pass p;
    p.name.assign(desc.name);
    p.reads.assign(desc.reads.begin(), desc.reads.end());
    p.writes.assign(desc.writes.begin(), desc.writes.end());
    p.execute = desc.execute;
    passes_.push_back(std::move(p));
}

// ---- Dead-pass culling -----------------------------------------------------

void FrameGraph::cull_dead_passes_()
{
    culled_pass_count_ = 0;
    if (passes_.empty())
        return;

    const std::size_t n = passes_.size();

    // A resource index is a "sink" if it is an imported resource with a
    // meaningful final_state (externally observed, e.g. the swapchain). Any
    // pass that writes a sink resource is unconditionally live, and so is any
    // pass whose output another live pass consumes.
    auto resource_is_sink = [this](std::uint32_t idx) -> bool
    {
        if (idx >= resources_.size())
            return false;
        const auto& r = resources_[idx];
        return r.kind == ResourceKind::kImported &&
               r.final_state != cd::rhi::ResourceState::kUndefined;
    };

    // For each resource, the ordered list of passes that read it and that
    // write it (registration order). Used for backward reachability.
    const std::size_t res_n = resources_.size();
    std::vector<std::vector<std::size_t>> writers(res_n);
    std::vector<std::vector<std::size_t>> readers(res_n);
    for (std::size_t p = 0; p < n; ++p)
    {
        for (const auto& w : passes_[p].writes)
        {
            const auto i = w.resource.index;
            if (i < res_n && resources_[i].generation == w.resource.generation)
                writers[i].push_back(p);
        }
        for (const auto& rd : passes_[p].reads)
        {
            const auto i = rd.resource.index;
            if (i < res_n && resources_[i].generation == rd.resource.generation)
                readers[i].push_back(p);
        }
    }

    std::vector<bool> live(n, false);
    std::vector<std::size_t> work;

    // Seed: a pass is live if it has no declared writes (its execute callback
    // may carry untracked side effects — clears/queries/copies — so culling it
    // is unsound), or if it writes a sink resource.
    for (std::size_t p = 0; p < n; ++p)
    {
        bool seed = passes_[p].writes.empty();
        if (!seed)
        {
            for (const auto& w : passes_[p].writes)
            {
                if (resource_is_sink(w.resource.index))
                {
                    seed = true;
                    break;
                }
            }
        }
        if (seed)
        {
            live[p] = true;
            work.push_back(p);
        }
    }

    // Backward reachability: a producer feeding a live consumer is live.
    // For every resource a live pass READS, mark all passes that WRITE that
    // resource live (a write-before-read producer). For every resource a live
    // pass WRITES, mark earlier writers live too (read-modify-write chains
    // where a later live pass depends on the accumulated content).
    while (!work.empty())
    {
        const std::size_t p = work.back();
        work.pop_back();

        auto promote = [&](std::size_t producer)
        {
            if (!live[producer])
            {
                live[producer] = true;
                work.push_back(producer);
            }
        };

        for (const auto& rd : passes_[p].reads)
        {
            const auto i = rd.resource.index;
            if (i >= res_n)
                continue;
            for (const std::size_t producer : writers[i])
                promote(producer);
        }
        for (const auto& w : passes_[p].writes)
        {
            const auto i = w.resource.index;
            if (i >= res_n)
                continue;
            for (const std::size_t producer : writers[i])
                if (producer < p)  // earlier writer feeds this RMW
                    promote(producer);
        }
    }

    // Compact: keep live passes in original order.
    std::vector<Pass> kept;
    kept.reserve(n);
    for (std::size_t p = 0; p < n; ++p)
    {
        if (live[p])
            kept.push_back(std::move(passes_[p]));
        else
            ++culled_pass_count_;
    }
    passes_ = std::move(kept);
}

// ---- Compile --------------------------------------------------------------

cd::core::Result<void> FrameGraph::compile()
{
    if (device_ == nullptr)
    {
        return std::unexpected(
            fg_errors::make(fg_errors::Code::kInvalidArgument,
                            "FrameGraph: no device"));
    }
    if (compiled_)
    {
        return std::unexpected(
            fg_errors::make(fg_errors::Code::kAlreadyCompiled,
                            "FrameGraph::compile twice"));
    }

    // Dead-pass culling (opt-in; default OFF preserves the v1 "run every
    // registered pass" contract). Runs before transient allocation so the
    // RT-ordering invariant below sees only the surviving passes.
    if (dead_pass_culling_)
        cull_dead_passes_();

    // Allocate transient textures.
    for (auto& r : resources_)
    {
        if (r.kind != ResourceKind::kTransient)
            continue;
        cd::rhi::TextureDesc td {};
        td.type         = r.transient_desc.type;
        td.format       = r.transient_desc.format;
        td.extent       = r.transient_desc.extent;
        td.mip_levels   = r.transient_desc.mip_levels;
        td.array_layers = r.transient_desc.array_layers;
        td.usage        = r.transient_desc.usage;
        td.memory       = cd::rhi::MemoryUsage::kGpuOnly;
        auto t = device_->create_texture(td);
        if (!t.has_value())
        {
            return std::unexpected(
                fg_errors::make(
                    fg_errors::Code::kAllocationFailed,
                    std::string { "transient texture '" } + r.name +
                        "' creation failed: " +
                        std::string { t.error().message }));
        }
        r.rhi_handle = *t;
    }

    // [T1.13] RT phase ordering invariant.
#if CHROMA_DEBUG
    std::int32_t tlas_rebuild_index { -1 };
    std::int32_t rt_trace_index { -1 };
    for (std::uint32_t i = 0; i < passes_.size(); ++i)
    {
        const auto& pass_name = passes_[i].name;
        bool is_rebuild = pass_name.find("rebuild") != std::string::npos ||
                          pass_name.find("TLAS") != std::string::npos;
        bool is_trace   = pass_name.find("trace") != std::string::npos;

        if (is_rebuild && tlas_rebuild_index == -1)
            tlas_rebuild_index = static_cast<std::int32_t>(i);
        if (is_trace && rt_trace_index == -1)
            rt_trace_index = static_cast<std::int32_t>(i);
    }
    if (tlas_rebuild_index != -1 && rt_trace_index != -1)
    {
        CHROMA_ASSERT(tlas_rebuild_index < rt_trace_index,
                      "FrameGraph: RT trace pass must follow TLAS rebuild pass. "
                      "Trace at index %d, rebuild at index %d.",
                      rt_trace_index, tlas_rebuild_index);
    }
#endif

    compiled_ = true;
    return {};
}

// ---- Execute --------------------------------------------------------------

cd::core::Result<void> FrameGraph::execute(cd::rhi::ICommandBuffer& cmd)
{
    if (!compiled_)
    {
        return std::unexpected(
            fg_errors::make(fg_errors::Code::kNotCompiled,
                            "FrameGraph::execute before compile"));
    }

    // Per-pass barrier emission + optional CPU instrumentation.
    //
    // When instrumentation_cb_ is non-empty, each pass is bracketed with
    // steady_clock timestamps.  The overhead is two Clock::now() calls per
    // pass — negligible compared to real render work.
    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double, std::milli>;

    const bool        instrument    = static_cast<bool>(instrumentation_cb_);
    const auto        execute_epoch = Clock::now();  // reference for start_ms
    std::uint32_t     pass_index    = 0U;

    for (const auto& pass : passes_)
    {
        std::vector<cd::rhi::TextureBarrier> barriers;
        barriers.reserve(pass.reads.size() + pass.writes.size());

        auto process = [&](const PassResource& pr)
        {
            if (!pr.resource.is_valid())
                return;
            if (pr.resource.index >= resources_.size())
                return;
            auto& res = resources_[pr.resource.index];
            if (res.generation != pr.resource.generation)
                return;
            if (res.state == pr.state)
                return;  // identity transition — elide
            barriers.push_back(make_barrier(res.rhi_handle, res.state, pr.state));
            res.state = pr.state;
        };

        for (const auto& r : pass.reads)
            process(r);
        for (const auto& w : pass.writes)
            process(w);

        if (!barriers.empty())
            cmd.barrier({}, barriers);

        if (pass.execute)
        {
            if (instrument)
            {
                const auto t0        = Clock::now();
                pass.execute(cmd);
                const auto t1        = Clock::now();
                const double start   = Duration(t0 - execute_epoch).count();
                const double dur     = Duration(t1 - t0).count();
                instrumentation_cb_(pass.name, start, dur, pass_index);
            }
            else
            {
                pass.execute(cmd);
            }
        }
        ++pass_index;
    }

    // Final imported-resource state transitions.
    std::vector<cd::rhi::TextureBarrier> finalize;
    for (auto& res : resources_)
    {
        if (res.kind != ResourceKind::kImported)
            continue;
        if (res.final_state == cd::rhi::ResourceState::kUndefined)
            continue;
        if (res.state == res.final_state)
            continue;
        finalize.push_back(make_barrier(res.rhi_handle, res.state, res.final_state));
        res.state = res.final_state;
    }
    if (!finalize.empty())
        cmd.barrier({}, finalize);

    return {};
}

void FrameGraph::reset()
{
    release_();
}

// ---- Introspection --------------------------------------------------------

cd::rhi::TextureHandle FrameGraph::texture_handle(ResourceHandle h) const noexcept
{
    if (h.index >= resources_.size())
        return {};
    const auto& r = resources_[h.index];
    if (r.generation != h.generation)
        return {};
    return r.rhi_handle;
}

cd::rhi::ResourceState FrameGraph::current_state(ResourceHandle h) const noexcept
{
    if (h.index >= resources_.size())
        return cd::rhi::ResourceState::kUndefined;
    const auto& r = resources_[h.index];
    if (r.generation != h.generation)
        return cd::rhi::ResourceState::kUndefined;
    return r.state;
}

}  // namespace cd::framegraph
