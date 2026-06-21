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
//     The LIVE UI pipeline is BUILT here (build_pipeline): wgpu::ShaderModule
//     from kUiShaderWgsl (Tint-compiled on a real device), wgpu::RenderPipeline
//     wired to the vertex_attributes() layout + the configured colour target +
//     a straight-alpha UI blend, a Globals UBO and a default 1x1 white atlas
//     tex/sampler bind group.  record_pass() binds the pipeline + bind groups
//     and draws into a caller-owned render pass.  The headless render+readback
//     test (tests/test_webgpu_dawn_device.cpp) proves it rasterizes pixels.
//     Against a NULL wgpu::Device (the build-only draw-count test) all GPU
//     entry points are skipped and the object stays a valid no-op.
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

#include <cd/ui/renderer_webgpu/WgpuContract.hpp>

#include <array>
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
    wgpu::Buffer         vb              {};
    wgpu::Buffer         ib              {};

    // Live UI pipeline (built in create() when the device is real).
    wgpu::RenderPipeline pipeline        {};
    wgpu::Buffer         globals_ubo     {};
    wgpu::BindGroup      globals_group   {};  ///< group(0): Globals UBO
    wgpu::BindGroup      atlas_group     {};  ///< group(1): atlas tex + sampler
    wgpu::Texture        atlas_texture   {};  ///< 1x1 white default atlas
    bool                 pipeline_ready  { false };
#endif
};

#if CD_UI_WEBGPU_HAVE_DAWN
namespace
{

// Map a host-contract logical vertex-format code (vertex_format::k*) to the
// concrete wgpu::VertexFormat enumerator.  The contract stores spec-stable
// numeric codes for host testability; the live Dawn enum is matched here BY
// NAME so a raw numeric cast can never select the wrong Dawn format (Dawn's
// enum order differs from the contract's documented spec codes).
[[nodiscard]] wgpu::VertexFormat to_wgpu_vertex_format(std::uint32_t code) noexcept
{
    switch (code)
    {
        case vertex_format::kFloat32x2: return wgpu::VertexFormat::Float32x2;
        case vertex_format::kUnorm8x4:  return wgpu::VertexFormat::Unorm8x4;
        case vertex_format::kUint8x4:   return wgpu::VertexFormat::Uint8x4;
        default:                        return wgpu::VertexFormat::Float32x2;
    }
}

// Map a host-contract colour-format code (texture_format::k*) to the concrete
// wgpu::TextureFormat enumerator BY NAME (same rationale as the vertex map:
// the contract's spec codes do not equal this Dawn build's enum values).
// Falls back to RGBA8Unorm for any unsupported code.
[[nodiscard]] wgpu::TextureFormat to_wgpu_color_format(std::uint32_t code) noexcept
{
    switch (code)
    {
        case texture_format::kRgba8Unorm:     return wgpu::TextureFormat::RGBA8Unorm;
        case texture_format::kRgba8UnormSrgb: return wgpu::TextureFormat::RGBA8UnormSrgb;
        case texture_format::kBgra8Unorm:     return wgpu::TextureFormat::BGRA8Unorm;
        case texture_format::kBgra8UnormSrgb: return wgpu::TextureFormat::BGRA8UnormSrgb;
        default:                              return wgpu::TextureFormat::RGBA8Unorm;
    }
}

// ---------------------------------------------------------------------------
// build_pipeline — construct the live UI RenderPipeline + bind resources.
//
// Consumes the host contract (kUiShaderWgsl, vertex_attributes(),
// vertex_stride_bytes(), bind_group::*, binding::*, make_globals/Globals).
// On any failure (e.g. a null device returns null handles) it leaves
// impl.pipeline_ready == false and the object behaves as a safe no-op.
// ---------------------------------------------------------------------------
void build_pipeline(Submitter::Impl&           impl,
                    const wgpu::Device&         device,
                    const SubmitterCreateInfo&  info)
{
    // (a) Shader module from the WGSL contract source (Tint compiles it).
    wgpu::ShaderSourceWGSL wgsl_src {};
    wgsl_src.code = kUiShaderWgsl;  // StringView accepts std::string_view

    wgpu::ShaderModuleDescriptor smd {};
    smd.nextInChain = &wgsl_src;
    smd.label       = "cd_ui_webgpu.shader";

    const wgpu::ShaderModule module = device.CreateShaderModule(&smd);
    if (!module) { return; }

    // (b) Globals UBO (inv-viewport block, 16 bytes) — written per record_pass.
    {
        wgpu::BufferDescriptor gbd {};
        gbd.label = "cd_ui_webgpu.globals";
        gbd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        gbd.size  = sizeof(Globals);
        impl.globals_ubo = device.CreateBuffer(&gbd);
        if (!impl.globals_ubo) { return; }
    }

    // (c) Default 1x1 white atlas texture (so kSolid draws — which ignore the
    //     atlas — still satisfy the bind-group layout the pipeline requires).
    {
        wgpu::TextureDescriptor td {};
        td.label         = "cd_ui_webgpu.atlas";
        td.usage         = wgpu::TextureUsage::TextureBinding
                         | wgpu::TextureUsage::CopyDst;
        td.dimension     = wgpu::TextureDimension::e2D;
        td.size          = wgpu::Extent3D { 1U, 1U, 1U };
        td.format        = wgpu::TextureFormat::RGBA8Unorm;
        td.mipLevelCount = 1U;
        td.sampleCount   = 1U;
        impl.atlas_texture = device.CreateTexture(&td);
        if (!impl.atlas_texture) { return; }

        // Upload one opaque-white texel.
        const std::array<std::uint8_t, 4U> white { 255U, 255U, 255U, 255U };
        wgpu::TexelCopyTextureInfo dst {};
        dst.texture  = impl.atlas_texture;
        dst.mipLevel = 0U;
        dst.origin   = wgpu::Origin3D { 0U, 0U, 0U };
        dst.aspect   = wgpu::TextureAspect::All;

        wgpu::TexelCopyBufferLayout layout {};
        layout.offset      = 0U;
        layout.bytesPerRow = 4U;
        layout.rowsPerImage = 1U;

        const wgpu::Extent3D extent { 1U, 1U, 1U };
        device.GetQueue().WriteTexture(&dst, white.data(), white.size(),
                                       &layout, &extent);
    }

    const wgpu::TextureView atlas_view = impl.atlas_texture.CreateView();
    if (!atlas_view) { return; }

    wgpu::SamplerDescriptor sd {};
    sd.label        = "cd_ui_webgpu.sampler";
    sd.addressModeU = wgpu::AddressMode::ClampToEdge;
    sd.addressModeV = wgpu::AddressMode::ClampToEdge;
    sd.addressModeW = wgpu::AddressMode::ClampToEdge;
    sd.magFilter    = wgpu::FilterMode::Linear;
    sd.minFilter    = wgpu::FilterMode::Linear;
    sd.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
    const wgpu::Sampler sampler = device.CreateSampler(&sd);
    if (!sampler) { return; }

    // (d) Vertex layout from the host contract.
    const auto contract_attrs = vertex_attributes();
    std::array<wgpu::VertexAttribute, 4U> attrs {};
    for (std::size_t i = 0U; i < attrs.size(); ++i)
    {
        attrs[i].format         = to_wgpu_vertex_format(contract_attrs[i].format);
        attrs[i].offset         = contract_attrs[i].offset;
        attrs[i].shaderLocation = contract_attrs[i].shader_location;
    }

    wgpu::VertexBufferLayout vbl {};
    vbl.stepMode       = wgpu::VertexStepMode::Vertex;
    vbl.arrayStride    = vertex_stride_bytes();
    vbl.attributeCount = attrs.size();
    vbl.attributes     = attrs.data();

    // (e) Alpha blend for UI (straight-alpha over).
    wgpu::BlendState blend {};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

    wgpu::ColorTargetState target {};
    target.format    = to_wgpu_color_format(info.color_format);
    target.blend     = &blend;
    target.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fs {};
    fs.module      = module;
    fs.entryPoint  = kFragmentEntryPoint;
    fs.targetCount = 1U;
    fs.targets     = &target;

    wgpu::RenderPipelineDescriptor rpd {};
    rpd.label              = "cd_ui_webgpu.pipeline";
    rpd.layout             = nullptr;  // auto layout from the WGSL bindings
    rpd.vertex.module      = module;
    rpd.vertex.entryPoint  = kVertexEntryPoint;
    rpd.vertex.bufferCount = 1U;
    rpd.vertex.buffers     = &vbl;
    rpd.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    rpd.primitive.frontFace = wgpu::FrontFace::CCW;
    rpd.primitive.cullMode  = wgpu::CullMode::None;
    rpd.multisample.count   = 1U;
    rpd.multisample.mask    = 0xFFFFFFFFU;
    rpd.fragment            = &fs;

    impl.pipeline = device.CreateRenderPipeline(&rpd);
    if (!impl.pipeline) { return; }

    // (f) Bind groups against the pipeline's auto-generated layouts.
    {
        wgpu::BindGroupEntry e {};
        e.binding = binding::kGlobalsUbo;
        e.buffer  = impl.globals_ubo;
        e.offset  = 0U;
        e.size    = sizeof(Globals);

        wgpu::BindGroupDescriptor bgd {};
        bgd.label      = "cd_ui_webgpu.globals_bg";
        bgd.layout     = impl.pipeline.GetBindGroupLayout(bind_group::kGlobals);
        bgd.entryCount = 1U;
        bgd.entries    = &e;
        impl.globals_group = device.CreateBindGroup(&bgd);
        if (!impl.globals_group) { return; }
    }
    {
        std::array<wgpu::BindGroupEntry, 2U> e {};
        e[0].binding     = binding::kAtlasTexture;
        e[0].textureView = atlas_view;
        e[1].binding     = binding::kAtlasSampler;
        e[1].sampler     = sampler;

        wgpu::BindGroupDescriptor bgd {};
        bgd.label      = "cd_ui_webgpu.atlas_bg";
        bgd.layout     = impl.pipeline.GetBindGroupLayout(bind_group::kAtlas);
        bgd.entryCount = e.size();
        bgd.entries    = e.data();
        impl.atlas_group = device.CreateBindGroup(&bgd);
        if (!impl.atlas_group) { return; }
    }

    impl.pipeline_ready = true;
}

// ---------------------------------------------------------------------------
// record_commands — bind the live pipeline (when ready) + the ring buffers,
// then issue one scissor + DrawIndexed per DrawCommand into `rp`.  Shared by
// record() (null-descriptor pass) and record_pass() (caller-owned pass).
// A null `rp` (e.g. the build-only null-encoder draw-count test) makes every
// call below a safe no-op on a null Dawn handle.
// ---------------------------------------------------------------------------
void record_commands(const Submitter::Impl&     impl,
                     const wgpu::RenderPassEncoder& rp,
                     Extent2D                   viewport_extent)
{
    if (!rp) { return; }

    if (impl.pipeline_ready)
    {
        rp.SetPipeline(impl.pipeline);
        rp.SetBindGroup(bind_group::kGlobals, impl.globals_group, 0U, nullptr);
        rp.SetBindGroup(bind_group::kAtlas,   impl.atlas_group,   0U, nullptr);
    }

    rp.SetVertexBuffer(0U, impl.vb, 0U, wgpu::kWholeSize);
    rp.SetIndexBuffer(impl.ib, wgpu::IndexFormat::Uint16, 0U, wgpu::kWholeSize);

    for (const auto& dc : impl.commands)
    {
        // Clamp scissor to the viewport via the shared (host-tested) contract
        // so we never pass negative or out-of-range values to SetScissorRect
        // (WebGPU's validation layer rejects them).
        const ScissorPx sc = clamp_scissor(
            dc.scissor, viewport_extent.width, viewport_extent.height);
        const DrawSlice slice = draw_slice(dc);

        rp.SetScissorRect(sc.x, sc.y, sc.width, sc.height);
        rp.DrawIndexed(slice.index_count,  // indexCount
                       1U,                 // instanceCount
                       slice.first_index,  // firstIndex
                       0,                  // baseVertex
                       0U);                // firstInstance
    }
}

}  // namespace
#endif  // CD_UI_WEBGPU_HAVE_DAWN

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
    // Release the live pipeline + bind resources before the ring buffers so
    // nothing references a Destroy()ed buffer.  Bind groups / pipeline are
    // reference-counted handles: clearing them drops our reference.
    impl_->globals_group = {};
    impl_->atlas_group   = {};
    impl_->pipeline      = {};
    if (impl_->atlas_texture) { impl_->atlas_texture.Destroy(); impl_->atlas_texture = {}; }
    if (impl_->globals_ubo)   { impl_->globals_ubo.Destroy();   impl_->globals_ubo   = {}; }
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
    // Guarded on a real device: calling CreateBuffer / CreateShaderModule on a
    // NULL wgpu::Device dereferences a null handle (AV), so a null device
    // (the build-only draw-count test) skips all GPU allocation and the object
    // stays a valid no-op.  This preserves the documented "create succeeds with
    // a null device" contract without touching Dawn entry points on null.
    if (device)
    {
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

        // LIVE Dawn UI pipeline (consumes the host contract in WgpuContract.hpp):
        // ShaderModule(kUiShaderWgsl) -> RenderPipeline(vertex_attributes() +
        // colour target + alpha blend) + Globals UBO + atlas tex/sampler bind
        // groups.  The headless offscreen render+readback test
        // (test_webgpu_dawn_device) proves this pipeline actually rasterizes.
        build_pipeline(*out.impl_, device, info);
    }
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
    // Real path: WriteBuffer into ring buffers via the device queue.  Guarded
    // on a real device + allocated buffers (a null device has no queue/buffers
    // — GetQueue on null would AV; the null-device draw-count test relies on
    // this skip).  Metadata is still snapshotted below in both cases.
    if (impl_->device && impl_->vb && impl_->ib)
    {
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
    // This overload opens its OWN render pass with a null-descriptor (no
    // colour attachment).  It is the build-only draw-count path exercised by
    // the Dawn=1 `RecordEmitsExpectedDrawCallCount` test with a null encoder;
    // a null/empty encoder yields a null pass and the binding calls below are
    // safe no-ops on null Dawn handles.  For REAL pixel output the caller
    // opens a pass against a real attachment and calls record_pass().
    {
        wgpu::RenderPassDescriptor rpd {};
        rpd.colorAttachmentCount = 0U;
        rpd.colorAttachments     = nullptr;

        wgpu::RenderPassEncoder rp = encoder ? encoder.BeginRenderPass(&rpd)
                                             : wgpu::RenderPassEncoder {};
        record_commands(*impl_, rp, viewport_extent);
        if (rp) { rp.End(); }
    }
#else
    // Stub path: iterate and count (so tests can verify command_count()).
    (void)encoder;
#endif  // CD_UI_WEBGPU_HAVE_DAWN
}

#if CD_UI_WEBGPU_HAVE_DAWN
// ===========================================================================
// record_pass — live pipeline path into a caller-owned render pass
// ===========================================================================

void Submitter::record_pass(wgpu::RenderPassEncoder pass,
                            Extent2D                viewport_extent) const
{
    if (!impl_ || impl_->commands.empty() || !pass) return;

    // Update the Globals UBO so the vertex shader maps pixel-space to clip
    // space for THIS viewport.  Uploaded via the queue before the pass draws
    // (queue writes are ordered before the submitted command buffer executes).
    if (impl_->pipeline_ready && impl_->globals_ubo)
    {
        const Globals g = make_globals(viewport_extent.width,
                                       viewport_extent.height);
        impl_->device.GetQueue().WriteBuffer(
            impl_->globals_ubo, 0U, &g, sizeof(Globals));
    }

    record_commands(*impl_, pass, viewport_extent);
}
#endif  // CD_UI_WEBGPU_HAVE_DAWN

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
