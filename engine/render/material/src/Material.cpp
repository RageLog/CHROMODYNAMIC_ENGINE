// =============================================================================
// CHROMODYNAMIC — cd/material/Material.cpp
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cd::material
{

namespace
{

[[nodiscard]] cd::shader::ShaderStage to_shader_stage(cd::rhi::ShaderStage s) noexcept
{
    using RS = cd::rhi::ShaderStage;
    if (cd::rhi::has(s, RS::kVertex))
        return cd::shader::ShaderStage::kVertex;
    if (cd::rhi::has(s, RS::kFragment))
        return cd::shader::ShaderStage::kFragment;
    if (cd::rhi::has(s, RS::kCompute))
        return cd::shader::ShaderStage::kCompute;
    return cd::shader::ShaderStage::kVertex;
}

/// Slurp a whole text file into an owning std::string. Returns
/// std::nullopt if the file cannot be opened. Used by the
/// `*_glsl_path` branch of `Material::create` so callers can keep
/// GLSL on disk (hot-reload, IDE syntax highlighting) without
/// paying for a long-lived file handle.
[[nodiscard]] std::optional<std::string> read_file_text(std::string_view path)
{
    const std::string p { path };
    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in.is_open())
        return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Compile a single GLSL stage and return its SPIR-V words. `out_spirv`
/// keeps the bytes alive past the function for downstream
/// `create_shader_module` use.
[[nodiscard]] cd::core::Result<std::vector<std::uint32_t>> compile_stage(
    cd::shader::ICompiler& compiler,
    std::string_view source,
    cd::rhi::ShaderStage rhi_stage,
    std::string_view name
)
{
    cd::shader::CompileDesc d {};
    d.source = source;
    d.stage = to_shader_stage(rhi_stage);
    d.lang = cd::shader::ShaderLanguage::kGlsl;
    d.target = cd::shader::TargetEnv::kVulkan13;
    d.source_name = name;
    auto r = compiler.compile(d);
    if (!r.has_value())
    {
        return std::unexpected(material_errors::wrap(material_errors::Code::kShaderCompileFailed, r.error()));
    }
    return std::move(r->spirv);
}

}  // namespace

// ---- Material ---------------------------------------------------------------

Material::~Material()
{
    release();
}

Material::Material(Material&& other) noexcept
{
    steal(std::move(other));
}

Material& Material::operator=(Material&& other) noexcept
{
    if (this != &other)
    {
        release();
        steal(std::move(other));
    }
    return *this;
}

void Material::release() noexcept
{
    if (device_ == nullptr)
        return;
    // Reverse-creation order: pipeline before its layouts; shaders are
    // independent of the pipeline once it's built so order with them is
    // immaterial, but keeping a deterministic sequence helps backends that
    // do strict tracking.
    if (pipeline_.is_valid())
        device_->destroy_graphics_pipeline(pipeline_);
    if (pipeline_layout_.is_valid())
        device_->destroy_pipeline_layout(pipeline_layout_);
    if (desc_layout_.is_valid())
        device_->destroy_descriptor_set_layout(desc_layout_);
    if (fs_.is_valid())
        device_->destroy_shader_module(fs_);
    if (vs_.is_valid())
        device_->destroy_shader_module(vs_);
    device_ = nullptr;
    pipeline_ = {};
    pipeline_layout_ = {};
    desc_layout_ = {};
    vs_ = {};
    fs_ = {};
    has_descriptors_ = false;
}

void Material::steal(Material&& other) noexcept
{
    device_ = other.device_;
    vs_ = other.vs_;
    fs_ = other.fs_;
    desc_layout_ = other.desc_layout_;
    pipeline_layout_ = other.pipeline_layout_;
    pipeline_ = other.pipeline_;
    has_descriptors_ = other.has_descriptors_;
    other.device_ = nullptr;
    other.vs_ = {};
    other.fs_ = {};
    other.desc_layout_ = {};
    other.pipeline_layout_ = {};
    other.pipeline_ = {};
    other.has_descriptors_ = false;
}

cd::core::Result<Material>
Material::create(cd::rhi::IDevice& device, cd::shader::ICompiler* compiler, const MaterialDesc& desc)
{
    // Resolve SPIR-V for each stage: prefer pre-compiled bytes, fall back
    // to inline GLSL via the supplied compiler.
    std::vector<std::uint32_t> vs_spirv;
    std::vector<std::uint32_t> fs_spirv;
    const std::uint32_t* vs_code { nullptr };
    std::size_t vs_size { 0 };
    const std::uint32_t* fs_code { nullptr };
    std::size_t fs_size { 0 };

    // Vertex-stage source resolution. Per ADR-20260529-X5 precedence:
    //   vertex_spirv > vertex_glsl_path > vertex_glsl
    // The owning string for the on-disk branch must outlive compile_stage.
    std::string vs_disk_source;
    if (!desc.vertex_spirv.empty())
    {
        vs_code = desc.vertex_spirv.data();
        vs_size = desc.vertex_spirv.size() * sizeof(std::uint32_t);
    }
    else if (!desc.vertex_glsl_path.empty() || !desc.vertex_glsl.empty())
    {
        if (compiler == nullptr)
        {
            return std::unexpected(
                material_errors::make(
                    material_errors::Code::kCompilerRequired,
                    "Material::create: vertex GLSL provided but no compiler"
                )
            );
        }
        std::string_view vs_src {};
        if (!desc.vertex_glsl_path.empty())
        {
            auto loaded = read_file_text(desc.vertex_glsl_path);
            if (loaded.has_value())
            {
                vs_disk_source = std::move(*loaded);
                vs_src = vs_disk_source;
            }
            else if (!desc.vertex_glsl.empty())
            {
                std::fprintf(
                    stderr,
                    "cd::material: warning: vertex_glsl_path '%.*s' not found, "
                    "falling back to embedded vertex_glsl\n",
                    static_cast<int>(desc.vertex_glsl_path.size()),
                    desc.vertex_glsl_path.data()
                );
                vs_src = desc.vertex_glsl;
            }
            else
            {
                return std::unexpected(
                    material_errors::make(
                        material_errors::Code::kInvalidArgument,
                        "Material::create: failed to open vertex_glsl_path AND no embedded vertex_glsl fallback"
                    )
                );
            }
        }
        else
        {
            vs_src = desc.vertex_glsl;
        }
        auto r = compile_stage(
            *compiler,
            vs_src,
            cd::rhi::ShaderStage::kVertex,
            std::string { desc.name } + ".vert"
        );
        if (!r.has_value())
            return std::unexpected(r.error());
        vs_spirv = std::move(*r);
        vs_code = vs_spirv.data();
        vs_size = vs_spirv.size() * sizeof(std::uint32_t);
    }
    else
    {
        return std::unexpected(
            material_errors::make(material_errors::Code::kInvalidArgument, "Material::create: vertex stage missing")
        );
    }

    // Fragment-stage source resolution. Same precedence as vertex.
    std::string fs_disk_source;
    if (!desc.fragment_spirv.empty())
    {
        fs_code = desc.fragment_spirv.data();
        fs_size = desc.fragment_spirv.size() * sizeof(std::uint32_t);
    }
    else if (!desc.fragment_glsl_path.empty() || !desc.fragment_glsl.empty())
    {
        if (compiler == nullptr)
        {
            return std::unexpected(
                material_errors::make(
                    material_errors::Code::kCompilerRequired,
                    "Material::create: fragment GLSL provided but no compiler"
                )
            );
        }
        std::string_view fs_src {};
        if (!desc.fragment_glsl_path.empty())
        {
            auto loaded = read_file_text(desc.fragment_glsl_path);
            if (loaded.has_value())
            {
                fs_disk_source = std::move(*loaded);
                fs_src = fs_disk_source;
            }
            else if (!desc.fragment_glsl.empty())
            {
                std::fprintf(
                    stderr,
                    "cd::material: warning: fragment_glsl_path '%.*s' not found, "
                    "falling back to embedded fragment_glsl\n",
                    static_cast<int>(desc.fragment_glsl_path.size()),
                    desc.fragment_glsl_path.data()
                );
                fs_src = desc.fragment_glsl;
            }
            else
            {
                return std::unexpected(
                    material_errors::make(
                        material_errors::Code::kInvalidArgument,
                        "Material::create: failed to open fragment_glsl_path AND no embedded fragment_glsl fallback"
                    )
                );
            }
        }
        else
        {
            fs_src = desc.fragment_glsl;
        }
        auto r = compile_stage(
            *compiler,
            fs_src,
            cd::rhi::ShaderStage::kFragment,
            std::string { desc.name } + ".frag"
        );
        if (!r.has_value())
            return std::unexpected(r.error());
        fs_spirv = std::move(*r);
        fs_code = fs_spirv.data();
        fs_size = fs_spirv.size() * sizeof(std::uint32_t);
    }
    else
    {
        return std::unexpected(
            material_errors::make(material_errors::Code::kInvalidArgument, "Material::create: fragment stage missing")
        );
    }

    Material m;
    m.device_ = &device;

    // 1) Shader modules.
    cd::rhi::ShaderModuleDesc vsd {};
    vsd.stage = cd::rhi::ShaderStage::kVertex;
    vsd.code = vs_code;
    vsd.code_size = vs_size;
    auto vs_r = device.create_shader_module(vsd);
    if (!vs_r.has_value())
    {
        return std::unexpected(
            material_errors::wrap(material_errors::Code::kPipelineCreationFailed, vs_r.error())
        );
    }
    m.vs_ = *vs_r;

    cd::rhi::ShaderModuleDesc fsd {};
    fsd.stage = cd::rhi::ShaderStage::kFragment;
    fsd.code = fs_code;
    fsd.code_size = fs_size;
    auto fs_r = device.create_shader_module(fsd);
    if (!fs_r.has_value())
    {
        return std::unexpected(
            material_errors::wrap(material_errors::Code::kPipelineCreationFailed, fs_r.error())
        );
    }
    m.fs_ = *fs_r;

    // 2) Descriptor set layout (only if the user gave us bindings).
    cd::rhi::DescriptorSetLayoutHandle set_layout_handle {};
    if (!desc.descriptor_bindings.empty())
    {
        cd::rhi::DescriptorSetLayoutDesc dsld {};
        dsld.bindings = desc.descriptor_bindings;
        auto dsl_r = device.create_descriptor_set_layout(dsld);
        if (!dsl_r.has_value())
        {
            return std::unexpected(
                material_errors::wrap(material_errors::Code::kPipelineCreationFailed, dsl_r.error())
            );
        }
        m.desc_layout_ = *dsl_r;
        set_layout_handle = *dsl_r;
        m.has_descriptors_ = true;
    }

    // 3) Pipeline layout.
    cd::rhi::PipelineLayoutDesc pld {};
    // phase864-multi-set: build the set-layout span from the material's
    // own descriptor set (when present) FOLLOWED BY any caller-supplied
    // extra layouts. Owner of `all_layouts` storage is this stack frame;
    // it lives until create_pipeline_layout returns, which is exactly
    // when the device-side pipeline layout no longer needs the handles.
    std::vector<cd::rhi::DescriptorSetLayoutHandle> all_layouts {};
    all_layouts.reserve(static_cast<std::size_t>(m.has_descriptors_)
                        + desc.extra_set_layouts.size());
    if (m.has_descriptors_)
    {
        all_layouts.push_back(set_layout_handle);
    }
    for (const auto& l : desc.extra_set_layouts)
    {
        all_layouts.push_back(l);
    }
    pld.set_layouts = std::span<const cd::rhi::DescriptorSetLayoutHandle>(
        all_layouts.data(), all_layouts.size());
    pld.push_constants = desc.push_constants;
    auto pl_r = device.create_pipeline_layout(pld);
    if (!pl_r.has_value())
    {
        return std::unexpected(
            material_errors::wrap(material_errors::Code::kPipelineCreationFailed, pl_r.error())
        );
    }
    m.pipeline_layout_ = *pl_r;

    // 4) Graphics pipeline.
    cd::rhi::GraphicsPipelineDesc gpd {};
    gpd.layout = m.pipeline_layout_;
    gpd.vertex_shader = m.vs_;
    gpd.fragment_shader = m.fs_;
    gpd.vertex_bindings = desc.vertex_bindings;
    gpd.vertex_attributes = desc.vertex_attributes;
    gpd.topology = desc.topology;
    gpd.raster = desc.raster;
    gpd.depth_stencil = desc.depth_stencil;
    gpd.blend_attachments = desc.blend_attachments;
    gpd.samples = desc.samples;
    gpd.color_attachment_formats = desc.color_attachment_formats;
    gpd.depth_attachment_format = desc.depth_attachment_format;
    auto gp_r = device.create_graphics_pipeline(gpd);
    if (!gp_r.has_value())
    {
        return std::unexpected(
            material_errors::wrap(material_errors::Code::kPipelineCreationFailed, gp_r.error())
        );
    }
    m.pipeline_ = *gp_r;

    return m;
}

void Material::apply(cd::rhi::IDrawRecorder& cmd) const
{
    if (!is_valid())
        return;
    cmd.bind_graphics_pipeline(pipeline_);
}

// ---- MaterialInstance -------------------------------------------------------

MaterialInstance::~MaterialInstance()
{
    release();
}

MaterialInstance::MaterialInstance(MaterialInstance&& other) noexcept
{
    steal(std::move(other));
}

MaterialInstance& MaterialInstance::operator=(MaterialInstance&& other) noexcept
{
    if (this != &other)
    {
        release();
        steal(std::move(other));
    }
    return *this;
}

void MaterialInstance::release() noexcept
{
    if (device_ == nullptr)
        return;
    if (desc_set_.is_valid())
        device_->destroy_descriptor_set(desc_set_);
    device_ = nullptr;
    desc_set_ = {};
    metallic_ = 0.0F;
    roughness_ = 0.5F;
    alpha_mode_ = AlphaMode::kOpaque;
    alpha_cutoff_ = 0.5F;
    // T1.12 — restore PbrFactors-equivalent defaults so an inert
    // MaterialInstance returns the documented neutral grey via the
    // ray_hit_sample fallback path (rather than stale set values from
    // before release).
    albedo_[0] = 1.0F;
    albedo_[1] = 1.0F;
    albedo_[2] = 1.0F;
    emissive_[0] = 0.0F;
    emissive_[1] = 0.0F;
    emissive_[2] = 0.0F;
}

void MaterialInstance::steal(MaterialInstance&& other) noexcept
{
    device_ = other.device_;
    desc_set_ = other.desc_set_;
    metallic_ = other.metallic_;
    roughness_ = other.roughness_;
    alpha_mode_ = other.alpha_mode_;
    alpha_cutoff_ = other.alpha_cutoff_;
    albedo_[0] = other.albedo_[0];
    albedo_[1] = other.albedo_[1];
    albedo_[2] = other.albedo_[2];
    emissive_[0] = other.emissive_[0];
    emissive_[1] = other.emissive_[1];
    emissive_[2] = other.emissive_[2];
    other.device_ = nullptr;
    other.desc_set_ = {};
    other.metallic_ = 0.0F;
    other.roughness_ = 0.5F;
    other.alpha_mode_ = AlphaMode::kOpaque;
    other.alpha_cutoff_ = 0.5F;
    other.albedo_[0] = 1.0F;
    other.albedo_[1] = 1.0F;
    other.albedo_[2] = 1.0F;
    other.emissive_[0] = 0.0F;
    other.emissive_[1] = 0.0F;
    other.emissive_[2] = 0.0F;
}

void MaterialInstance::set_metallic(float m) noexcept
{
    // T1.9 — clamp into [0,1]; out-of-range input is silently saturated so
    // downstream Schlick F0 lerp and GGX roughness^2 stay numerically safe.
    metallic_ = std::clamp(m, 0.0F, 1.0F);
}

void MaterialInstance::set_roughness(float r) noexcept
{
    roughness_ = std::clamp(r, 0.0F, 1.0F);
}

void MaterialInstance::set_alpha_mode(AlphaMode m) noexcept
{
    // T1.10 — enum domain is already constrained; defensive default for
    // any caller that casts an out-of-range integer in.
    alpha_mode_ = (m == AlphaMode::kOpaque || m == AlphaMode::kMask || m == AlphaMode::kBlend)
                  ? m : AlphaMode::kOpaque;
}

void MaterialInstance::set_alpha_cutoff(float c) noexcept
{
    // T1.10 — glTF 2.0 §3.9.3 cutoff is in [0,1]. Saturate so the shader
    // discard branch stays well-defined.
    alpha_cutoff_ = std::clamp(c, 0.0F, 1.0F);
}

void MaterialInstance::set_alpha_params(const AlphaParams& p) noexcept
{
    // T1.10 — convenience wrapper: glTF loader writes the pair atomically.
    set_alpha_mode(p.mode);
    set_alpha_cutoff(p.cutoff);
}

void MaterialInstance::set_albedo(float r, float g, float b) noexcept
{
    // T1.12 — clamp to [0, kRtMaxRadiance]. Lower bound 0 keeps Schlick F0
    // lerps and reflection multiplications well-defined; upper bound is a
    // generous HDR cap so a runaway glTF baseColor cannot NaN the trace.
    constexpr float kRtMaxRadiance = 1024.0F;
    albedo_[0] = std::clamp(r, 0.0F, kRtMaxRadiance);
    albedo_[1] = std::clamp(g, 0.0F, kRtMaxRadiance);
    albedo_[2] = std::clamp(b, 0.0F, kRtMaxRadiance);
}

void MaterialInstance::set_emissive(float r, float g, float b) noexcept
{
    // T1.12 — same clamp domain as albedo so KHR_materials_emissive_strength
    // scaled values cannot push downstream sums to inf.
    constexpr float kRtMaxRadiance = 1024.0F;
    emissive_[0] = std::clamp(r, 0.0F, kRtMaxRadiance);
    emissive_[1] = std::clamp(g, 0.0F, kRtMaxRadiance);
    emissive_[2] = std::clamp(b, 0.0F, kRtMaxRadiance);
}

// ---- T1.12 — RT closest-hit sample stub helper ----------------------------
//
// Today the cd::material library does not own an RT pipeline (the closest-hit
// GLSL lives in samples/rhi/hello_rt/main.cpp as a string literal, and the
// hello_engine sample is FROZEN). This helper is the **stable contract** the
// future engine-owned RT pipeline will read: given a MaterialInstance, it
// returns the per-prim shading state a general-geometry closest-hit shader
// needs to shade a non-sphere hit (Sponza wall, curtain, vegetation).
//
// Defensive guarantee per T1.12 step 3: when `instance.is_valid()` is false
// — which happens for default-constructed inert instances, or instances
// whose device handles have been released — the helper still returns a
// well-formed `RayHitSample` with the neutral-grey albedo fallback. The
// `valid` flag lets the caller distinguish "real material data" from
// "fallback" without parsing the albedo values.

RayHitSample ray_hit_sample(const MaterialInstance& instance) noexcept
{
    RayHitSample s {};
    if (!instance.is_valid())
    {
        // Defensive fallback: neutral grey so the reflection shows
        // SOMETHING instead of black. Emissive stays zero so the
        // fallback path cannot accidentally light the scene.
        s.albedo[0] = kRayHitFallbackGrey;
        s.albedo[1] = kRayHitFallbackGrey;
        s.albedo[2] = kRayHitFallbackGrey;
        s.emissive[0] = 0.0F;
        s.emissive[1] = 0.0F;
        s.emissive[2] = 0.0F;
        s.metallic = 0.0F;
        s.roughness = 0.5F;
        s.valid = false;
        return s;
    }
    s.albedo[0] = instance.albedo_r();
    s.albedo[1] = instance.albedo_g();
    s.albedo[2] = instance.albedo_b();
    s.emissive[0] = instance.emissive_r();
    s.emissive[1] = instance.emissive_g();
    s.emissive[2] = instance.emissive_b();
    s.metallic = instance.metallic();
    s.roughness = instance.roughness();
    s.valid = true;
    return s;
}

cd::core::Result<MaterialInstance> MaterialInstance::create(cd::rhi::IDevice& device, const Material& material)
{
    if (!material.is_valid() || !material.has_descriptors())
    {
        return std::unexpected(
            material_errors::make(
                material_errors::Code::kInvalidArgument,
                "MaterialInstance::create: material has no descriptor bindings"
            )
        );
    }
    auto r = device.allocate_descriptor_set(material.descriptor_set_layout());
    if (!r.has_value())
    {
        return std::unexpected(
            material_errors::wrap(material_errors::Code::kPipelineCreationFailed, r.error())
        );
    }
    MaterialInstance inst;
    inst.device_ = &device;
    inst.desc_set_ = *r;
    return inst;
}

cd::core::Result<void> MaterialInstance::update(std::span<const cd::rhi::DescriptorWrite> writes)
{
    if (!is_valid())
    {
        return std::unexpected(
            material_errors::make(
                material_errors::Code::kInvalidArgument,
                "MaterialInstance::update: instance not initialized"
            )
        );
    }
    auto r = device_->update_descriptor_set(desc_set_, writes);
    if (!r.has_value())
    {
        return std::unexpected(material_errors::wrap(material_errors::Code::kInvalidArgument, r.error()));
    }
    return {};
}

void MaterialInstance::bind(cd::rhi::IDrawRecorder& cmd, std::uint32_t set_index) const
{
    if (!is_valid())
        return;
    cmd.bind_descriptor_set(set_index, desc_set_);
}

}  // namespace cd::material
