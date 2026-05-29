// =============================================================================
// CHROMODYNAMIC — cd/material/Material.hpp
// Sprint S3.8 — render-tier `Material` layer.
//
// A Material bundles the GPU artifacts needed to draw with a specific shader
// pair: vertex + fragment shader modules, descriptor-set layout, pipeline
// layout, graphics pipeline. It is RAII over `cd::rhi::IDevice` and never
// requires the caller to manage individual RHI handle lifetimes.
//
// A MaterialInstance is the parameterized usage of a Material — it owns a
// descriptor set allocated from the material's set layout. Bind a Material
// once per pipeline change, then bind one or more MaterialInstances inside.
//
// The Material can be constructed two ways:
//   1. From pre-compiled SPIR-V (no compiler needed) — for asset pipelines.
//   2. From inline GLSL source via `cd::shader::ICompiler` — for development
//      and hot-reload workflows.
//
// The library does NOT depend on a particular shader backend: any
// `cd::shader::ICompiler` implementation (glslang today, Slang later) works.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/rhi/Pipeline.hpp>
#include <cd/shader/Compiler.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace cd::rhi
{
class ICommandBuffer;
}

namespace cd::material
{

// ---- Error domain -----------------------------------------------------------

namespace material_errors
{
inline constexpr std::uint32_t kDomain = 0x000B;

enum class Code : std::uint32_t
{
    kOk = 0,
    kShaderCompileFailed = 1,
    kPipelineCreationFailed = 2,
    kInvalidArgument = 3,
    kCompilerRequired = 4,  ///< GLSL source supplied but no compiler available.
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}

/// Re-tag an upstream ErrorCode with material_errors::Code, preserving
/// owned diagnostic storage (e.g. glslang infoLog). Prefer this over
/// `make(code, upstream.message)` because the latter only captures the
/// `string_view`, losing the upstream's owning storage when the
/// upstream is destructed.
[[nodiscard]] inline cd::core::ErrorCode wrap(Code c, const cd::core::ErrorCode& upstream) noexcept
{
    return cd::core::ErrorCode::rewrap(kDomain, static_cast<std::uint32_t>(c), upstream);
}
}  // namespace material_errors

// ---- Material description --------------------------------------------------

struct MaterialDesc
{
    // -------- Shader inputs (one of the two pairs must be non-empty) ---------

    /// Inline GLSL source — compiled at create() time via the supplied
    /// `ICompiler*`. Ignored when `*_spirv` is non-empty.
    std::string_view vertex_glsl {};
    std::string_view fragment_glsl {};

    /// On-disk GLSL source — opt-in alternative to inline `vertex_glsl` /
    /// `fragment_glsl`. When set, `Material::create` reads the file into a
    /// scratch `std::string` and feeds it to the existing GLSL compile
    /// path; the underlying file handle does not survive the create call.
    /// Path is resolved relative to the process working directory (no
    /// implicit search root for V1 — callers that need search-root
    /// resolution should compose the path themselves).
    ///
    /// Precedence (per ADR-20260529-X5):
    ///   *_spirv  >  *_glsl_path  >  *_glsl
    /// i.e. `*_glsl_path` wins over a non-empty inline `*_glsl`. The two
    /// are not mutually exclusive at the type level so callers can ship an
    /// embedded fallback string alongside the on-disk path.
    std::string_view vertex_glsl_path {};
    std::string_view fragment_glsl_path {};

    /// Pre-compiled SPIR-V — takes precedence over GLSL. Used by asset
    /// pipelines that ship .spv blobs.
    std::span<const std::uint32_t> vertex_spirv {};
    std::span<const std::uint32_t> fragment_spirv {};

    // -------- Pipeline configuration -----------------------------------------

    /// Render target color formats (dynamic-rendering pipeline creation).
    std::span<const cd::rhi::Format> color_attachment_formats {};
    /// Optional depth attachment format. kUndefined disables depth attachment.
    cd::rhi::Format depth_attachment_format { cd::rhi::Format::kUndefined };

    /// Optional vertex input layout (empty for procedural geometry — i.e.
    /// fully positions in the shader, `gl_VertexIndex`-style triangles).
    std::span<const cd::rhi::VertexBinding> vertex_bindings {};
    std::span<const cd::rhi::VertexAttribute> vertex_attributes {};

    /// Optional descriptor set layout binding for material parameters. Empty
    /// for materials with no per-instance parameters (e.g. the trivial flat-
    /// color triangle).
    std::span<const cd::rhi::DescriptorSetLayoutBinding> descriptor_bindings {};

    /// Optional push-constant ranges. Each range declares the byte window
    /// of the user-provided constant block visible to the listed shader
    /// stages. Empty (default) means the pipeline layout has no push range.
    std::span<const cd::rhi::PushConstantRange> push_constants {};

    cd::rhi::PrimitiveTopology topology { cd::rhi::PrimitiveTopology::kTriangleList };
    cd::rhi::RasterState raster {};
    cd::rhi::DepthStencilState depth_stencil {};
    /// Optional per-attachment blend state. Empty (default) leaves blend
    /// disabled so writes overwrite. Bloom upsample uses additive blend.
    std::span<const cd::rhi::BlendAttachmentState> blend_attachments {};
    cd::rhi::SampleCount samples { cd::rhi::SampleCount::k1 };

    /// Diagnostic label, surfaced in compiler error messages.
    std::string_view name { "material" };
};

// ---- Material ---------------------------------------------------------------

class Material
{
public:
    /// Build a Material. `compiler` may be null when both `vertex_spirv` and
    /// `fragment_spirv` are non-empty; otherwise it is required for GLSL
    /// compilation. The Material owns every RHI handle it creates; destroying
    /// the Material releases them in LIFO order.
    [[nodiscard]] static cd::core::Result<Material>
    create(cd::rhi::IDevice& device, cd::shader::ICompiler* compiler, const MaterialDesc& desc);

    Material() noexcept = default;  ///< Constructs an inert Material (no RHI handles).
    ~Material();
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return device_ != nullptr;
    }

    [[nodiscard]] cd::rhi::GraphicsPipelineHandle pipeline() const noexcept
    {
        return pipeline_;
    }

    [[nodiscard]] cd::rhi::PipelineLayoutHandle pipeline_layout() const noexcept
    {
        return pipeline_layout_;
    }

    [[nodiscard]] cd::rhi::DescriptorSetLayoutHandle descriptor_set_layout() const noexcept
    {
        return desc_layout_;
    }

    [[nodiscard]] bool has_descriptors() const noexcept
    {
        return has_descriptors_;
    }

    /// Record vkCmdBindPipeline. Call once per pipeline change.
    void apply(cd::rhi::ICommandBuffer& cmd) const;

private:
    void release_() noexcept;
    void steal_(Material&& other) noexcept;

    cd::rhi::IDevice* device_ { nullptr };
    cd::rhi::ShaderModuleHandle vs_ {};
    cd::rhi::ShaderModuleHandle fs_ {};
    cd::rhi::DescriptorSetLayoutHandle desc_layout_ {};
    cd::rhi::PipelineLayoutHandle pipeline_layout_ {};
    cd::rhi::GraphicsPipelineHandle pipeline_ {};
    bool has_descriptors_ { false };
};

// ---- Material instance ------------------------------------------------------

class MaterialInstance
{
public:
    /// Allocate a descriptor set from the material's set layout. Returns
    /// kInvalidArgument if the material has no descriptor bindings.
    [[nodiscard]] static cd::core::Result<MaterialInstance> create(cd::rhi::IDevice& device, const Material& material);

    MaterialInstance() noexcept = default;
    ~MaterialInstance();
    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;
    MaterialInstance(MaterialInstance&& other) noexcept;
    MaterialInstance& operator=(MaterialInstance&& other) noexcept;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return device_ != nullptr;
    }

    [[nodiscard]] cd::rhi::DescriptorSetHandle descriptor_set() const noexcept
    {
        return desc_set_;
    }

    /// Apply per-instance parameter writes (UBO, texture/sampler). Forwards
    /// to `IDevice::update_descriptor_set` against this instance's set.
    [[nodiscard]] cd::core::Result<void> update(std::span<const cd::rhi::DescriptorWrite> writes);

    /// Record vkCmdBindDescriptorSets at the given set index. Call after the
    /// owning Material's `apply()`.
    void bind(cd::rhi::ICommandBuffer& cmd, std::uint32_t set_index = 0) const;

private:
    void release_() noexcept;
    void steal_(MaterialInstance&& other) noexcept;

    cd::rhi::IDevice* device_ { nullptr };
    cd::rhi::DescriptorSetHandle desc_set_ {};
};

}  // namespace cd::material
