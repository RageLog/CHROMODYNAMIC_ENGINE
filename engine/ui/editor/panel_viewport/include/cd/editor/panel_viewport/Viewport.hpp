// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_viewport/Viewport.hpp
//
// phase546 — cd::editor::panel::viewport  (panel_viewport library)
//
// Viewport panel: displays the rendered scene colour target as a single
// textured quad that fills the full panel bounds. Provides a DrawBatcher-
// based draw path for the DockSpace shell (apps/editor).
//
// State API:
//   set_scene_texture(TextureHandle) — bind the scene colour render target.
//   current_texture() const          — returns the currently bound handle.
//
// Draw behaviour:
//   * If the bounds are invalid (w <= 0 or h <= 0) the panel returns early
//     after emitting the background quad.
//   * When a valid (non-null) texture handle is set the panel emits a
//     kTextured quad covering the full bounds (UV = 0..1 in both axes).
//   * When the texture handle is null (default-constructed) the panel falls
//     back to a solid background fill so the panel tile is always visible.
//
// The panel stores only the lightweight TextureHandle (a 64-bit opaque ID).
// No raw pointers or owning GPU resources are held — lifetime management
// is entirely the caller's responsibility.
//
// Lifetime contract:
//   Viewport is default-constructible and owns no heap storage.
//   The TextureHandle is value-type; no external ownership is required.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/rhi/Handles.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

namespace cd::editor::panel::viewport
{

class Viewport
{
public:
    // Default-constructible; starts with a null texture handle.
    Viewport() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Bind the scene colour render target. Pass a default-constructed
    /// (null) handle to clear the texture and render a plain background.
    void set_scene_texture(cd::rhi::TextureHandle handle) noexcept;

    /// Returns the currently bound texture handle (null if none was set).
    [[nodiscard]] cd::rhi::TextureHandle current_texture() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// When a valid texture handle is set the panel emits a kTextured quad
    /// covering the full bounds so the RHI submitter can blit the scene
    /// colour target. The `texture_slot` forwarded to the batcher is the
    /// lower 32 bits of the handle's raw value — the RHI submitter uses
    /// the same convention when binding descriptors.
    ///
    /// When no texture is set the panel falls back to a solid background
    /// fill using theme.background (slightly darker than panel surfaces,
    /// matching the intent of the original draw_viewport_stub in apps/editor).
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    cd::rhi::TextureHandle scene_texture_ {};  ///< Currently bound scene target (null = none).
};

}  // namespace cd::editor::panel::viewport
