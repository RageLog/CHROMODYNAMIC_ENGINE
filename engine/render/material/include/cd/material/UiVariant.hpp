// =============================================================================
// CHROMODYNAMIC — cd/material/UiVariant.hpp
// M3 W1B Sprint-1 — Route A foundation for the editor's UI material.
//
// The editor (`apps/editor`) currently routes UI draws through a tactical
// bridge (Route B, owned by parallel agent W1A). The strategic path is
// Route A: a first-class `cd::material::UiVariant` that bundles the UI
// pipeline state + descriptor layout key needed to render
// `cd::ui::renderer::DrawBatcher` output through `cd::material::Material`'s
// existing factory. When this header is mature enough to replace the
// tactical bridge, `cd::ui::renderer_rhi::Submitter` will switch its
// pipeline construction to call `create_ui_variant()` instead of rolling
// its own `MaterialDesc`. That Submitter wire-up is M3 W2 / M4 — NOT this
// sprint.
//
// Sprint-1 scope (this commit):
//   * Public `UiVariantSpec` + `UiVariant` types.
//   * `create_ui_variant(device, spec)` factory that:
//       - resolves the vertex format to a binding+attribute pair that
//         matches `cd::ui::renderer::Vertex` (pos2 + uv2 + RGBA8 +
//         variant byte + 3 pad bytes; 24 bytes/stride);
//       - resolves blend / depth from the spec (defaults = alpha blend
//         enabled, depth disabled — the canonical UI recipe);
//       - exposes a *vertex-color-only* fragment shader (no glyph SDF
//         sampler yet, no theme UBO yet — see Sprint-2 notes below);
//       - delegates the actual pipeline creation to
//         `cd::material::Material::create()` so we never duplicate the
//         RHI plumbing.
//
// Sprint-2 scope (NOT this commit):
//   * Glyph SDF atlas sampler (`texture_count = 1`).
//   * Theme color palette UBO (`theme_palette_ubo_binding`).
//   * `cd::ui::renderer_rhi::Submitter` wire-up to Route A.
//
// Vertex layout contract (Sprint-1):
//   binding 0, stride 24, per-vertex
//     loc 0  vec2  pos      (offset 0)
//     loc 1  vec2  uv       (offset 8)
//     loc 2  vec4  color    (RGBA8 unorm, offset 16)
//     loc 3  uint  flags    (8-bit variant + 3 pad bytes, offset 20)
//
// The shader strings are kept inline (header-resident `kUiVariantVS` /
// `kUiVariantFS`) so library consumers can call the factory without
// supplying a compiler argument override.
//
// Spec defaults (UiVariantSpec):
//   vertex_format               = kUiDefault   (matches DrawBatcher::Vertex)
//   blend_state                 = alpha        (premultiplied-friendly)
//   depth_state                 = disabled     (UI = depth off)
//   texture_count               = 0            (Sprint-1)
//   theme_palette_ubo_binding   = -1           (Sprint-1: unused)
//   color_attachment_formats    = {kRGBA8Unorm}  (default UI target)
//
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/material/Material.hpp>
#include <cd/rhi/Enums.hpp>
#include <cd/rhi/Format.hpp>

#include <cstdint>
#include <span>
#include <string_view>

namespace cd::rhi
{
class IDevice;
class ICommandBuffer;
}  // namespace cd::rhi

namespace cd::material
{

// ---- Vertex format enum ----------------------------------------------------

/// Selector for the vertex input layout the UI pipeline binds. Sprint-1
/// only defines the canonical UI vertex (`kUiDefault`, matching
/// `cd::ui::renderer::Vertex`). Future entries (e.g. an instanced
/// per-quad layout) can extend this without breaking ABI.
enum class UiVertexFormat : std::uint8_t
{
    /// pos2 + uv2 + RGBA8 + variant+pad. Stride 24. Matches
    /// `cd::ui::renderer::Vertex`.
    kUiDefault = 0,
};

// ---- Blend / depth selectors -----------------------------------------------

/// Blend mode for the UI variant. `kAlpha` is the canonical
/// "src.a * src + (1 - src.a) * dst" UI recipe. `kPremultiplied`
/// matches a premultiplied-alpha pipeline (font atlases that pre-mix).
/// `kOpaque` disables blending entirely (debug overlays).
enum class UiBlendMode : std::uint8_t
{
    kAlpha = 0,         ///< standard alpha-over (default)
    kPremultiplied = 1, ///< src * 1 + dst * (1 - srcAlpha)
    kOpaque = 2,        ///< blend disabled
};

/// Depth state selector. UI is typically `kDisabled` (the canonical
/// CHROMODYNAMIC UI pass runs after the scene depth-test is done and
/// does not participate in z-rejection).
enum class UiDepthMode : std::uint8_t
{
    kDisabled = 0,  ///< depth_test = depth_write = off (default)
    kReadOnly = 1,  ///< depth_test = on, depth_write = off (debug overlay over scene)
};

// ---- Variant spec -----------------------------------------------------------

/// Description of the UI variant the factory should produce. All
/// defaults match the canonical UI recipe (alpha blend, depth off,
/// vertex color only) — passing a default-constructed spec to
/// `create_ui_variant()` yields a working Sprint-1 pipeline.
struct UiVariantSpec
{
    /// Selects the vertex input layout. Sprint-1: only `kUiDefault`.
    UiVertexFormat vertex_format { UiVertexFormat::kUiDefault };

    /// Blend recipe. Default = alpha-over (`kAlpha`).
    UiBlendMode blend_state { UiBlendMode::kAlpha };

    /// Depth recipe. Default = disabled (`kDisabled`).
    UiDepthMode depth_state { UiDepthMode::kDisabled };

    /// Number of sampler2D bindings the fragment shader expects.
    ///   * 0 in Sprint-1 (vertex color only — no glyph sampler yet).
    ///   * 1 reserved for Sprint-2 (SDF glyph / sprite atlas).
    /// Any value > 1 is rejected by the factory (kInvalidArgument).
    std::uint32_t texture_count { 0U };

    /// Binding slot for the theme color palette UBO. `-1` means
    /// "no theme UBO bound" — the Sprint-1 default. Sprint-2 will
    /// honour any non-negative value by allocating a corresponding
    /// `kUniformBuffer` binding in the descriptor set layout.
    std::int32_t theme_palette_ubo_binding { -1 };

    /// Render-target color attachment formats the pipeline will
    /// render INTO. Must match the active render pass when the
    /// variant is bound. Default = single `kRGBA8Unorm`.
    std::span<const cd::rhi::Format> color_attachment_formats {};

    /// Optional depth attachment format. `kUndefined` (default)
    /// disables the depth attachment entirely.
    cd::rhi::Format depth_attachment_format { cd::rhi::Format::kUndefined };

    /// Diagnostic label propagated to the underlying Material's pipeline.
    std::string_view name { "ui_variant" };
};

// ---- Sprint-1 default shaders ----------------------------------------------

/// Vertex shader source. Outputs clip-space position from the
/// `pos2 + uv2 + RGBA8 + flags` vertex format; passes UV + color +
/// variant byte through to the fragment stage. The viewport-to-clip
/// transform is supplied via push-constant for Sprint-1 simplicity
/// (Sprint-2 will switch to a per-pass UBO).
inline constexpr std::string_view kUiVariantSprint1VS = R"glsl(
#version 450
layout(push_constant) uniform Push { vec2 inv_viewport; } u_push;

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_flags;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) flat out uint v_variant;

void main()
{
    // Map [0..viewport] -> NDC [-1..+1]. Y is flipped to match UI top-left.
    vec2 ndc = in_pos * u_push.inv_viewport * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_variant = in_flags & 0xFFu;
}
)glsl";

/// Fragment shader source. Sprint-1: vertex color only (no
/// texture sampling, no theme palette). Sprint-2 will add the
/// glyph SDF + theme UBO branches.
inline constexpr std::string_view kUiVariantSprint1FS = R"glsl(
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in uint v_variant;

layout(location = 0) out vec4 out_color;

void main()
{
    // Sprint-1: vertex color only. Variant byte ignored (placeholder).
    out_color = v_color;
}
)glsl";

// ---- Variant object ---------------------------------------------------------

/// A UiVariant bundles the pipeline state + handles needed to render
/// `cd::ui::renderer::DrawBatcher` output. It owns a `cd::material::Material`
/// internally so the underlying pipeline / pipeline layout / descriptor
/// set layout are RAII-released alongside the variant.
///
/// Sprint-1: the variant exposes its pipeline + pipeline-layout +
/// descriptor-set-layout handles via the inner `Material`, plus a
/// `descriptor_layout_key()` that downstream consumers (e.g. a future
/// `cd::ui::renderer_rhi::Submitter` wired to Route A) can use to cache
/// per-variant descriptor sets.
class UiVariant
{
public:
    UiVariant() noexcept = default;
    ~UiVariant() = default;
    UiVariant(const UiVariant&) = delete;
    UiVariant& operator=(const UiVariant&) = delete;
    UiVariant(UiVariant&&) noexcept = default;
    UiVariant& operator=(UiVariant&&) noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return material_.is_valid();
    }

    [[nodiscard]] const Material& material() const noexcept
    {
        return material_;
    }

    [[nodiscard]] Material& material_mut() noexcept
    {
        return material_;
    }

    /// Stable key identifying the descriptor-set layout shape (texture
    /// count + theme UBO binding). Future caches keyed by variant
    /// shape can use this without recomputing it from the spec.
    [[nodiscard]] std::uint64_t descriptor_layout_key() const noexcept
    {
        return descriptor_layout_key_;
    }

    /// Sprint-1: 0 textures. Sprint-2: 1 (glyph SDF).
    [[nodiscard]] std::uint32_t texture_count() const noexcept
    {
        return texture_count_;
    }

    /// Convenience: bind the variant's pipeline for the next draw.
    /// Equivalent to `material().apply(cmd)`. Inert when the variant
    /// is default-constructed.
    void apply(cd::rhi::ICommandBuffer& cmd) const
    {
        material_.apply(cmd);
    }

private:
    // Hide the constructor body from public callers; only the factory
    // assembles a valid UiVariant.
    friend cd::core::Result<UiVariant> create_ui_variant(cd::rhi::IDevice&, const UiVariantSpec&);

    Material      material_ {};
    std::uint64_t descriptor_layout_key_ { 0U };
    std::uint32_t texture_count_         { 0U };
};

// ---- Factory ----------------------------------------------------------------

/// Build a UiVariant from the supplied spec. Sprint-1 only accepts
/// `texture_count == 0` and ignores `theme_palette_ubo_binding`
/// (Sprint-2 will honour both). Returns `kInvalidArgument` for any
/// out-of-range field. Returns the underlying error tagged with
/// `kPipelineCreationFailed` for RHI failures.
///
/// Note: this factory does NOT take a `cd::shader::ICompiler*`. It
/// uses an internally-managed glslang compiler when the engine is
/// built with `CD_ENABLE_GLSLANG` (the default). When the engine is
/// built without glslang, the factory returns `kCompilerRequired` —
/// callers in that configuration must build their UI variant directly
/// via `Material::create()` from pre-compiled SPIR-V (Sprint-2 will
/// ship the precompiled `.spv` blobs alongside this header).
[[nodiscard]] cd::core::Result<UiVariant>
create_ui_variant(cd::rhi::IDevice& device, const UiVariantSpec& spec);

}  // namespace cd::material
