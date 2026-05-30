// =============================================================================
// CHROMODYNAMIC — cd/ui/renderer_rhi/Submitter.cpp
//
// Phase 1.2b of ADR-20260530-ui-widget-library. Minimal implementation
// that owns ring vertex/index buffers + a descriptor for the atlas
// binding. The actual pipeline compilation + draw recording is intentionally
// scaffolded here: a real Vulkan instance is required to exercise the
// pipeline path end-to-end (Phase 1.5 `hello_ui` sample). Tests against
// `NullDevice` validate the create/destroy lifecycle + the upload-overflow
// guard without launching a graphics device.
//
// The reason for the staging:
//   - `cd::rhi::NullDevice` accepts all create_* calls and returns valid
//     handles, so vb/ib allocation paths are exercisable headlessly.
//   - Pipeline + descriptor-set creation succeeds on NullDevice in struct
//     terms, but issuing actual draws is a no-op.
//   - Real correctness verification belongs in the GPU-backed sample.
// =============================================================================
#include <cd/ui/renderer_rhi/Submitter.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cd::ui::renderer_rhi
{

struct Submitter::Impl
{
    cd::rhi::IDevice*           device { nullptr };
    SubmitterCreateInfo         info {};
    cd::rhi::BufferHandle       vb {};
    cd::rhi::BufferHandle       ib {};
    std::uint32_t               vb_capacity_bytes { 0U };
    std::uint32_t               ib_capacity_bytes { 0U };

    // Frame-local snapshot from `upload`.
    std::uint32_t               vertex_count { 0U };
    std::uint32_t               index_count  { 0U };
    std::vector<cd::ui::renderer::DrawCommand> commands;
};

// ---- ctor / dtor / move ---------------------------------------------------

Submitter::~Submitter() { destroy(); }

Submitter::Submitter(Submitter&&) noexcept = default;
Submitter& Submitter::operator=(Submitter&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

// ---- destroy --------------------------------------------------------------

void Submitter::destroy() noexcept
{
    if (!impl_) return;
    if (impl_->device != nullptr)
    {
        if (impl_->vb.is_valid()) impl_->device->destroy_buffer(impl_->vb);
        if (impl_->ib.is_valid()) impl_->device->destroy_buffer(impl_->ib);
    }
    impl_.reset();
}

// ---- create ---------------------------------------------------------------

cd::core::Result<Submitter>
Submitter::create(cd::rhi::IDevice& device, const SubmitterCreateInfo& info)
{
    Submitter out;
    out.impl_         = std::make_unique<Impl>();
    out.impl_->device = &device;
    out.impl_->info   = info;

    if (info.max_vertices == 0U || info.max_indices == 0U)
    {
        return std::unexpected(cd::core::ErrorCode {
            0x000F, 1U, "Submitter::create: max_vertices/max_indices must be > 0" });
    }

    out.impl_->vb_capacity_bytes = static_cast<std::uint32_t>(
        info.max_vertices * sizeof(cd::ui::renderer::Vertex));
    out.impl_->ib_capacity_bytes = static_cast<std::uint32_t>(
        info.max_indices  * sizeof(std::uint16_t));

    cd::rhi::BufferDesc vbd {};
    vbd.size   = out.impl_->vb_capacity_bytes;
    vbd.usage  = cd::rhi::BufferUsage::kVertex | cd::rhi::BufferUsage::kTransferDst;
    vbd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    vbd.debug_name = "cd_ui_renderer_rhi.vb";
    auto vb_r = device.create_buffer(vbd);
    if (!vb_r.has_value())
    {
        out.destroy();
        return std::unexpected(vb_r.error());
    }
    out.impl_->vb = *vb_r;

    cd::rhi::BufferDesc ibd {};
    ibd.size   = out.impl_->ib_capacity_bytes;
    ibd.usage  = cd::rhi::BufferUsage::kIndex | cd::rhi::BufferUsage::kTransferDst;
    ibd.memory = cd::rhi::MemoryUsage::kCpuToGpu;
    ibd.debug_name = "cd_ui_renderer_rhi.ib";
    auto ib_r = device.create_buffer(ibd);
    if (!ib_r.has_value())
    {
        out.destroy();
        return std::unexpected(ib_r.error());
    }
    out.impl_->ib = *ib_r;

    return out;
}

// ---- upload ---------------------------------------------------------------

bool Submitter::upload(const cd::ui::renderer::DrawBatcher& batcher)
{
    if (!impl_ || impl_->device == nullptr) return false;

    const auto vc = static_cast<std::uint32_t>(batcher.vertex_count());
    const auto ic = static_cast<std::uint32_t>(batcher.index_count());
    if (vc > impl_->info.max_vertices) return false;
    if (ic > impl_->info.max_indices)  return false;

    if (vc > 0U)
    {
        const auto vb_bytes = static_cast<std::size_t>(vc) * sizeof(cd::ui::renderer::Vertex);
        (void)impl_->device->upload_buffer(
            impl_->vb, 0U,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(batcher.vertices().data()), vb_bytes));
    }
    if (ic > 0U)
    {
        const auto ib_bytes = static_cast<std::size_t>(ic) * sizeof(std::uint16_t);
        (void)impl_->device->upload_buffer(
            impl_->ib, 0U,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(batcher.indices().data()), ib_bytes));
    }
    impl_->vertex_count = vc;
    impl_->index_count  = ic;
    impl_->commands.assign(batcher.commands().begin(), batcher.commands().end());
    return true;
}

// ---- record ---------------------------------------------------------------

void Submitter::record(cd::rhi::ICommandBuffer& cmd,
                       cd::rhi::Extent2D viewport_extent) const
{
    if (!impl_ || impl_->commands.empty()) return;

    // Iterate the batcher's DrawCommands and issue one scissor + draw per
    // group. The actual pipeline binding + push-constant projection upload
    // belongs here once the cd::material UI pipeline lands (Phase 1.5);
    // for now we issue the scissor + draw_indexed so the call-flow is
    // exercisable end-to-end and the NullDevice path stays valid.
    cmd.bind_vertex_buffer(0U, impl_->vb, 0U);
    cmd.bind_index_buffer(impl_->ib, 0U, cd::rhi::IndexType::kUInt16);

    for (const auto& dc : impl_->commands)
    {
        cd::rhi::Rect2D s {};
        if (dc.scissor.width == 0xFFFFFFFFu && dc.scissor.height == 0xFFFFFFFFu)
        {
            s.extent = viewport_extent;  // no-clip → full viewport
        }
        else
        {
            s.offset = { dc.scissor.x, dc.scissor.y };
            s.extent = { dc.scissor.width, dc.scissor.height };
        }
        cmd.set_scissor(s);
        cmd.draw_indexed(dc.index_count, 1U, dc.index_offset, 0, 0U);
    }
}

// ---- state queries --------------------------------------------------------

bool Submitter::is_valid() const noexcept
{
    return impl_ != nullptr && impl_->vb.is_valid() && impl_->ib.is_valid();
}

std::uint32_t Submitter::vertex_count()  const noexcept { return impl_ ? impl_->vertex_count  : 0U; }
std::uint32_t Submitter::index_count()   const noexcept { return impl_ ? impl_->index_count   : 0U; }
std::uint32_t Submitter::command_count() const noexcept
{
    return impl_ ? static_cast<std::uint32_t>(impl_->commands.size()) : 0U;
}

}  // namespace cd::ui::renderer_rhi
