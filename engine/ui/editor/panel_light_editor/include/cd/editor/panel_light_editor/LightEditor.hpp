// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_light_editor/LightEditor.hpp
//
// phase664 — cd::editor::panel::light_editor  (panel_light_editor library)
//
// Light Editor panel: provides a DrawBatcher-based draw path for the DockSpace
// shell (apps/editor). Renders:
//
//   * A scrollable list of point lights — one row per light showing:
//       position vec3 + radius + intensity + colour swatch.
//     The selected row is highlighted in the panel's accent colour.
//
//   * A cluster grid stats section below the list showing:
//       X/Y/Z tile counts + near/far plane distances.
//
// State API:
//   set_lights(span<const PointLight>)   — update the light list view (copies).
//   set_grid(const ClusterGrid&)         — update cluster grid config (copies).
//   selected_light() const               — returns selected index, or nullopt.
//   simulate_click(x, y, bounds)         — hit-test against row geometry.
//
// All fields are read-only this Sprint; editing is Sprint-2.
//
// Lifetime contract:
//   LightEditor is default-constructible and owns no external resources.
//   set_lights / set_grid copy the data — no dangling-reference hazard.
//
// MOMENT: A lighting designer drops the panel into the editor, sees all 200
// point lights in their scene as a list, clicks one to highlight it in the
// viewport.
// =============================================================================
#pragma once

#include <cd/render/lighting_clusters/LightingClusters.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace cd::editor::panel::light_editor
{

// ---------------------------------------------------------------------------
// LightEditor
// ---------------------------------------------------------------------------
class LightEditor
{
public:
    // Default-constructible; starts with empty light list and default grid.
    LightEditor() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Replace the light list displayed by the panel.
    /// The span is consumed immediately; the caller may release it after return.
    void set_lights(std::span<const cd::render::lighting_clusters::PointLight> lights);

    /// Replace the cluster grid configuration shown in the stats section.
    void set_grid(const cd::render::lighting_clusters::ClusterGrid& grid) noexcept;

    /// Returns the index of the currently selected light, or std::nullopt if
    /// nothing is selected.
    [[nodiscard]] std::optional<std::size_t> selected_light() const noexcept;

    /// Perform a hit-test against the panel row geometry at (x, y) in the same
    /// coordinate space as `bounds`. Updates the selection on a hit.
    void simulate_click(float x, float y, const cd::ui::widgets::Rect& bounds) noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders:
    ///   1. Panel background quad.
    ///   2. Accent separator bar under the title area.
    ///   3. One row per light (colour swatch + three position strips + radius
    ///      + intensity strip). The selected row gets an accent highlight.
    ///   4. Cluster grid stats section (three cell-count strips + near/far).
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    std::vector<cd::render::lighting_clusters::PointLight> lights_;
    cd::render::lighting_clusters::ClusterGrid             grid_ {};
    std::optional<std::size_t>                             selected_ {};

    // ---------------------------------------------------------------------------
    // Internal geometry constants (shared between draw() and simulate_click()).
    // ---------------------------------------------------------------------------
    static constexpr float kRowH    = 22.0F;   ///< Height of one light row.
    static constexpr float kPad     = 6.0F;    ///< Horizontal/vertical padding.
    static constexpr float kBarH    = 4.0F;    ///< Title separator bar height.
    static constexpr float kHeaderH = kPad + kBarH + kPad; ///< Space above first row.

    /// Y coordinate of row `i` relative to bounds.y.
    [[nodiscard]] static float row_top(std::size_t i) noexcept
    {
        return kHeaderH + static_cast<float>(i) * (kRowH + 2.0F);
    }
};

}  // namespace cd::editor::panel::light_editor
