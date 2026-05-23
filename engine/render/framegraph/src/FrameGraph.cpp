// =============================================================================
// CHROMODYNAMIC — cd/framegraph/FrameGraph.cpp
// =============================================================================
#include <cd/framegraph/FrameGraph.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <array>
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
make_barrier(cd::rhi::TextureHandle tex, cd::rhi::ResourceState from, cd::rhi::ResourceState to) noexcept
{
    cd::rhi::TextureBarrier b {};
    b.texture = tex;
    b.from = from;
    b.to = to;
    b.range = cd::rhi::TextureSubresourceRange { 0, 1, 0, 1 };
    return b;
}

}  // namespace

// ---- Move / destroy --------------------------------------------------------

FrameGraph::~FrameGraph()
{
    release_();
}

FrameGraph::FrameGraph(FrameGraph&& other) noexcept
{
    device_ = other.device_;
    resources_ = std::move(other.resources_);
    passes_ = std::move(other.passes_);
    compiled_ = other.compiled_;
    next_generation_ = other.next_generation_;
    other.device_ = nullptr;
    other.compiled_ = false;
}

FrameGraph& FrameGraph::operator=(FrameGraph&& other) noexcept
{
    if (this != &other)
    {
        release_();
        device_ = other.device_;
        resources_ = std::move(other.resources_);
        passes_ = std::move(other.passes_);
        compiled_ = other.compiled_;
        next_generation_ = other.next_generation_;
        other.device_ = nullptr;
        other.compiled_ = false;
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

ResourceHandle FrameGraph::create_texture(const TransientTextureDesc& desc, std::string_view name)
{
    Resource r;
    r.kind = ResourceKind::kTransient;
    r.transient_desc = desc;
    r.state = cd::rhi::ResourceState::kUndefined;
    r.final_state = cd::rhi::ResourceState::kUndefined;
    r.name.assign(name);
    r.generation = next_generation_++;
    r.owned_by_graph = true;
    const auto idx = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back(std::move(r));
    return ResourceHandle { idx, resources_[idx].generation };
}

ResourceHandle FrameGraph::import_texture(const ImportedTextureDesc& desc, std::string_view name)
{
    Resource r;
    r.kind = ResourceKind::kImported;
    r.imported_desc = desc;
    r.rhi_handle = desc.texture;
    r.state = desc.initial_state;
    r.final_state = desc.final_state;
    r.name.assign(name);
    r.generation = next_generation_++;
    r.owned_by_graph = false;
    const auto idx = static_cast<std::uint32_t>(resources_.size());
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

// ---- Compile --------------------------------------------------------------

cd::core::Result<void> FrameGraph::compile()
{
    if (device_ == nullptr)
    {
        return std::unexpected(fg_errors::make(fg_errors::Code::kInvalidArgument, "FrameGraph: no device"));
    }
    if (compiled_)
    {
        return std::unexpected(fg_errors::make(fg_errors::Code::kAlreadyCompiled, "FrameGraph::compile twice"));
    }

    // Allocate transient textures. Imported textures already point at a real
    // VkImage; transient ones are created lazily here so the user can build
    // up the declaration phase without touching the GPU.
    for (auto& r : resources_)
    {
        if (r.kind != ResourceKind::kTransient)
            continue;
        cd::rhi::TextureDesc td {};
        td.type = r.transient_desc.type;
        td.format = r.transient_desc.format;
        td.extent = r.transient_desc.extent;
        td.mip_levels = r.transient_desc.mip_levels;
        td.array_layers = r.transient_desc.array_layers;
        td.usage = r.transient_desc.usage;
        td.memory = cd::rhi::MemoryUsage::kGpuOnly;
        auto t = device_->create_texture(td);
        if (!t.has_value())
        {
            return std::unexpected(
                fg_errors::make(
                    fg_errors::Code::kAllocationFailed,
                    std::string { "transient texture '" } + r.name +
                        "' creation failed: " + std::string { t.error().message }
                )
            );
        }
        r.rhi_handle = *t;
    }
    compiled_ = true;
    return {};
}

// ---- Execute --------------------------------------------------------------

cd::core::Result<void> FrameGraph::execute(cd::rhi::ICommandBuffer& cmd)
{
    if (!compiled_)
    {
        return std::unexpected(fg_errors::make(fg_errors::Code::kNotCompiled, "FrameGraph::execute before compile"));
    }

    // Per-pass barrier emission. We gather all transitions a pass requires
    // into one vector and emit a single vkCmdPipelineBarrier2 per pass —
    // batching matters on tiled GPUs.
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
                return;  // identity transition — elide.
            barriers.push_back(make_barrier(res.rhi_handle, res.state, pr.state));
            res.state = pr.state;
        };
        for (const auto& r : pass.reads)
            process(r);
        for (const auto& w : pass.writes)
            process(w);

        if (!barriers.empty())
        {
            cmd.barrier({}, barriers);
        }
        if (pass.execute)
        {
            pass.execute(cmd);
        }
    }

    // Final transition for imported resources: take them to their declared
    // post-graph state (e.g. PRESENT for the swapchain image). Transient
    // resources have no post-graph state — they're about to be destroyed.
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
    {
        cmd.barrier({}, finalize);
    }
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
