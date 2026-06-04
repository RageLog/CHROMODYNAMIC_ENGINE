// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_ik_chain_editor/IkChainEditor.hpp
//
// phase710 — cd::editor::panel::ik_chain_editor  (panel_ik_chain_editor library)
// phase743 — 3D VP projection + draggable target manipulation
//
// IK Chain Editor panel: provides a DrawBatcher-based draw path for the
// DockSpace shell (apps/editor). Renders a projection of a
// cd::animation::ik::IkChain and the result from the most recent CCD solve.
//
// Sprint-1 renders (2D side-view when no VP matrix set):
//
//   * Panel background quad.
//   * Accent separator bar under the title area.
//   * Lines connecting consecutive joint world positions (chain order).
//   * Per-joint coloured circle:
//       root joint (index 0) = blue  (kColorRoot)
//       middle joints        = white (kColorMid)
//       end-effector joint   = accent_success green (kColorEnd)
//   * Selected joint ring: accent-coloured thin ring around the selected joint.
//   * Target position (IkChain::end_effector_target) = small X marker in
//       accent_warning orange.  When a mutable chain is bound via
//       set_chain_mutable(), the X marker is draggable.
//   * Converged indicator (bottom-left, when result is bound):
//       green check quad  if result.converged == true
//       red   cross quad  if result.converged == false
//
// Sprint-2 (phase 743) — 3D viewport projection + target drag:
//   * set_camera(view_proj) binds a 4×4 column-major VP matrix.
//     When bound, world-space joint positions are projected to screen pixels
//     via the full VP transform + perspective divide + NDC→panel mapping.
//   * set_chain_mutable(IkChain*) binds a mutable chain for interactive editing.
//   * begin_target_drag / update_target_drag / end_target_drag API:
//     - begin_target_drag(x, y, bounds) initiates a drag when (x,y) is within
//       kDragRadius pixels of the projected target position.
//     - update_target_drag(x, y, bounds) back-projects the screen delta to
//       world-space and writes the new position into IkChain::end_effector_target,
//       then re-runs CcdSolver::solve and stores the result.
//     - end_target_drag() commits the drag and clears the drag state.
//     - is_dragging_target() const reports whether a drag is in progress.
//
// State API:
//   set_chain(const IkChain*)           — bind read-only chain (nullptr = detach).
//   set_chain_mutable(IkChain*)         — bind mutable chain for drag editing.
//   set_last_result(const IkResult*)    — bind last solve result (nullptr = clear).
//   set_camera(view_proj)               — bind 4×4 VP matrix for 3D projection.
//   clear_camera()                      — revert to 2D side-view projection.
//   selected_joint() const              — returns selected joint index or nullopt.
//   simulate_click(x, y, bounds)        — hit-test joints; updates selection.
//   begin_target_drag(x, y, bounds)     — start drag on target X marker.
//   update_target_drag(x, y, bounds)    — move target; re-solve IK chain.
//   end_target_drag()                   — finish drag.
//   is_dragging_target() const          — true while a drag is in progress.
//
// Lifetime contract:
//   IkChainEditor is default-constructible and holds non-owning raw pointers
//   to IkChain and IkResult. Callers must ensure the pointed-to objects outlive
//   the panel. Calling set_chain(nullptr) or set_last_result(nullptr) detaches
//   safely.
//
// MOMENT: An animator drags the foot target through 3D space, the IK chain
// solves in real-time, the leg follows realistically.
// =============================================================================
#pragma once

#include <cd/animation/ik/Ik.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace cd::editor::panel::ik_chain_editor
{

// ---------------------------------------------------------------------------
// IkChainEditor
// ---------------------------------------------------------------------------
class IkChainEditor
{
public:
    // Default-constructible; starts detached (no chain, no result, no selection).
    IkChainEditor() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Bind the IK chain to visualize (read-only).  nullptr detaches.
    /// The pointer is stored non-owning; the caller must ensure it outlives the
    /// panel, or call set_chain(nullptr) before the object is destroyed.
    /// NOTE: set_chain_mutable() takes precedence for drawing; call set_chain(nullptr)
    /// AND set_chain_mutable(nullptr) to fully detach.
    void set_chain(const cd::animation::ik::IkChain* chain) noexcept;

    /// Bind a mutable IK chain for interactive target-drag editing.
    /// When non-null this chain is used for both drawing and drag updates.
    /// nullptr clears mutable binding (falls back to read-only chain_).
    void set_chain_mutable(cd::animation::ik::IkChain* chain) noexcept;

    /// Bind the most recent solve result.  nullptr clears the result overlay
    /// (convergence indicator + solved state).
    void set_last_result(const cd::animation::ik::IkResult* result) noexcept;

    /// Bind a 4×4 column-major view-projection matrix for 3D world-to-screen
    /// projection.  Replaces the 2D side-view projection.
    /// Column-major layout: m[col * 4 + row], same as OpenGL/GLSL mat4.
    void set_camera(const std::array<float, 16>& view_proj) noexcept;

    /// Revert to the default 2D side-view projection (clear VP matrix).
    void clear_camera() noexcept;

    /// Returns the index of the currently selected joint, or std::nullopt when
    /// nothing is selected or no chain is bound.
    [[nodiscard]] std::optional<std::size_t> selected_joint() const noexcept;

    /// Perform a hit-test at (x, y) in the same coordinate space as `bounds`.
    /// Selects the nearest joint within kJointRadius pixels.
    /// Does nothing when no chain is bound.
    void simulate_click(float x, float y,
                        const cd::ui::widgets::Rect& bounds) noexcept;

    // ---- Target drag API ----------------------------------------------------

    /// Begin a drag if (x, y) is within kDragRadius pixels of the projected
    /// target marker.  Requires set_chain_mutable() to be bound.
    /// Returns true if the drag was initiated.
    bool begin_target_drag(float x, float y,
                           const cd::ui::widgets::Rect& bounds) noexcept;

    /// Update the drag: back-projects the new screen position to world space,
    /// writes into IkChain::end_effector_target, re-runs CcdSolver::solve and
    /// stores the result (accessible via set_last_result → the internal copy).
    /// No-op when not dragging or no mutable chain is bound.
    void update_target_drag(float x, float y,
                            const cd::ui::widgets::Rect& bounds) noexcept;

    /// Commit and finish the current drag.  No-op when not dragging.
    void end_target_drag() noexcept;

    /// True while a target drag is in progress.
    [[nodiscard]] bool is_dragging_target() const noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (in order):
    ///   1. Panel background quad.
    ///   2. Accent separator bar.
    ///   3. Joint chain lines (foreground, accent-dim colour).
    ///   4. Joint circles (root=blue, mid=white, end=success-green).
    ///   5. Selected joint ring (accent outline).
    ///   6. Target X marker (warning-orange; brighter when dragging).
    ///   7. Convergence indicator quad (bottom-left corner).
    ///
    /// No-op when the chain pointer is null or bounds are invalid.
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&   batcher,
              const cd::ui::widgets::Theme&    theme,
              const cd::ui::widgets::Rect&     bounds) const;

private:
    // ---- Bound data (non-owning) --------------------------------------------

    const cd::animation::ik::IkChain*  chain_         { nullptr };
    cd::animation::ik::IkChain*        chain_mutable_ { nullptr };
    const cd::animation::ik::IkResult* result_        { nullptr };

    // ---- Camera / VP matrix -------------------------------------------------

    /// Column-major 4×4 view-projection matrix.
    std::array<float, 16> view_proj_ {};
    bool                  has_camera_ { false };

    // ---- Selection ----------------------------------------------------------

    std::optional<std::size_t> selected_ {};

    // ---- Drag state ---------------------------------------------------------

    bool  is_dragging_  { false };
    float drag_start_x_ { 0.0F };  ///< Panel-pixel X where drag began.
    float drag_start_y_ { 0.0F };  ///< Panel-pixel Y where drag began.

    /// Live solve result produced during drag (stored so draw() can show it).
    cd::animation::ik::IkResult drag_result_ {};

    /// CCD solver instance used for real-time re-solve during drag.
    cd::animation::ik::CcdSolver solver_ {};

    // ---- Layout / geometry constants ----------------------------------------

    static constexpr float kPad        = 6.0F;   ///< Horizontal/vertical padding.
    static constexpr float kBarH       = 4.0F;   ///< Title separator bar height.
    static constexpr float kHeaderH    = kPad + kBarH + kPad;

    static constexpr float kJointR     = 6.0F;   ///< Joint circle visual radius (px).
    static constexpr float kJointRadius= 10.0F;  ///< Hit-test radius for simulate_click.
    static constexpr float kDragRadius = 12.0F;  ///< Hit-test radius for target drag.
    static constexpr float kLineH      = 2.0F;   ///< Bone-line thickness.
    static constexpr float kTargetHalf = 5.0F;   ///< Half-size of the target X marker.
    static constexpr float kIndicatorW = 14.0F;  ///< Convergence indicator quad size.

    // ---- Active chain accessor ----------------------------------------------

    /// Returns the active (const) chain: mutable chain if bound, else read-only.
    [[nodiscard]] const cd::animation::ik::IkChain* active_chain() const noexcept;

    // ---- Projection helpers -------------------------------------------------

    /// Map a chain world-space X coordinate to a panel pixel X.
    /// Uses the XY extent of the chain to fill the panel content area.
    [[nodiscard]] static float project_x(float world_x,
                                         float world_min_x, float world_range_x,
                                         float panel_x,     float panel_w) noexcept;

    /// Map a chain world-space Y coordinate to a panel pixel Y.
    /// Y is flipped so positive world-Y maps upward on screen.
    [[nodiscard]] static float project_y(float world_y,
                                         float world_min_y, float world_range_y,
                                         float panel_y,     float panel_h) noexcept;

    /// Project a 3D world-space point through the stored VP matrix to
    /// panel-pixel coordinates.  Returns the (x, y) screen position.
    /// The panel rect is used to map NDC [-1,+1] → pixel rect.
    [[nodiscard]] std::array<float, 2> project_3d(
        const std::array<float, 3>& world_pos,
        const cd::ui::widgets::Rect& bounds) const noexcept;

    /// Compute panel-pixel positions for all joints using 3D VP projection
    /// (when has_camera_) or 2D side-view projection (otherwise).
    [[nodiscard]] std::vector<std::array<float, 2>> compute_screen_positions(
        const cd::animation::ik::IkChain& chain,
        const cd::ui::widgets::Rect& bounds) const noexcept;

    /// Compute projected screen position of the end-effector target.
    [[nodiscard]] std::array<float, 2> compute_target_screen_pos(
        const cd::animation::ik::IkChain& chain,
        const cd::ui::widgets::Rect& bounds) const noexcept;

    // ---- Chain geometry helpers ---------------------------------------------

    /// Compute flat world-space XY positions from the chain's local_position
    /// fields (treating local_position[0] as X and local_position[1] as Y for
    /// 2D projection; the panel is a XY side-view).
    ///
    /// positions[0] = chain.joints[0].local_position (root world position).
    /// positions[i] = positions[i-1] shifted by this joint's local_position.
    [[nodiscard]] static std::vector<std::array<float, 2>> compute_joint_positions_2d(
        const cd::animation::ik::IkChain& chain) noexcept;

    /// Compute world-space 3D positions for all joints by accumulating
    /// local_position offsets from the root.
    [[nodiscard]] static std::vector<std::array<float, 3>> compute_joint_positions_3d(
        const cd::animation::ik::IkChain& chain) noexcept;

    // ---- Back-projection helper ---------------------------------------------

    /// Back-project a panel-pixel (x, y) to a world-space XZ plane at world
    /// Y = world_y_plane.  Used to convert a drag position to a new target.
    /// Returns the world position or std::nullopt if the ray is parallel to
    /// the plane (degenerate VP).
    [[nodiscard]] std::optional<std::array<float, 3>> unproject_to_plane(
        float x, float y,
        const cd::ui::widgets::Rect& bounds,
        float world_y_plane) const noexcept;
};

}  // namespace cd::editor::panel::ik_chain_editor
