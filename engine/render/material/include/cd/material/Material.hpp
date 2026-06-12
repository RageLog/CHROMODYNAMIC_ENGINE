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
#include <cd/material/AlphaMode.hpp>
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
class IDrawRecorder;
} // namespace cd::rhi

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

    /// phase1135 (SL-D wave 1): optional, non-owning include resolver
    /// forwarded into every GLSL stage compile. Null keeps the legacy
    /// behaviour (#include is a compile error). Pair with
    /// cd::shader_lib::ModuleResolver to pull shader-library modules.
    cd::shader::IIncludeResolver* include_resolver { nullptr };

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

    /// phase864-multi-set: extra descriptor set layouts appended AFTER the
    /// material's own descriptor set (which always sits at set index 0).
    /// Each entry becomes set index 1, 2, ... in the pipeline layout. Used
    /// by the W8-BE bindless dedicated-set path so the bindless sampler2D
    /// array can live on its own descriptor set, isolated from the shared
    /// per-prim set (which phase 851 + 860 proved triggers a NVIDIA
    /// dynamic-index crash). Caller owns the layouts and is responsible
    /// for their lifetime — Material does NOT take ownership.
    std::span<const cd::rhi::DescriptorSetLayoutHandle> extra_set_layouts {};

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
    /// phase1121 (X1-FU-F step 3): takes the draw-subset recorder so
    /// materials can be applied inside parallel pass lanes; a full
    /// ICommandBuffer binds implicitly (it IS an IDrawRecorder).
    void apply(cd::rhi::IDrawRecorder& cmd) const;

private:
    void release() noexcept;
    void steal(Material&& other) noexcept;

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
    void bind(cd::rhi::IDrawRecorder& cmd, std::uint32_t set_index = 0) const;

    // ---- T1.9: metallic / roughness CPU-side accessors --------------------
    //
    // The Material descriptor-set ships metallic/roughness to the GPU via
    // the std140 PbrFactors UBO. These accessors expose the same scalars on
    // the CPU side so the engine can:
    //
    //   * write metallic into the G-buffer metallic_roughness MRT channel
    //     during the geometry / fill pass, instead of inferring it from the
    //     material kind enum (the bug pattern documented in
    //     docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md).
    //   * gate SSR / RT-reflection composite by the per-pixel metallic
    //     readback, not by a discrete surface_flag bucket.
    //
    // Both values are clamped to [0,1] on set — out-of-range input is
    // silently saturated so downstream BRDF math (Schlick F0 lerp, GGX
    // roughness^2) cannot see NaNs or negative weights.
    //
    // The accessors are pure CPU state; they do NOT touch the descriptor
    // set. The G-buffer writer pass (and any std140 packer) is responsible
    // for forwarding the values to the GPU on the next frame.

    [[nodiscard]] float metallic() const noexcept
    {
        return metallic_;
    }

    [[nodiscard]] float roughness() const noexcept
    {
        return roughness_;
    }

    void set_metallic(float m) noexcept;
    void set_roughness(float r) noexcept;

    // ---- T1.10: glTF alphaMode round-trip ---------------------------------
    //
    // The render-tier mirror of `cd::asset::gltf::GltfAlphaMode`. The glTF
    // loader sets these when ingesting a material; the shader / pipeline-
    // selection code reads them to:
    //   * MASK   → enable per-fragment discard against `alpha_cutoff_`
    //   * BLEND  → route through the alpha-blend pass (sort back-to-front,
    //              depth-test on / depth-write off)
    //   * OPAQUE → standard depth-tested + depth-written render
    // See docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md.
    [[nodiscard]] AlphaMode alpha_mode() const noexcept
    {
        return alpha_mode_;
    }

    [[nodiscard]] float alpha_cutoff() const noexcept
    {
        return alpha_cutoff_;
    }

    void set_alpha_mode(AlphaMode m) noexcept;
    void set_alpha_cutoff(float c) noexcept;
    void set_alpha_params(const AlphaParams& p) noexcept;

    // ---- T1.16: alpha-mode predicate accessors ----------------------------
    //
    // Downstream code can branch on alpha mode without re-querying the enum:
    //
    //   if (mat.is_blend()) route_to_blend_bucket();
    //   if (mat.is_mask())  set_alpha_test_threshold(mat.alpha_cutoff());
    //
    // These are trivially inlined; no virtual dispatch, no extra state.

    [[nodiscard]] bool is_opaque() const noexcept
    {
        return alpha_mode_ == AlphaMode::kOpaque;
    }

    [[nodiscard]] bool is_mask() const noexcept
    {
        return alpha_mode_ == AlphaMode::kMask;
    }

    [[nodiscard]] bool is_blend() const noexcept
    {
        return alpha_mode_ == AlphaMode::kBlend;
    }

    // ---- T1.12: CPU-side albedo + emissive accessors ----------------------
    //
    // The metallic / roughness accessors above let the G-buffer fill pass
    // forward the two scalar BRDF knobs into MRT slot 2 without inferring
    // them from a material-kind enum (the surface_flag bug pattern). T1.12
    // extends the same idea to the two RGB values an RT closest-hit shader
    // needs to shade a general-geometry hit:
    //
    //   * albedo   — baseColor.rgb (alpha lives on alpha_mode / alpha_cutoff).
    //   * emissive — KHR_materials_emissive_strength scaled emissive.rgb.
    //
    // The accessors are pure CPU state. The Sponza glTF loader writes them
    // when it builds each prim's MaterialInstance; the (future) RT hit-shader
    // record builder reads them via `cd::material::ray_hit_sample` and packs
    // them into the per-instance SSBO that the closest-hit GLSL samples on
    // `gl_InstanceCustomIndexEXT`.
    //
    // Defaults mirror `PbrFactors`: albedo = (1,1,1), emissive = (0,0,0).
    // Components are clamped into [0, kRtMaxRadiance] on set; the upper
    // bound prevents NaN propagation if a glTF importer ships a runaway
    // emissive strength (HDR scenes commonly run into the hundreds, so the
    // cap is generous — but finite).

    [[nodiscard]] float albedo_r() const noexcept { return albedo_[0]; }
    [[nodiscard]] float albedo_g() const noexcept { return albedo_[1]; }
    [[nodiscard]] float albedo_b() const noexcept { return albedo_[2]; }

    [[nodiscard]] float emissive_r() const noexcept { return emissive_[0]; }
    [[nodiscard]] float emissive_g() const noexcept { return emissive_[1]; }
    [[nodiscard]] float emissive_b() const noexcept { return emissive_[2]; }

    void set_albedo(float r, float g, float b) noexcept;
    void set_emissive(float r, float g, float b) noexcept;

private:
    void release() noexcept;
    void steal(MaterialInstance&& other) noexcept;

    cd::rhi::IDevice* device_ { nullptr };
    cd::rhi::DescriptorSetHandle desc_set_ {};
    // T1.9 — match PbrFactors defaults (dielectric, half-rough).
    float metallic_ { 0.0F };
    float roughness_ { 0.5F };
    // T1.10 — glTF alphaMode defaults (opaque, standard cutoff).
    AlphaMode alpha_mode_ { AlphaMode::kOpaque };
    float alpha_cutoff_ { 0.5F };
    // T1.12 — RT closest-hit sample state. Defaults match PbrFactors
    // (white diffuse, no emissive).
    float albedo_[3]   { 1.0F, 1.0F, 1.0F };
    float emissive_[3] { 0.0F, 0.0F, 0.0F };
};

// ---- T1.12 — RT closest-hit sample stub helper -----------------------------
//
// Future integration point for the cd::material RT closest-hit shader (or its
// equivalent shader-record table). When an RT reflection / GI / probe ray
// hits a non-sphere geometry, the closest-hit shader needs the hit material's
// albedo + emissive to shade the sample correctly — without that, the chrome
// PBR sphere reflection of a Sponza scene shows only sky + nearby spheres
// because every general-geometry hit falls back to black or to the IBL miss
// branch.
//
// The full integration (closest-hit GLSL branch on hit_kind, per-instance
// SBT albedo SSBO, barycentric UV interpolation) is deferred behind the
// inline-GLSL-string boundary in samples/rhi/hello_rt and the eventual
// engine RT pipeline. This header-only helper is the **stable contract**:
// the CPU-side data the future shader-record builder will pack.
//
// Returned by value. Contains everything an RT hit needs to shade a sample:
//   - albedo   (RGB; vec3-as-3-float for std140 ergonomics)
//   - emissive (RGB)
//   - metallic + roughness (forwarded from the MaterialInstance accessors —
//     duplicated here so the future SBT-record builder reads ONE struct
//     per prim instead of three live MaterialInstance fields).
//   - valid    : false when the source instance is inert (no device, no
//                descriptor set); the caller must use the neutral-grey
//                fallback in that case to keep the reflection visible.
//
// Defensive guarantee: when the MaterialInstance is inert / invalid,
// `ray_hit_sample` returns `valid = false` AND fills albedo with the
// neutral-grey fallback (0.6, 0.6, 0.6) so a closest-hit shader that
// blindly multiplies `payload.color *= sample.albedo` still shows
// SOMETHING in the reflection instead of black. Emissive falls back to
// zero so the fallback path cannot light the scene by accident.
struct RayHitSample
{
    float albedo[3]   { 0.6F, 0.6F, 0.6F };   ///< neutral-grey fallback
    float emissive[3] { 0.0F, 0.0F, 0.0F };
    float metallic   { 0.0F };
    float roughness  { 0.5F };
    bool  valid      { false };
};

/// Neutral-grey fallback used when the source MaterialInstance is inert.
/// 0.6 matches the audit doc note on "show SOMETHING instead of black"
/// (docs/AUDIT/learned-lessons-pbr-rt-and-curtain-alpha-2026-06-03.md,
/// Bug A, T1.12 step 3) and is high enough to read as a real reflection
/// against a typical HDR sky background but low enough to look like an
/// unresolved diffuse stand-in, not a lit surface.
inline constexpr float kRayHitFallbackGrey = 0.6F;

/// Sample the per-prim shading state an RT closest-hit shader would read.
/// Defensive against inert `MaterialInstance` per the T1.12 contract.
[[nodiscard]] RayHitSample ray_hit_sample(const MaterialInstance& instance) noexcept;

}  // namespace cd::material
