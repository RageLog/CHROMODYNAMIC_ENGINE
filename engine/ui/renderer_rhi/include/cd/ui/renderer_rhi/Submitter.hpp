// =============================================================================
// CHROMODYNAMIC — cd/ui/renderer_rhi/Submitter.hpp
//
// Phase 1.2b of ADR-20260530-ui-widget-library. Bridges the pure-CPU
// `cd::ui::renderer::DrawBatcher` to a `cd::rhi::IDevice` + command
// buffer. Lifetime model:
//
//   Once at boot:    Submitter::create(device, target_format)
//                    -> uploads + creates vertex/index buffers,
//                       compiles UI VS + FS, links pipeline.
//
//   Per frame:       submitter.upload(batcher);
//                    submitter.record(cmd, viewport_extent);
//
//   At shutdown:     submitter.destroy() (or destruct).
//
// The submitter owns three GPU resources only: one ring vb, one ring ib,
// one pipeline (+ pipeline-layout + descriptor set bound to a single
// font/atlas texture slot). Multiple atlas slots = multiple submitters.
// Phase 2 will introduce a multi-atlas variant when widget catalogs need
// it.
//
// Atlas binding contract: the caller hands a single
// `cd::rhi::TextureViewHandle` (alpha-only, R8 format) at create-time —
// `cd::ui::font::AtlasBitmap.pixels` directly uploads into this slot.
// Glyph variant fragment shader samples `.r` and tints with vertex colour.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/rhi/Descriptors.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/rhi/ICommandBuffer.hpp>
#include <cd/rhi/IDevice.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <cstdint>
#include <memory>

// Phase 608 / M7 W3 — forward-declare cd::material::UiVariant so the
// header keeps the cd::material include behind the .cpp boundary. The
// Route A factory below takes the variant by rvalue reference (sink),
// so callers must `#include <cd/material/UiVariant.hpp>` themselves
// before invoking `create_with_material_ui_variant`. Keeping the
// forward-decl here avoids dragging glslang's transitive include
// surface (cd::material -> cd::shader) into every translation unit
// that just wants to call `create()` / `record()`.
//
// Phase 648 / M10 W3A Sprint-3 — also forward-decl
// `UiThemePaletteUbo` so the Sprint-3 `set_theme_palette` accessor can
// stay header-visible without dragging the cd::material include in.
// Callers that actually invoke `set_theme_palette` must include
// `<cd/material/UiVariant.hpp>` to construct the payload.
namespace cd::material
{
class UiVariant;
struct UiThemePaletteUbo;
}  // namespace cd::material

namespace cd::ui::renderer_rhi
{

/// Configuration handed to `Submitter::create`. The submitter creates
/// its own pipeline keyed to the caller's render target format + a
/// reasonable default blend (premultiplied alpha) + scissor-enabled
/// dynamic state.
struct SubmitterCreateInfo
{
    /// Target color-attachment format the submitter will render INTO.
    /// Must match the render pass the caller will issue `record()` from.
    cd::rhi::Format          color_format { cd::rhi::Format::kRGBA8Unorm };

    /// Optional depth attachment format. Pass `kUndefined` to skip the
    /// depth state entirely (default — UI typically draws after the
    /// scene depth-test is finished).
    cd::rhi::Format          depth_format { cd::rhi::Format::kUndefined };

    /// Maximum number of vertices supported per frame. The submitter
    /// allocates ONE vertex buffer of this size on create. 65535
    /// (uint16 index max) is the sane default for one UI page.
    std::uint32_t            max_vertices { 65535U };

    /// Maximum number of indices supported per frame. Default 6x
    /// max_vertices (one quad = 4 verts + 6 indices).
    std::uint32_t            max_indices  { 65535U * 6U };

    /// Single-texture slot bound for glyph + textured-quad variants.
    /// Solid-fill quads ignore it. Pass an invalid view to render
    /// solid-only (the texture descriptor still gets a valid binding;
    /// any sampling will return undefined values).
    cd::rhi::TextureViewHandle atlas_view {};
    cd::rhi::SamplerHandle     atlas_sampler {};
};

/// Submitter — owning handle for a UI rendering pipeline + ring
/// vertex/index buffers. Move-only. Construct via `create`, hand back
/// via `destroy` (or rely on the dtor).
class Submitter
{
public:
    // Out-of-line (defined in Submitter.cpp where Impl is complete): a header
    // `= default` would force inline ~unique_ptr<Impl> instantiation against the
    // incomplete PIMPL type and fail to compile in default-constructing consumers.
    Submitter() noexcept;
    ~Submitter();

    Submitter(const Submitter&) = delete;
    Submitter& operator=(const Submitter&) = delete;
    Submitter(Submitter&&) noexcept;
    Submitter& operator=(Submitter&&) noexcept;

    /// Factory. Returns the constructed submitter or a `cd::core::ErrorCode`
    /// on RHI allocation failure.
    [[nodiscard]] static cd::core::Result<Submitter>
    create(cd::rhi::IDevice& device, const SubmitterCreateInfo& info);

    /// Phase 554 / M3 W1A -- Route B fallback factory.
    ///
    /// Constructs the submitter with an INLINE-COMPILED GLSL pipeline so
    /// callers get a working `record()` path even when the cd::material UI
    /// variants (Route A) are not yet present. The pipeline:
    ///
    ///   * vertex shader transforms `pos2` by a viewport-size push-constant
    ///     into Vulkan-NDC (Y-down) and passes vertex `color4` through;
    ///   * fragment shader writes the vertex colour straight to the single
    ///     output attachment -- no atlas sampling in v1 (glyphs deferred
    ///     until Route A / cd::material UI variants land);
    ///   * pipeline state: alpha blend ON, depth test OFF, no culling,
    ///     scissor + viewport are dynamic state (set per draw command).
    ///
    /// Vertex layout matches `cd::ui::renderer::Vertex` exactly:
    ///   location 0 : vec2 (pos)
    ///   location 1 : vec2 (uv -- bound, ignored by FS today)
    ///   location 2 : RGBA8 unorm -> vec4 (color)
    ///
    /// Returns the same kinds of errors as `create()` plus shader compile
    /// failures, which arrive in the `ErrorCode::message` field.
    [[nodiscard]] static cd::core::Result<Submitter>
    create_with_inline_shader(cd::rhi::IDevice& device, const SubmitterCreateInfo& info);

    /// Phase 608 / M7 W3 — Route A factory.
    ///
    /// Constructs the submitter using a pre-built `cd::material::UiVariant`
    /// as the pipeline source (instead of compiling inline GLSL in
    /// `create_with_inline_shader`). The submitter:
    ///
    ///   * takes ownership of the supplied `variant` (move-in);
    ///   * binds the variant's pipeline + pushes the viewport-size push
    ///     constant inside `record()`, exactly the same way the Route B
    ///     inline path does;
    ///   * reuses the same ring vb/ib allocation flow as `create()`.
    ///
    /// Contract: the variant MUST have been built with the canonical UI
    /// vertex format + a single push constant range carrying `vec2
    /// inv_viewport` for the vertex stage. The default
    /// `cd::material::UiVariantSpec` satisfies both requirements (see
    /// `cd::material::create_ui_variant`).
    ///
    /// Phase 648 / M10 W3A Sprint-3 — END-TO-END WIRE-UP.
    ///
    /// When the supplied variant carries descriptor bindings (Sprint-2
    /// theme UBO and/or SDF glyph sampler), the factory:
    ///   * allocates a `cd::material::MaterialInstance` from the variant's
    ///     descriptor-set layout and stashes it inside the submitter;
    ///   * if the variant has a theme UBO, allocates a
    ///     `sizeof(UiThemePaletteUbo)` device buffer (kCpuToGpu, kUniform)
    ///     and writes it into the descriptor at the variant's
    ///     `theme_palette_ubo_slot`. The UBO starts initialised to the
    ///     `UiThemePaletteUbo` default constructor (white primary, dim
    ///     surface). Use `set_theme_palette` to swap the payload before /
    ///     between frames.
    ///   * if the variant has an SDF sampler descriptor (texture_count == 1),
    ///     the descriptor is left UNBOUND at create-time; the caller MUST
    ///     call `set_sdf_atlas` with a live `TextureViewHandle` +
    ///     `SamplerHandle` before the first `record()` that depends on
    ///     glyph sampling. A submitter with an unbound SDF descriptor will
    ///     still record cleanly (Vulkan validation may warn, but the call
    ///     sequence is legal); the glyph quad fragments will sample
    ///     garbage values.
    ///
    /// At record-time the submitter:
    ///   * binds the variant's pipeline;
    ///   * pushes the viewport-size push constant (`vec2 inv_viewport`);
    ///   * binds the MaterialInstance descriptor set at set index 0 when
    ///     the variant has descriptors (no-op otherwise).
    ///
    /// The submitter takes ownership of any handles it allocates above;
    /// `destroy()` tears them down in reverse-create order.
    ///
    /// Returns the same kinds of errors as `create()` plus any error
    /// surfaced by the supplied variant (the factory rejects an inert
    /// `UiVariant` with kInvalidArgument before allocating any GPU
    /// resources) and any descriptor-set allocation / buffer-create
    /// failure.
    [[nodiscard]] static cd::core::Result<Submitter>
    create_with_material_ui_variant(cd::rhi::IDevice&          device,
                                    const SubmitterCreateInfo& info,
                                    cd::material::UiVariant&&  variant);

    /// Phase 648 / M10 W3A Sprint-3 — refresh the Sprint-2 theme palette
    /// payload. The submitter must have been built via
    /// `create_with_material_ui_variant` from a variant with a theme UBO
    /// descriptor; on any other path this is a no-op (returns false). The
    /// upload uses the underlying device's `upload_buffer` call so the
    /// next `record()` reads the fresh palette. Returns true on success.
    bool set_theme_palette(const cd::material::UiThemePaletteUbo& payload);

    /// Phase 648 / M10 W3A Sprint-3 — bind a glyph SDF atlas
    /// (combined image+sampler) into the variant's descriptor set. The
    /// submitter must have been built via
    /// `create_with_material_ui_variant` from a variant with an SDF
    /// sampler descriptor (texture_count == 1); on any other path this
    /// is a no-op (returns false). Returns true on success.
    bool set_sdf_atlas(cd::rhi::TextureViewHandle view,
                       cd::rhi::SamplerHandle     sampler);

    /// Free GPU resources. Idempotent. Called automatically on destruction.
    void destroy() noexcept;

    /// Upload the batcher's vertex + index buffers to the GPU ring buffers.
    /// Returns false if the batcher overflows the configured max sizes —
    /// the caller should `begin_frame()` more often or bump max_* on create.
    [[nodiscard]] bool upload(const cd::ui::renderer::DrawBatcher& batcher);

    /// Record draw commands from the last `upload()` into the caller's
    /// command buffer. Must be inside an active render pass. The submitter
    /// sets its own viewport + scissor for each `DrawCommand` based on the
    /// batcher's scissor stack.
    void record(cd::rhi::ICommandBuffer& cmd, cd::rhi::Extent2D viewport_extent) const;

    /// Read-only state.
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] std::uint32_t vertex_count() const noexcept;
    [[nodiscard]] std::uint32_t index_count() const noexcept;
    [[nodiscard]] std::uint32_t command_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace cd::ui::renderer_rhi
