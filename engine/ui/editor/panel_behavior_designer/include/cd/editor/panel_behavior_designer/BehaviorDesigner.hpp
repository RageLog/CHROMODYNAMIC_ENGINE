// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_behavior_designer/BehaviorDesigner.hpp
//
// phase556-557-558 — cd::editor::panel::behavior_designer  (panel_behavior_designer library)
//
// Behavior tree graph view panel.
// Provides a DrawBatcher-based draw path for the DockSpace shell (apps/editor).
// Sprint-1 renders:
//
//   * A coloured panel background.
//   * A separator bar under the title area.
//   * A placeholder grid of horizontal + vertical lines (represented as thin
//     quads) within the panned view.
//   * Three demo nodes connected by edge lines:
//       - Root (Selector)  — top centre.
//       - Child A (Sequence) — bottom-left.
//       - Child B (Action)   — bottom-right.
//   * The currently selected node is highlighted with an accent border.
//
// Sprint-2 will add full BT graph data (arbitrary node/edge lists read from
// the cd::ai or cd::behavior subsystem) and interactive pan/select via input
// events.
//
// State API:
//   set_tree_root(BehaviorNodeId)     — bind the panel to a BT root node.
//   tree_root() const                 — returns the currently bound root id.
//   set_selected(BehaviorNodeId)      — select a node (kInvalidNodeId = none).
//   selected() const                  — returns the selected node id.
//   set_pan_offset(float x, float y)  — set view pan in pixels.
//   pan_x() const                     — horizontal pan in pixels.
//   pan_y() const                     — vertical pan in pixels.
//
// BehaviorNodeId is a lightweight opaque 32-bit identifier following the same
// idiom as ClipId in panel_animator and MaterialId in panel_material_editor.
// Resolving a BehaviorNodeId to actual BT data is the caller's responsibility.
//
// Lifetime contract:
//   BehaviorDesigner is default-constructible and owns no heap storage.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstdint>

namespace cd::editor::panel::behavior_designer
{

// ---------------------------------------------------------------------------
// BehaviorNodeId — lightweight opaque handle for a behavior tree node.
// ---------------------------------------------------------------------------
using BehaviorNodeId = std::uint32_t;

/// Sentinel value meaning "no node bound / selected".
inline constexpr BehaviorNodeId kInvalidNodeId = 0U;

// ---------------------------------------------------------------------------
// BehaviorDesigner
// ---------------------------------------------------------------------------
class BehaviorDesigner
{
public:
    // Default-constructible; starts with no root, no selection, zero pan.
    BehaviorDesigner() noexcept = default;

    // ---- Tree root binding API ----------------------------------------------

    /// Bind the panel to a behavior tree root node.
    /// Pass kInvalidNodeId to detach.
    void set_tree_root(BehaviorNodeId id) noexcept;

    /// Returns the currently bound root node id (kInvalidNodeId if none).
    [[nodiscard]] BehaviorNodeId tree_root() const noexcept;

    // ---- Node selection API -------------------------------------------------

    /// Select a node by id. Pass kInvalidNodeId (or 0) to clear the selection.
    void set_selected(BehaviorNodeId id) noexcept;

    /// Returns the currently selected node id.
    [[nodiscard]] BehaviorNodeId selected() const noexcept;

    // ---- View pan API -------------------------------------------------------

    /// Set the view pan offset in pixels (drag-to-pan).
    void set_pan_offset(float x, float y) noexcept;

    /// Returns the horizontal pan offset in pixels.
    [[nodiscard]] float pan_x() const noexcept;

    /// Returns the vertical pan offset in pixels.
    [[nodiscard]] float pan_y() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Sprint-1: renders a placeholder grid + 3 demo nodes + 2 connecting edges.
    /// The selected node (if any) receives an accent-coloured border quad.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    BehaviorNodeId root_id_     { kInvalidNodeId }; ///< Bound BT root.
    BehaviorNodeId selected_id_ { kInvalidNodeId }; ///< Selected node.
    float          pan_x_       { 0.0F };            ///< Horizontal pan (px).
    float          pan_y_       { 0.0F };            ///< Vertical pan (px).
};

}  // namespace cd::editor::panel::behavior_designer
