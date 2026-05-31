// =============================================================================
// CHROMODYNAMIC — cd/ui/renderer_webgpu/Submitter.cpp
//
// Phase 5 of ADR-20260530-ui-widget-library (phase522).
//
// Implementation strategy
// -----------------------
// This file provides TWO compile-time backends controlled by the
// CD_UI_WEBGPU_HAVE_DAWN preprocessor symbol (injected by CMake):
//
//   CD_UI_WEBGPU_HAVE_DAWN == 0  (default, no Dawn installed)
//     Pure stub: all GPU calls are no-ops.  Vertex/index counts are
//     tracked so the four build-only tests can verify the API surface
//     compiles + the lifecycle is correct without a physical WebGPU
//     device or the Dawn library.
//
//   CD_UI_WEBGPU_HAVE_DAWN == 1  (opt-in via -DCD_ENABLE_WEBGPU=ON)
//     Real Dawn backend: wgpu::Device::CreateBuffer allocates the ring
//     vertex/index buffers; wgpu::Queue::WriteBuffer performs the per-
//     frame upload; the RenderPassEncoder issues SetVertexBuffer +
//     SetIndexBuffer + DrawIndexed per DrawCommand.
//     Pipeline (VS + FS) compilation is scaffolded — the WGSL shaders
//     exist in the Phase 5 shell but are not wired until hello_ui_webgpu
//     (Phase 5.5) lands with a real surface.
//
// Rationale for the stub-first approach (same as renderer_rhi Phase 1.2b):
//   - Dawn is a heavy dependency (~800 MB build) that requires Abseil +
//     Python; many CI runners lack it.
//   - The stub lets all four lifecycle tests compile and run on every
//     platform / CI tier without Dawn.
//   - When Dawn IS installed the same test binary links the real backend
//     — no source change required (CMake flips CD_UI_WEBGPU_HAVE_DAWN).
// =============================================================================

#include <cd/ui/renderer_webgpu/Submitter.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#if CD_UI_WEBGPU_HAVE_DAWN
// Dawn real backend headers already pulled in via Submitter.hpp
#endif

namespace cd::ui::renderer_webgpu
{

// ===========================================================================
// Impl — private storage
// ===========================================================================

struct Submitter::Impl
{
    WgpuDevice           device {};
    SubmitterCreateInfo  info   {};

    bool                 valid          { false };
    std::uint32_t        vertex_count   { 0U };
    std::uint32_t        index_count    { 0U };

    // Frame-local snapshot from upload().
    std::vector<cd::ui::renderer::DrawCommand> commands;

#if CD_UI_WEBGPU_HAVE_DAWN
    // Dawn real handles.
    wgpu::Buffer vb {};
    wgpu::Buffer ib {};
#endif
};

// ===========================================================================
// Ctor / dtor / move
// ===========================================================================

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

// ===========================================================================
// destroy
// ===========================================================================

void Submitter::destroy() noexcept
{
    if (!impl_) return;

#if CD_UI_WEBGPU_HAVE_DAWN
    if (impl_->vb) { impl_->vb.Destroy(); impl_->vb = {}; }
    if (impl_->ib) { impl_->ib.Destroy(); impl_->ib = {}; }
#endif

    impl_.reset();
}

// ===========================================================================
// create
// ===========================================================================

cd::core::Result<Submitter>
Submitter::create(WgpuDevice device, const SubmitterCreateInfo& info)
{
    if (info.max_vertices == 0U || info.max_indices == 0U)
    {
        return std::unexpected(cd::core::ErrorCode {
            0x0010, 1U,
            "Submitter::create: max_vertices and max_indices must be > 0" });
    }

    Submitter out;
    out.impl_         = std::make_unique<Impl>();
    out.impl_->device = device;
    out.impl_->info   = info;

#if CD_UI_WEBGPU_HAVE_DAWN
    // --- Real Dawn path ---------------------------------------------------
    // Dawn's wgpu::Device::CreateBuffer takes a wgpu::BufferDescriptor.
    {
        wgpu::BufferDescriptor vbd {};
        vbd.label = "cd_ui_webgpu.vb";
        vbd.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        vbd.size  = static_cast<std::uint64_t>(info.max_vertices)
                    * sizeof(cd::ui::renderer::Vertex);
        out.impl_->vb = device.CreateBuffer(&vbd);
        if (!out.impl_->vb)
        {
            return std::unexpected(cd::core::ErrorCode {
                0x0010, 2U, "Submitter::create: CreateBuffer(vb) failed" });
        }
    }
    {
        wgpu::BufferDescriptor ibd {};
        ibd.label = "cd_ui_webgpu.ib";
        ibd.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        ibd.size  = static_cast<std::uint64_t>(info.max_indices)
                    * sizeof(std::uint16_t);
        out.impl_->ib = device.CreateBuffer(&ibd);
        if (!out.impl_->ib)
        {
            return std::unexpected(cd::core::ErrorCode {
                0x0010, 3U, "Submitter::create: CreateBuffer(ib) failed" });
        }
    }
    // Pipeline/shader module creation deferred to Phase 5.5 hello_ui_webgpu
    // sample which has a real swapchain surface to drive create-validation.
#endif  // CD_UI_WEBGPU_HAVE_DAWN

    // Stub and real paths both land here.
    out.impl_->valid = true;
    return out;
}

// ===========================================================================
// upload
// ===========================================================================

bool Submitter::upload(const cd::ui::renderer::DrawBatcher& batcher)
{
    if (!impl_ || !impl_->valid) return false;

    const auto vc = static_cast<std::uint32_t>(batcher.vertex_count());
    const auto ic = static_cast<std::uint32_t>(batcher.index_count());

    if (vc > impl_->info.max_vertices) return false;
    if (ic > impl_->info.max_indices)  return false;

#if CD_UI_WEBGPU_HAVE_DAWN
    // Real path: WriteBuffer into ring buffers via the device queue.
    wgpu::Queue queue = impl_->device.GetQueue();
    if (vc > 0U)
    {
        const auto vb_bytes = static_cast<std::size_t>(vc)
                              * sizeof(cd::ui::renderer::Vertex);
        queue.WriteBuffer(impl_->vb, 0U,
            batcher.vertices().data(), vb_bytes);
    }
    if (ic > 0U)
    {
        const auto ib_bytes = static_cast<std::size_t>(ic)
                              * sizeof(std::uint16_t);
        queue.WriteBuffer(impl_->ib, 0U,
            batcher.indices().data(), ib_bytes);
    }
#endif  // CD_UI_WEBGPU_HAVE_DAWN

    // Snapshot metadata (both stub and real).
    impl_->vertex_count = vc;
    impl_->index_count  = ic;
    impl_->commands.assign(batcher.commands().begin(), batcher.commands().end());
    return true;
}

// ===========================================================================
// record
// ===========================================================================

void Submitter::record(WgpuCommandEncoder encoder,
                       Extent2D           viewport_extent) const
{
    if (!impl_ || impl_->commands.empty()) return;

    (void)viewport_extent;   // used below in real path

#if CD_UI_WEBGPU_HAVE_DAWN
    // Real Dawn path: bind the vertex/index ring buffers and issue one
    // DrawIndexed per DrawCommand.
    //
    // The render pass must already be open on `encoder` — WebGPU encodes
    // BeginRenderPass on the CommandEncoder (not inline on a queue), so the
    // caller is responsible for begin/end.  The encoder is passed in already
    // inside an active pass so that Submitter stays stateless w.r.t. the
    // colour attachment and load/store ops (which differ between overlay and
    // full-screen UI modes).
    //
    // Pipeline-set is deferred to Phase 5.5 hello_ui_webgpu where a real
    // swapchain surface validates the PSO; here we emit the pure
    // geometry-binding + draw sequence so integration tests can verify the
    // call count without a real GPU.
    {
        // Retrieve the render-pass encoder handle embedded in the command
        // encoder.  In the Dawn C++ API the caller normally opens the pass and
        // passes the RenderPassEncoder directly; to avoid changing the public
        // API surface (WgpuCommandEncoder) we derive a transient pass handle
        // here with a minimal descriptor so the skeleton is self-contained.
        //
        // Phase 5.5 will replace this block with a proper
        //   wgpu::RenderPassDescriptor rpd { … };
        //   auto rp = encoder.BeginRenderPass(&rpd);
        // driven by the swapchain texture view.
        wgpu::RenderPassDescriptor rpd {};
        // colour attachment left default-initialised (null view) — acceptable
        // for recording draw commands on a null/mock encoder in unit tests;
        // Phase 5.5 will fill attachmentCount + colorAttachments.
        rpd.colorAttachmentCount = 0U;
        rpd.colorAttachments     = nullptr;

        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&rpd);

        rp.SetVertexBuffer(0U, impl_->vb, 0U, wgpu::kWholeSize);
        rp.SetIndexBuffer(impl_->ib, wgpu::IndexFormat::Uint16, 0U,
                          wgpu::kWholeSize);

        for (const auto& dc : impl_->commands)
        {
            // Clamp scissor to the viewport so we never pass negative or
            // out-of-range values to SetScissorRect (WebGPU validation layer
            // rejects them).
            const auto sx = static_cast<std::uint32_t>(
                std::max<std::int32_t>(dc.scissor.x, 0));
            const auto sy = static_cast<std::uint32_t>(
                std::max<std::int32_t>(dc.scissor.y, 0));
            const auto sw = std::min(dc.scissor.width,  viewport_extent.width);
            const auto sh = std::min(dc.scissor.height, viewport_extent.height);

            rp.SetScissorRect(sx, sy, sw, sh);
            rp.DrawIndexed(dc.index_count,   // indexCount
                           1U,               // instanceCount
                           dc.index_offset,  // firstIndex
                           0,                // baseVertex
                           0U);              // firstInstance
        }

        rp.End();
    }
#else
    // Stub path: iterate and count (so tests can verify command_count()).
    (void)encoder;
#endif  // CD_UI_WEBGPU_HAVE_DAWN
}

// ===========================================================================
// State queries
// ===========================================================================

bool Submitter::is_valid() const noexcept
{
    return impl_ != nullptr && impl_->valid;
}

std::uint32_t Submitter::vertex_count()  const noexcept
{
    return impl_ ? impl_->vertex_count  : 0U;
}

std::uint32_t Submitter::index_count()   const noexcept
{
    return impl_ ? impl_->index_count   : 0U;
}

std::uint32_t Submitter::command_count() const noexcept
{
    return impl_ ? static_cast<std::uint32_t>(impl_->commands.size()) : 0U;
}

}  // namespace cd::ui::renderer_webgpu
