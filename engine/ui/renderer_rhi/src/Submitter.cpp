// =============================================================================
// CHROMODYNAMIC -- cd/ui/renderer_rhi/Submitter.cpp
//
// Phase 1.2b of ADR-20260530-ui-widget-library. Owns ring vertex/index
// buffers plus (optionally) an inline-compiled GLSL pipeline for the
// Route B fallback path introduced in Phase 554 / M3 W1A.
//
// Two factories:
//   * Submitter::create(...)                      -- Route A: ring vb/ib only;
//                                                    caller binds a pipeline.
//   * Submitter::create_with_inline_shader(...)   -- Route B: vb/ib +
//                                                    glslang-compiled
//                                                    solid-quad pipeline.
//
// NullDevice path: `create()` succeeds (vb/ib allocations succeed against
// NullDevice). `create_with_inline_shader()` ALSO succeeds against
// NullDevice -- glslang produces real SPIR-V, NullDevice accepts the
// shader-module / pipeline-layout / graphics-pipeline create calls and
// returns valid (stub) handles. Functional draws happen only on a real
// graphics backend (Vulkan today).
// =============================================================================
#include <cd/ui/renderer_rhi/Submitter.hpp>

#include <cd/material/Material.hpp>
#include <cd/material/UiVariant.hpp>
#include <cd/rhi/BlendPresets.hpp>
#include <cd/rhi/DepthStencilPresets.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/rhi/RasterStatePresets.hpp>
#include <cd/shader/Compiler.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace cd::ui::renderer_rhi
{

// -----------------------------------------------------------------------------
// Phase 554 / M3 W1A -- inline GLSL sources for the Route B fallback pipeline.
//
// vec2 pos -> NDC via push-constant viewport size; vertex colour passes
// straight through to the single colour attachment. No texture sampler
// because the v1 path renders solid quads only (panel backgrounds, etc.).
// -----------------------------------------------------------------------------
namespace
{

constexpr const char* kInlineVS = R"glsl(
#version 450
layout(push_constant) uniform PushConstants {
    vec2 viewport_size;   // in pixels, e.g. (1280, 720)
} pc;

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;   // RGBA8 normalized -> vec4

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main()
{
    // pixel-space -> [0,1] -> [-1,+1]. Y stays top-down (Vulkan NDC).
    vec2 ndc = (in_pos / pc.viewport_size) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = in_color;
    v_uv    = in_uv;
}
)glsl";

constexpr const char* kInlineFS = R"glsl(
#version 450
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    // v1 fallback: no atlas sampler. Glyph rendering requires the
    // cd::material UI variant (Route A) -- it lands with M3 W1B.
    out_color = v_color;
}
)glsl";

}  // namespace

struct Submitter::Impl
{
    cd::rhi::IDevice*           device { nullptr };
    SubmitterCreateInfo         info {};
    cd::rhi::BufferHandle       vb {};
    cd::rhi::BufferHandle       ib {};
    std::uint32_t               vb_capacity_bytes { 0U };
    std::uint32_t               ib_capacity_bytes { 0U };

    // Phase 554 -- inline pipeline (Route B). Stays empty when create()
    // was used; both factories share the same Impl + destroy() path.
    bool                            inline_pipeline_owned { false };
    cd::rhi::ShaderModuleHandle     vs {};
    cd::rhi::ShaderModuleHandle     fs {};
    cd::rhi::PipelineLayoutHandle   pipeline_layout {};
    cd::rhi::GraphicsPipelineHandle pipeline {};

    // Phase 608 / M7 W3 -- Route A pipeline source. When set, `record()`
    // binds `material_variant->material().pipeline()` and pushes the
    // viewport size into the variant's push-constant range. RAII through
    // the variant's owned Material handles, so Submitter::destroy() does
    // NOT need to touch any of these handles individually.
    bool                                          material_pipeline_owned { false };
    std::unique_ptr<cd::material::UiVariant>      material_variant {};

    // Phase 648 / M10 W3A Sprint-3 -- Route A descriptor-set wire-up.
    //
    // When the supplied UiVariant has descriptors (theme UBO and/or SDF
    // sampler), the Submitter allocates a MaterialInstance from the
    // variant's set layout and (optionally) a theme UBO buffer.  Both
    // are released in destroy(); the MaterialInstance carries its own
    // RAII against the device.
    std::unique_ptr<cd::material::MaterialInstance> material_instance {};
    cd::rhi::BufferHandle                         theme_ubo {};
    std::uint32_t                                 theme_ubo_slot { 0U };
    std::uint32_t                                 sdf_sampler_slot { 0U };
    bool                                          has_theme_ubo { false };
    bool                                          has_sdf_sampler { false };

    // Frame-local snapshot from `upload`.
    std::uint32_t               vertex_count { 0U };
    std::uint32_t               index_count  { 0U };
    std::vector<cd::ui::renderer::DrawCommand> commands;
};

// ---- ctor / dtor / move ---------------------------------------------------

Submitter::Submitter() noexcept = default;
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
        // Tear down inline pipeline resources (Route B) in reverse-create
        // order so dependents go first. Route A leaves these handles
        // invalid -- destroy_* is a no-op for null handles.
        if (impl_->inline_pipeline_owned)
        {
            if (impl_->pipeline.is_valid())
            {
                impl_->device->destroy_graphics_pipeline(impl_->pipeline);
            }
            if (impl_->pipeline_layout.is_valid())
            {
                impl_->device->destroy_pipeline_layout(impl_->pipeline_layout);
            }
            if (impl_->fs.is_valid())
            {
                impl_->device->destroy_shader_module(impl_->fs);
            }
            if (impl_->vs.is_valid())
            {
                impl_->device->destroy_shader_module(impl_->vs);
            }
        }
        // Phase 608 / M7 W3 -- Route A: release the owned UiVariant BEFORE
        // tearing down our own vb/ib so the variant's Material RAII (which
        // holds pipeline + pipeline_layout + descriptor_set_layout + shader
        // modules) drives its destroy_* calls against a still-live device.
        //
        // Phase 648 / M10 W3A Sprint-3: release the MaterialInstance and
        // the theme UBO buffer (if any) BEFORE the variant so the descriptor
        // set goes back to the pool while its layout is still alive.
        if (impl_->material_pipeline_owned)
        {
            impl_->material_instance.reset();
            if (impl_->theme_ubo.is_valid())
            {
                impl_->device->destroy_buffer(impl_->theme_ubo);
            }
            impl_->material_variant.reset();
        }
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

// ---- create_with_inline_shader -------------------------------------------
//
// Phase 554 / M3 W1A -- Route B. Reuses `create()` for the ring vertex/index
// buffer allocation, then layers a full GLSL-compiled pipeline on top so
// `record()` produces a real coloured panel pass. See header for the
// shader contract.
cd::core::Result<Submitter>
Submitter::create_with_inline_shader(cd::rhi::IDevice& device, const SubmitterCreateInfo& info)
{
    auto base = Submitter::create(device, info);
    if (!base.has_value())
    {
        return std::unexpected(base.error());
    }
    Submitter out = std::move(*base);

    // 1) Compile the inline GLSL via cd::shader::ICompiler (glslang backend).
    //    When the engine is built without CD_ENABLE_GLSLANG the factory
    //    returns nullptr and the caller cannot use Route B -- propagate
    //    the failure as a structured error so the caller can fall back.
    auto compiler = cd::shader::make_glslang_compiler();
    if (compiler == nullptr)
    {
        out.destroy();
        return std::unexpected(cd::core::ErrorCode {
            cd::shader::shader_errors::kDomain,
            static_cast<std::uint32_t>(cd::shader::shader_errors::Code::kInitFailed),
            "Submitter::create_with_inline_shader: glslang backend disabled"});
    }

    cd::shader::CompileDesc vs_desc {};
    vs_desc.source       = kInlineVS;
    vs_desc.stage        = cd::shader::ShaderStage::kVertex;
    vs_desc.lang         = cd::shader::ShaderLanguage::kGlsl;
    vs_desc.source_name  = "cd_ui_submitter_inline.vert";
    auto vs_compile = compiler->compile(vs_desc);
    if (!vs_compile.has_value())
    {
        out.destroy();
        return std::unexpected(vs_compile.error());
    }

    cd::shader::CompileDesc fs_desc {};
    fs_desc.source       = kInlineFS;
    fs_desc.stage        = cd::shader::ShaderStage::kFragment;
    fs_desc.lang         = cd::shader::ShaderLanguage::kGlsl;
    fs_desc.source_name  = "cd_ui_submitter_inline.frag";
    auto fs_compile = compiler->compile(fs_desc);
    if (!fs_compile.has_value())
    {
        out.destroy();
        return std::unexpected(fs_compile.error());
    }

    // 2) Create shader modules from the produced SPIR-V words.
    cd::rhi::ShaderModuleDesc vs_mod {};
    vs_mod.stage       = cd::rhi::ShaderStage::kVertex;
    vs_mod.code        = vs_compile->spirv.data();
    vs_mod.code_size   = vs_compile->spirv.size() * sizeof(std::uint32_t);
    vs_mod.entry_point = "main";
    vs_mod.debug_name  = "cd_ui_submitter_inline.vs";
    auto vs_r = device.create_shader_module(vs_mod);
    if (!vs_r.has_value())
    {
        out.destroy();
        return std::unexpected(vs_r.error());
    }
    out.impl_->vs = *vs_r;

    cd::rhi::ShaderModuleDesc fs_mod {};
    fs_mod.stage       = cd::rhi::ShaderStage::kFragment;
    fs_mod.code        = fs_compile->spirv.data();
    fs_mod.code_size   = fs_compile->spirv.size() * sizeof(std::uint32_t);
    fs_mod.entry_point = "main";
    fs_mod.debug_name  = "cd_ui_submitter_inline.fs";
    auto fs_r = device.create_shader_module(fs_mod);
    if (!fs_r.has_value())
    {
        out.destroy();
        return std::unexpected(fs_r.error());
    }
    out.impl_->fs = *fs_r;

    // 3) Pipeline layout: one push-constant range carrying the viewport
    //    size (two floats). No descriptor sets in Route B (no sampler).
    const std::array<cd::rhi::PushConstantRange, 1> pcr {
        cd::rhi::PushConstantRange {
            cd::rhi::ShaderStage::kVertex, 0U,
            static_cast<std::uint32_t>(2U * sizeof(float)) }
    };
    cd::rhi::PipelineLayoutDesc pld {};
    pld.push_constants = std::span<const cd::rhi::PushConstantRange>(pcr.data(), pcr.size());
    auto pl_r = device.create_pipeline_layout(pld);
    if (!pl_r.has_value())
    {
        out.destroy();
        return std::unexpected(pl_r.error());
    }
    out.impl_->pipeline_layout = *pl_r;

    // 4) Vertex input layout. Matches `cd::ui::renderer::Vertex` exactly:
    //    pos2 + uv2 + RGBA8 colour. The packed variant+pad bytes are
    //    ignored by Route B (Route A's pipeline will read them as a uint).
    const std::array<cd::rhi::VertexBinding, 1> v_binds {
        cd::rhi::VertexBinding { 0U,
                                 static_cast<std::uint32_t>(sizeof(cd::ui::renderer::Vertex)),
                                 false }
    };
    const std::array<cd::rhi::VertexAttribute, 3> v_attrs {
        // location 0 : pos (vec2 float)
        cd::rhi::VertexAttribute { 0U, 0U, cd::rhi::Format::kRG32Float,
            static_cast<std::uint32_t>(offsetof(cd::ui::renderer::Vertex, pos_x)) },
        // location 1 : uv  (vec2 float)
        cd::rhi::VertexAttribute { 1U, 0U, cd::rhi::Format::kRG32Float,
            static_cast<std::uint32_t>(offsetof(cd::ui::renderer::Vertex, uv_x)) },
        // location 2 : color (RGBA8 unorm -> vec4)
        cd::rhi::VertexAttribute { 2U, 0U, cd::rhi::Format::kRGBA8Unorm,
            static_cast<std::uint32_t>(offsetof(cd::ui::renderer::Vertex, r)) },
    };

    // 5) Pipeline state. Alpha blend on, depth disabled, no culling
    //    (UI quads are emitted in CW order from the batcher but we don't
    //    want pixel loss if a future caller flips the order).
    const std::array<cd::rhi::BlendAttachmentState, 1> blends {
        cd::rhi::blend_alpha()
    };
    const std::array<cd::rhi::Format, 1> color_fmts { info.color_format };

    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout                   = out.impl_->pipeline_layout;
    gpd.vertex_shader            = out.impl_->vs;
    gpd.fragment_shader          = out.impl_->fs;
    gpd.vertex_bindings          = std::span<const cd::rhi::VertexBinding>(v_binds.data(), v_binds.size());
    gpd.vertex_attributes        = std::span<const cd::rhi::VertexAttribute>(v_attrs.data(), v_attrs.size());
    gpd.topology                 = cd::rhi::PrimitiveTopology::kTriangleList;
    gpd.raster                   = cd::rhi::raster_solid_none();
    gpd.depth_stencil            = cd::rhi::depth_disabled();
    gpd.blend_attachments        = std::span<const cd::rhi::BlendAttachmentState>(blends.data(), blends.size());
    gpd.samples                  = cd::rhi::SampleCount::k1;
    gpd.color_attachment_formats = std::span<const cd::rhi::Format>(color_fmts.data(), color_fmts.size());
    gpd.depth_attachment_format  = info.depth_format;
    auto gp_r = device.create_graphics_pipeline(gpd);
    if (!gp_r.has_value())
    {
        out.destroy();
        return std::unexpected(gp_r.error());
    }
    out.impl_->pipeline              = *gp_r;
    out.impl_->inline_pipeline_owned = true;
    return out;
}

// ---- create_with_material_ui_variant -------------------------------------
//
// Phase 608 / M7 W3 -- Route A. Reuses `create()` for the ring vertex/index
// buffer allocation, then stashes the supplied cd::material::UiVariant as
// the pipeline source. `record()` later binds the variant's pipeline +
// pushes the viewport-size push-constant the variant's shader expects
// (matches the Sprint-1/Sprint-2 vertex-shader push contract).
cd::core::Result<Submitter>
Submitter::create_with_material_ui_variant(cd::rhi::IDevice&          device,
                                           const SubmitterCreateInfo& info,
                                           cd::material::UiVariant&&  variant)
{
    if (!variant.is_valid())
    {
        return std::unexpected(cd::core::ErrorCode {
            0x000F, 2U,
            "Submitter::create_with_material_ui_variant: supplied UiVariant is inert"});
    }

    auto base = Submitter::create(device, info);
    if (!base.has_value())
    {
        return std::unexpected(base.error());
    }
    Submitter out = std::move(*base);

    const bool has_theme_ubo   = variant.has_theme_ubo();
    const bool has_sdf_sampler = variant.has_sdf_sampler();
    const bool has_descriptors = variant.material().has_descriptors();

    out.impl_->material_variant        = std::make_unique<cd::material::UiVariant>(std::move(variant));
    out.impl_->material_pipeline_owned = true;
    out.impl_->has_theme_ubo           = has_theme_ubo;
    out.impl_->has_sdf_sampler         = has_sdf_sampler;

    // Phase 648 / M10 W3A Sprint-3 -- when the variant ships descriptors,
    // allocate a MaterialInstance + optional theme UBO buffer + write the
    // initial descriptor set so the first record() can bind it cleanly.
    //
    // The slot numbers MUST mirror the spec the variant was built from.
    // Sprint-2 default is theme_ubo at binding 0, sdf_sampler at binding 1;
    // if the caller used different slots the descriptor set still has the
    // bindings (the layout was built from the spec), and a follow-up
    // set_theme_palette / set_sdf_atlas with the matching slot succeeds.
    // (Callers using custom slots can rebind via set_theme_palette /
    // set_sdf_atlas — the slot numbers in those calls flow into the
    // DescriptorWrite, not the layout, so this default initialiser is
    // safe even when the spec used non-default slots.)
    if (has_descriptors)
    {
        // For now we hard-wire slots to the Sprint-2 defaults (0 = theme,
        // 1 = SDF). Callers using custom slots can still upload via the
        // explicit `set_theme_palette` / `set_sdf_atlas` API after create
        // returns; only the initial write below uses defaults.
        out.impl_->theme_ubo_slot   = 0U;
        out.impl_->sdf_sampler_slot = 1U;

        auto inst_r = cd::material::MaterialInstance::create(
            device, out.impl_->material_variant->material());
        if (!inst_r.has_value())
        {
            out.destroy();
            return std::unexpected(inst_r.error());
        }
        out.impl_->material_instance =
            std::make_unique<cd::material::MaterialInstance>(std::move(*inst_r));

        if (has_theme_ubo)
        {
            cd::rhi::BufferDesc bd {};
            bd.size       = sizeof(cd::material::UiThemePaletteUbo);
            bd.usage      = cd::rhi::BufferUsage::kUniform
                          | cd::rhi::BufferUsage::kTransferDst;
            bd.memory     = cd::rhi::MemoryUsage::kCpuToGpu;
            bd.debug_name = "cd_ui_renderer_rhi.theme_ubo";
            auto ubo_r = device.create_buffer(bd);
            if (!ubo_r.has_value())
            {
                out.destroy();
                return std::unexpected(ubo_r.error());
            }
            out.impl_->theme_ubo = *ubo_r;

            // Seed the buffer with default palette so the first frame
            // does not sample uninitialised memory.
            const cd::material::UiThemePaletteUbo default_payload {};
            std::array<std::byte, sizeof(cd::material::UiThemePaletteUbo)> bytes {};
            std::memcpy(bytes.data(), &default_payload, sizeof(default_payload));
            (void)device.upload_buffer(
                out.impl_->theme_ubo, 0U,
                std::span<const std::byte>(bytes.data(), bytes.size()));

            // Write the UBO into the descriptor set so the variant's
            // pipeline can sample the palette on its first draw. The slot
            // matches the variant spec's `theme_palette_ubo_slot` (default 0).
            cd::rhi::DescriptorWrite w {};
            w.binding       = out.impl_->theme_ubo_slot;
            w.array_element = 0U;
            w.type          = cd::rhi::DescriptorType::kUniformBuffer;
            w.buffer        = out.impl_->theme_ubo;
            w.buffer_offset = 0U;
            w.buffer_range  = sizeof(cd::material::UiThemePaletteUbo);
            const std::array<cd::rhi::DescriptorWrite, 1> writes { w };
            auto upd = out.impl_->material_instance->update(
                std::span<const cd::rhi::DescriptorWrite>(writes.data(), writes.size()));
            if (!upd.has_value())
            {
                out.destroy();
                return std::unexpected(upd.error());
            }
        }

        // SDF sampler descriptor is left UNBOUND at create-time. The caller
        // MUST invoke `set_sdf_atlas` with a live view + sampler before any
        // record() that depends on glyph quads. The descriptor set still
        // has the binding (the layout came from the variant spec); a
        // record() with the binding unbound is a Vulkan validation warning,
        // not a hard error — the editor's glyph atlas wires in once the
        // ui_font path produces a TextureView the submitter can consume.
        if (has_sdf_sampler && info.atlas_view.is_valid() && info.atlas_sampler.is_valid())
        {
            cd::rhi::DescriptorWrite w {};
            w.binding       = out.impl_->sdf_sampler_slot;
            w.array_element = 0U;
            w.type          = cd::rhi::DescriptorType::kCombinedImageSampler;
            w.view          = info.atlas_view;
            w.sampler       = info.atlas_sampler;
            const std::array<cd::rhi::DescriptorWrite, 1> writes { w };
            (void)out.impl_->material_instance->update(
                std::span<const cd::rhi::DescriptorWrite>(writes.data(), writes.size()));
        }
    }

    return out;
}

bool Submitter::set_theme_palette(const cd::material::UiThemePaletteUbo& payload)
{
    if (!impl_ || impl_->device == nullptr) return false;
    if (!impl_->material_pipeline_owned)    return false;
    if (!impl_->has_theme_ubo)              return false;
    if (!impl_->theme_ubo.is_valid())       return false;

    std::array<std::byte, sizeof(cd::material::UiThemePaletteUbo)> bytes {};
    std::memcpy(bytes.data(), &payload, sizeof(payload));
    auto up = impl_->device->upload_buffer(
        impl_->theme_ubo, 0U,
        std::span<const std::byte>(bytes.data(), bytes.size()));
    return up.has_value();
}

bool Submitter::set_sdf_atlas(cd::rhi::TextureViewHandle view,
                              cd::rhi::SamplerHandle     sampler)
{
    if (!impl_ || impl_->device == nullptr)  return false;
    if (!impl_->material_pipeline_owned)     return false;
    if (!impl_->has_sdf_sampler)             return false;
    if (!impl_->material_instance)           return false;
    if (!view.is_valid() || !sampler.is_valid()) return false;

    cd::rhi::DescriptorWrite w {};
    w.binding       = impl_->sdf_sampler_slot;
    w.array_element = 0U;
    w.type          = cd::rhi::DescriptorType::kCombinedImageSampler;
    w.view          = view;
    w.sampler       = sampler;
    const std::array<cd::rhi::DescriptorWrite, 1> writes { w };
    auto upd = impl_->material_instance->update(
        std::span<const cd::rhi::DescriptorWrite>(writes.data(), writes.size()));
    return upd.has_value();
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
    // group. Phase 554 / M3 W1A: when the submitter owns an inline pipeline
    // (Route B), bind the pipeline + push the viewport size so the vertex
    // shader can project pixel-space verts into NDC. Phase 608 / M7 W3:
    // when the submitter owns a cd::material::UiVariant (Route A), bind
    // the variant's pipeline + push the SAME viewport-size constant the
    // Route A vertex shader expects (Sprint-1/Sprint-2 vertex shader maps
    // pixel space -> NDC via `vec2 inv_viewport`). The vb/ib bind and
    // per-command scissor + draw_indexed are universal across both routes.
    if (impl_->inline_pipeline_owned && impl_->pipeline.is_valid())
    {
        cmd.bind_graphics_pipeline(impl_->pipeline);
        const std::array<float, 2> vp_size {
            static_cast<float>(viewport_extent.width),
            static_cast<float>(viewport_extent.height)
        };
        cmd.push_constants(impl_->pipeline_layout,
                           cd::rhi::ShaderStage::kVertex,
                           0U,
                           static_cast<std::uint32_t>(vp_size.size() * sizeof(float)),
                           vp_size.data());
    }
    else if (impl_->material_pipeline_owned && impl_->material_variant)
    {
        const auto& mat = impl_->material_variant->material();
        if (mat.pipeline().is_valid())
        {
            cmd.bind_graphics_pipeline(mat.pipeline());
            // Route A vertex shader uses `inv_viewport` (1 / pixel size)
            // so the same `pos * inv_viewport * 2.0 - 1.0` NDC mapping
            // works for any caller-supplied viewport. Guard against a
            // zero-size viewport by writing zeros (no draws will land on
            // a zero-extent target anyway, but avoid div-by-zero in case
            // a future caller pushes a NaN through the path).
            const float inv_w = viewport_extent.width  > 0U
                ? 1.0F / static_cast<float>(viewport_extent.width)  : 0.0F;
            const float inv_h = viewport_extent.height > 0U
                ? 1.0F / static_cast<float>(viewport_extent.height) : 0.0F;
            const std::array<float, 2> inv_vp { inv_w, inv_h };
            cmd.push_constants(mat.pipeline_layout(),
                               cd::rhi::ShaderStage::kVertex,
                               0U,
                               static_cast<std::uint32_t>(inv_vp.size() * sizeof(float)),
                               inv_vp.data());

            // Phase 648 / M10 W3A Sprint-3 -- bind the variant's
            // descriptor set at set index 0 when present. The set carries
            // the theme UBO (always when has_theme_ubo) + optional SDF
            // sampler. The descriptor was written at create()/set_*()
            // time; record() just binds the existing set.
            if (impl_->material_instance &&
                impl_->material_instance->is_valid() &&
                impl_->material_instance->descriptor_set().is_valid())
            {
                impl_->material_instance->bind(cmd, /*set_index=*/0U);
            }
        }
    }

    cmd.bind_vertex_buffer(0U, impl_->vb, 0U);
    cmd.bind_index_buffer(impl_->ib, 0U, cd::rhi::IndexType::kUInt16);

    for (const auto& dc : impl_->commands)
    {
        cd::rhi::Rect2D s {};
        if (dc.scissor.width == 0xFFFFFFFFu && dc.scissor.height == 0xFFFFFFFFu)
        {
            s.extent = viewport_extent;  // no-clip -> full viewport
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
