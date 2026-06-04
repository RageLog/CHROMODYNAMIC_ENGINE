// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_material_preview/MaterialPreview.hpp
//
// phase678 — cd::editor::panel::material_preview  (panel_material_preview library)
//
// Sprint-1 material preview panel: provides a DrawBatcher-based live preview of
// an AuthoredMaterial for the DockSpace shell (apps/editor).
//
// Renders (2D projection, no GPU pipeline required):
//
//   * Panel background fill.
//   * Separator bar under the title area (accent colour).
//   * 2D-projected sphere: a circular gradient quad tinted by base_color.
//     Implemented as a single filled quad whose colour matches the material's
//     base_color so the artist sees the dominant tint at a glance.
//   * Metallic bar (accent_success tint — full bar = fully metallic).
//   * Roughness bar (accent_warning tint — full bar = maximally rough).
//   * Alpha cutoff row (shown only when alpha_mode == kMask).
//   * Texture path list: albedo / normal / MR paths listed as pill rows.
//     Empty paths render as greyed-out "—" placeholders.
//
// Sprint-2 plan: replace the sphere quad with a real PBR render via the
// cd::material UI variant pipeline once that subsystem is wired end-to-end.
//
// State API:
//   set_material(const AuthoredMaterial*)  — bind/update the preview target.
//                                            Pass nullptr to detach.
//   material() const                       — returns the bound pointer (may be
//                                            nullptr).
//
// Lifetime contract:
//   MaterialPreview is default-constructible and holds a non-owning raw pointer.
//   The caller must ensure the AuthoredMaterial outlives the panel, or call
//   set_material(nullptr) before the material is destroyed.
//
// MOMENT: A material artist hand-edits material.json, hot-reload picks up the
// file, calls set_material(&new_mat) and triggers a draw — the preview panel
// immediately repaints the new base_color tint, metallic bar, and roughness bar.
// The material iteration loop closes without a full engine rebuild.
// =============================================================================
#pragma once

#include <cd/asset/material_authoring/MaterialAuthoring.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

namespace cd::editor::panel::material_preview
{

// ---------------------------------------------------------------------------
// MaterialPreview
// ---------------------------------------------------------------------------
class MaterialPreview
{
public:
    // Default-constructible; starts with no bound material (nullptr).
    MaterialPreview() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Bind to an AuthoredMaterial. The panel holds a non-owning pointer;
    /// pass nullptr to detach. The next draw() call will reflect the new
    /// material state.
    void set_material(const cd::asset::material_authoring::AuthoredMaterial* mat) noexcept;

    /// Returns the currently bound AuthoredMaterial pointer (nullptr if none).
    [[nodiscard]] const cd::asset::material_authoring::AuthoredMaterial*
    material() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (top-to-bottom within bounds):
    ///   1. Panel background quad.
    ///   2. Accent separator bar.
    ///   3. Sphere swatch quad tinted with the material's base_color.
    ///   4. Metallic bar row (accent_success tint).
    ///   5. Roughness bar row (accent_warning tint).
    ///   6. Alpha cutoff bar row (error tint) — only when alpha_mode == kMask.
    ///   7. Texture path pills: albedo / normal / MR.
    ///
    /// When no material is bound (material() == nullptr), only the background
    /// and separator are drawn.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    const cd::asset::material_authoring::AuthoredMaterial* mat_ { nullptr };
};

}  // namespace cd::editor::panel::material_preview
