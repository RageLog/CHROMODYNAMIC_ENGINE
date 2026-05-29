// =============================================================================
// CHROMODYNAMIC — cd/material/Material.cpp
// =============================================================================
#include <cd/material/Material.hpp>
#include <cd/rhi/ICommandBuffer.hpp>

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
    d.target = cd::shader::TargetEnv::kVulkan_1_3;
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
    release_();
}

Material::Material(Material&& other) noexcept
{
    steal_(std::move(other));
}

Material& Material::operator=(Material&& other) noexcept
{
    if (this != &other)
    {
        release_();
        steal_(std::move(other));
    }
    return *this;
}

void Material::release_() noexcept
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

void Material::steal_(Material&& other) noexcept
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
    std::span<const cd::rhi::DescriptorSetLayoutHandle> set_layout_span {};
    if (m.has_descriptors_)
    {
        set_layout_span = std::span<const cd::rhi::DescriptorSetLayoutHandle>(&set_layout_handle, 1);
    }
    pld.set_layouts = set_layout_span;
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

void Material::apply(cd::rhi::ICommandBuffer& cmd) const
{
    if (!is_valid())
        return;
    cmd.bind_graphics_pipeline(pipeline_);
}

// ---- MaterialInstance -------------------------------------------------------

MaterialInstance::~MaterialInstance()
{
    release_();
}

MaterialInstance::MaterialInstance(MaterialInstance&& other) noexcept
{
    steal_(std::move(other));
}

MaterialInstance& MaterialInstance::operator=(MaterialInstance&& other) noexcept
{
    if (this != &other)
    {
        release_();
        steal_(std::move(other));
    }
    return *this;
}

void MaterialInstance::release_() noexcept
{
    if (device_ == nullptr)
        return;
    if (desc_set_.is_valid())
        device_->destroy_descriptor_set(desc_set_);
    device_ = nullptr;
    desc_set_ = {};
}

void MaterialInstance::steal_(MaterialInstance&& other) noexcept
{
    device_ = other.device_;
    desc_set_ = other.desc_set_;
    other.device_ = nullptr;
    other.desc_set_ = {};
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

void MaterialInstance::bind(cd::rhi::ICommandBuffer& cmd, std::uint32_t set_index) const
{
    if (!is_valid())
        return;
    cmd.bind_descriptor_set(set_index, desc_set_);
}

}  // namespace cd::material
