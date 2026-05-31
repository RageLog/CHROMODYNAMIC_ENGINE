// =============================================================================
// CHROMODYNAMIC — cd/material/UiVariant.hpp
// M3 W1B Route A — UI material factory for the editor + ui_renderer_rhi.
//
// The editor (`apps/editor`) currently routes UI draws through a tactical
// bridge (Route B, owned by parallel agent W1A). The strategic path is
// Route A: a first-class `cd::material::UiVariant` that bundles the UI
// pipeline state + descriptor layout key needed to render
// `cd::ui::renderer::DrawBatcher` output through `cd::material::Material`'s
// existing factory. When this header is mature enough to replace the
// tactical bridge, `cd::ui::renderer_rhi::Submitter` will switch its
// pipeline construction to call `create_ui_variant()` instead of rolling
// its own `MaterialDesc`. That Submitter wire-up (the gate flip) is
// Sprint-3 — NOT this sprint.
//
// Sprint-1 (phase 555) scope — DONE:
//   * Public `UiVariantSpec` + `UiVariant` types.
//   * `create_ui_variant(device, spec)` factory that resolves the
//     vertex format / blend / depth and exposes a vertex-color-only
//     fragment shader (no glyph SDF, no theme UBO).
//
// Sprint-2 (phase 573, THIS commit) scope:
//   * `UiVariantSpec::theme_palette_ubo_slot`   (default 0) — descriptor
//     binding for a Sprint-2 theme UBO carrying a 4-vec4 subset of the
//     `cd::ui::theme::PaletteV2`: primary, secondary, surface, on_surface
//     (the four roles widget code actually paints with). The UBO is bound
//     for the fragment stage only; the vertex stage continues to use the
//     pos/uv/color stream verbatim. Std140 layout: 64 bytes per UBO,
//     4 × vec4 in slot order.
//   * `UiVariantSpec::sdf_font_sampler_slot`    (default 1) — descriptor
//     binding for a Sprint-2 glyph SDF atlas (combined image+sampler).
//     The fragment shader gates the sample on `uv.x in [0..1]` AND
//     `texture_count > 0` so non-glyph quads still render via the
//     vertex-color path. The atlas "zero level" (the SDF mid-grey) is
//     128 / 255 ≈ 0.502 — the shader applies `smoothstep` around that
//     value to produce a 1-px AA glyph edge.
//   * Spec accepts `texture_count` in {0, 1}. When `texture_count == 1`,
//     the factory allocates a combined image+sampler descriptor slot at
//     `sdf_font_sampler_slot`. When `texture_count == 0` (theme-UBO-only
//     path or pure vertex-color path), no sampler descriptor is allocated.
//   * Tests: (a) variant with SDF sampler bound dispatches OK, (b) theme
//     UBO update reflects in subsequent draws (descriptor_set update is
//     observable via MaterialInstance::update()).
//
// Sprint-2 does NOT flip the Submitter gate. `cd::ui::renderer_rhi::Submitter`
// continues to follow the Route-B tactical path for now; a future agent in
// Sprint-3 flips the gate once both halves of Route A are battle-tested.
//
// Vertex layout contract (unchanged Sprint-1 -> Sprint-2):
//   binding 0, stride 24, per-vertex
//     loc 0  vec2  pos      (offset 0)
//     loc 1  vec2  uv       (offset 8)
//     loc 2  vec4  color    (RGBA8 unorm, offset 16)
//     loc 3  uint  flags    (8-bit variant + 3 pad bytes, offset 20)
//
// The shader strings are kept inline (header-resident `kUiVariantSprint1*`
// for the Sprint-1 legacy path, `kUiVariantSprint2*` for the Sprint-2 SDF
// + theme path) so library consumers can call the factory without
// supplying a compiler argument override.
//
// Spec defaults (UiVariantSpec):
//   vertex_format               = kUiDefault   (matches DrawBatcher::Vertex)
//   blend_state                 = alpha        (premultiplied-friendly)
//   depth_state                 = disabled     (UI = depth off)
//   texture_count               = 0            (no SDF sampler)
//   theme_palette_ubo_binding   = -1           (legacy Sprint-1 reject)
//   theme_palette_ubo_slot      = 0            (Sprint-2: binding=0 when bound)
//   sdf_font_sampler_slot       = 1            (Sprint-2: binding=1 when bound)
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
    ///   * 0 = no glyph sampler (vertex-color or theme-only path).
    ///   * 1 = SDF glyph / sprite atlas bound at `sdf_font_sampler_slot`.
    /// Any value > 1 is rejected by the factory (kInvalidArgument).
    std::uint32_t texture_count { 0U };

    /// Legacy Sprint-1 marker — kept for ABI compat. The Sprint-1 factory
    /// rejected any non-negative value as "Sprint-2 territory"; the
    /// Sprint-2 factory accepts negative values (=no theme UBO) and
    /// rejects any non-negative value to steer callers towards
    /// `theme_palette_ubo_slot` instead. Sprint-3 will remove this field.
    std::int32_t theme_palette_ubo_binding { -1 };

    /// Sprint-2 — descriptor-set binding slot for the theme color
    /// palette UBO. Default = 0. The UBO carries 4 vec4 (std140, 64
    /// bytes total) in slot order:
    ///   binding[0] = primary
    ///   binding[1] = secondary
    ///   binding[2] = surface
    ///   binding[3] = on_surface
    /// The fragment shader multiplies the resolved color by the
    /// primary swatch (downstream Sprint-3 will switch to per-quad
    /// theme-role selection via the variant byte). The UBO is bound
    /// for the fragment stage only.
    std::uint32_t theme_palette_ubo_slot { 0U };

    /// Sprint-2 — descriptor-set binding slot for the glyph SDF atlas
    /// (combined image+sampler). Default = 1. Only consulted when
    /// `texture_count == 1`. Must differ from `theme_palette_ubo_slot`
    /// (the factory rejects an overlap with kInvalidArgument).
    std::uint32_t sdf_font_sampler_slot { 1U };

    /// Sprint-2 opt-in: when true, the factory allocates a theme UBO
    /// descriptor at `theme_palette_ubo_slot` and routes the fragment
    /// stage through the Sprint-2 shader (vertex color * theme tint).
    /// When false, the variant stays on the Sprint-1 vertex-color-only
    /// path UNLESS `texture_count == 1` (in which case the Sprint-2
    /// shader is selected anyway because the SDF branch requires the
    /// theme tint multiplier). Default = false so a default-constructed
    /// spec yields the Sprint-1 variant byte-for-byte.
    bool use_theme_palette_ubo { false };

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
/// texture sampling, no theme palette).
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

// -----------------------------------------------------------------------------
// Sprint-2 shaders — theme palette UBO + SDF font sampler.
// -----------------------------------------------------------------------------

/// Vertex shader is identical to Sprint-1 (same vertex format, same
/// push-constant). Sprint-2 only diverges in the fragment stage.
inline constexpr std::string_view kUiVariantSprint2VS = kUiVariantSprint1VS;

/// Fragment shader source — Sprint-2. Samples the SDF atlas when
/// `uv.x in [0..1]` AND the sampler descriptor is bound; multiplies
/// the resulting color by the theme primary swatch from the UBO.
///
/// The descriptor layout is fixed at runtime by the factory; the
/// shader source is generated dynamically (see UiVariant.cpp's
/// `make_sprint2_fs()`) so the actual binding numbers match the
/// caller's `theme_palette_ubo_slot` / `sdf_font_sampler_slot` spec.
/// This header constant is the canonical reference layout that the
/// generator falls back to when both slots take their default values
/// (UBO=0, SDF=1).
inline constexpr std::string_view kUiVariantSprint2FS_Reference = R"glsl(
#version 450

layout(set = 0, binding = 0) uniform ThemePalette {
    vec4 primary;
    vec4 secondary;
    vec4 surface;
    vec4 on_surface;
} u_theme;

layout(set = 0, binding = 1) uniform sampler2D u_sdf_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in uint v_variant;

layout(location = 0) out vec4 out_color;

void main()
{
    // Baseline = vertex color (matches Sprint-1).
    vec4 base = v_color;

    // SDF branch: only sample inside the [0..1] glyph quad. The atlas
    // stores a unit-distance field around the "zero level" at 128/255
    // (~0.502). smoothstep around that mid-grey gives a 1-px AA edge.
    const float kSdfZero = 128.0 / 255.0;
    if (v_uv.x >= 0.0 && v_uv.x <= 1.0 &&
        v_uv.y >= 0.0 && v_uv.y <= 1.0)
    {
        float d = texture(u_sdf_atlas, v_uv).r;
        float a = smoothstep(kSdfZero - 0.0625, kSdfZero + 0.0625, d);
        base.a *= a;
    }

    // Theme tint: multiply by the primary swatch. The variant byte
    // currently selects between primary (=0) and surface (=1); future
    // Sprint-3 will expose secondary + on_surface for filled chips and
    // disabled-state widget paint.
    vec4 tint = (v_variant == 1u) ? u_theme.surface : u_theme.primary;
    out_color = base * tint;
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

    /// Sprint-1: 0 textures. Sprint-2: 0 or 1 (glyph SDF).
    [[nodiscard]] std::uint32_t texture_count() const noexcept
    {
        return texture_count_;
    }

    /// True when the variant was built with a Sprint-2 theme palette
    /// UBO descriptor (i.e. `theme_palette_ubo_slot` allocated). Used by
    /// downstream Submitter code to decide whether to upload a
    /// `ThemePalette` UBO before binding the descriptor set.
    [[nodiscard]] bool has_theme_ubo() const noexcept
    {
        return has_theme_ubo_;
    }

    /// True when the variant was built with a Sprint-2 SDF atlas
    /// sampler descriptor (i.e. `texture_count == 1`).
    [[nodiscard]] bool has_sdf_sampler() const noexcept
    {
        return texture_count_ > 0U;
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
    bool          has_theme_ubo_         { false };
};

// ---- Factory ----------------------------------------------------------------

/// Build a UiVariant from the supplied spec.
///
/// Sprint-2 contract:
///   * `texture_count` may be 0 or 1. `1` allocates a combined
///     image+sampler descriptor at `sdf_font_sampler_slot` and uses
///     the Sprint-2 fragment shader (SDF + theme tint).
///   * `theme_palette_ubo_binding` is the legacy Sprint-1 ABI marker
///     and MUST be left at its `-1` default. Any non-negative value
///     is rejected with kInvalidArgument (use `theme_palette_ubo_slot`
///     instead). Sprint-3 will remove the legacy field outright.
///   * `theme_palette_ubo_slot` is always honoured: a UBO descriptor
///     is allocated at that slot whenever the Sprint-2 fragment shader
///     is selected. When `texture_count == 0` and the variant must
///     remain on the Sprint-1 vertex-color-only path, callers should
///     explicitly skip Sprint-2 by leaving `theme_palette_ubo_slot ==
///     sdf_font_sampler_slot` — the factory then takes that as "no
///     descriptors" and falls back to Sprint-1.  (Default values place
///     them at 0 and 1 respectively, so the default spec automatically
///     follows the Sprint-1 path when `texture_count == 0`.)
///   * Slot collision (`theme_palette_ubo_slot ==
///     sdf_font_sampler_slot` while `texture_count == 1`) is rejected
///     with kInvalidArgument.
///
/// Returns `kInvalidArgument` for any out-of-range spec field; returns
/// the underlying error tagged with `kPipelineCreationFailed` for RHI
/// failures.
///
/// Note: this factory does NOT take a `cd::shader::ICompiler*`. It
/// uses an internally-managed glslang compiler when the engine is
/// built with `CD_ENABLE_GLSLANG` (the default). When the engine is
/// built without glslang, the factory returns `kCompilerRequired`.
[[nodiscard]] cd::core::Result<UiVariant>
create_ui_variant(cd::rhi::IDevice& device, const UiVariantSpec& spec);

// ---- Sprint-2 theme UBO payload --------------------------------------------

/// Std140 layout for the Sprint-2 theme palette UBO. Four vec4 in
/// slot order: primary, secondary, surface, on_surface. Each slot
/// is { r, g, b, a } in linear-space [0..1] (matching
/// `cd::ui::theme::ColorToken`). Total size = 64 bytes.
struct UiThemePaletteUbo
{
    float primary    [4] { 1.0F, 1.0F, 1.0F, 1.0F };
    float secondary  [4] { 0.5F, 0.5F, 0.5F, 1.0F };
    float surface    [4] { 0.1F, 0.1F, 0.1F, 1.0F };
    float on_surface [4] { 0.9F, 0.9F, 0.9F, 1.0F };
};
static_assert(sizeof(UiThemePaletteUbo) == 64,
              "UiThemePaletteUbo must be 64 bytes (std140 4 x vec4)");

}  // namespace cd::material
