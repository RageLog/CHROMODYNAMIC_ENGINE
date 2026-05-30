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
    Submitter() = default;
    ~Submitter();

    Submitter(const Submitter&) = delete;
    Submitter& operator=(const Submitter&) = delete;
    Submitter(Submitter&&) noexcept;
    Submitter& operator=(Submitter&&) noexcept;

    /// Factory. Returns the constructed submitter or a `cd::core::ErrorCode`
    /// on RHI allocation failure.
    [[nodiscard]] static cd::core::Result<Submitter>
    create(cd::rhi::IDevice& device, const SubmitterCreateInfo& info);

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
