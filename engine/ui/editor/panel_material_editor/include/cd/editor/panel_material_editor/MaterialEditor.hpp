// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_material_editor/MaterialEditor.hpp
//
// phase556-557-558 — cd::editor::panel::material_editor  (panel_material_editor library)
//
// Material parameter editor panel: provides a DrawBatcher-based draw path for
// the DockSpace shell (apps/editor). Renders:
//
//   * A coloured panel background.
//   * A separator bar under the title area.
//   * Three labelled slider rows for base-color (RGB tint), metallic, and
//     roughness — the values are visualised as horizontal fill bars.
//   * A live preview rectangle filled with the current base-color tint so the
//     artist can see the colour at a glance.
//
// State API:
//   set_material_id(MaterialId)         — bind to a material slot (0 = none).
//   material_id() const                 — returns the currently bound id.
//   set_base_color(float, float, float) — override the RGB base-colour tint.
//   set_metallic(float)                 — override the metallic value [0..1].
//   set_roughness(float)                — override the roughness value [0..1].
//   base_color_r/g/b() const            — read back current tint channels.
//   metallic() const                    — read back current metallic value.
//   roughness() const                   — read back current roughness value.
//
// MaterialId is a lightweight opaque 32-bit identifier. The panel stores it
// as an editorial reference; binding / resolving to a GPU material is the
// caller's responsibility.
//
// Lifetime contract:
//   MaterialEditor is default-constructible and owns no heap storage.
//   No external pointer ownership is required.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstdint>

namespace cd::editor::panel::material_editor
{

// ---------------------------------------------------------------------------
// MaterialId — lightweight opaque handle for a material slot.
// ---------------------------------------------------------------------------
using MaterialId = std::uint32_t;

/// Sentinel value meaning "no material bound".
inline constexpr MaterialId kInvalidMaterialId = 0U;

// ---------------------------------------------------------------------------
// MaterialEditor
// ---------------------------------------------------------------------------
class MaterialEditor
{
public:
    // Default-constructible; starts with no bound material and default PBR
    // parameters (white diffuse, non-metallic, half-rough).
    MaterialEditor() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Bind to a material slot. Pass kInvalidMaterialId (or 0) to clear the
    /// binding. The panel stores the id but performs no GPU operations.
    void set_material_id(MaterialId id) noexcept;

    /// Returns the currently bound MaterialId (kInvalidMaterialId if none).
    [[nodiscard]] MaterialId material_id() const noexcept;

    /// Override the RGB base-colour tint. Each channel is clamped to [0..1].
    void set_base_color(float r, float g, float b) noexcept;

    /// Read back the red channel of the current base-colour tint.
    [[nodiscard]] float base_color_r() const noexcept;
    /// Read back the green channel of the current base-colour tint.
    [[nodiscard]] float base_color_g() const noexcept;
    /// Read back the blue channel of the current base-colour tint.
    [[nodiscard]] float base_color_b() const noexcept;

    /// Set the metallic value. Clamped to [0..1].
    void set_metallic(float value) noexcept;
    /// Returns the current metallic value.
    [[nodiscard]] float metallic() const noexcept;

    /// Set the roughness value. Clamped to [0..1].
    void set_roughness(float value) noexcept;
    /// Returns the current roughness value.
    [[nodiscard]] float roughness() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders the material-editor panel background, a separator, three labelled
    /// parameter rows (base color / metallic / roughness), and a live preview
    /// rectangle showing the current base-colour tint.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    MaterialId material_id_  { kInvalidMaterialId };  ///< Bound material slot.
    float      base_color_r_ { 1.0F };                ///< Base-colour red   [0..1].
    float      base_color_g_ { 1.0F };                ///< Base-colour green [0..1].
    float      base_color_b_ { 1.0F };                ///< Base-colour blue  [0..1].
    float      metallic_     { 0.0F };                ///< Metallic factor   [0..1].
    float      roughness_    { 0.5F };                ///< Roughness factor  [0..1].
};

}  // namespace cd::editor::panel::material_editor
