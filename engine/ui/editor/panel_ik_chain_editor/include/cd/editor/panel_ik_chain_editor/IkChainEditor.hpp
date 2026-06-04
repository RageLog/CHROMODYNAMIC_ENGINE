// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_ik_chain_editor/IkChainEditor.hpp
//
// phase710 — cd::editor::panel::ik_chain_editor  (panel_ik_chain_editor library)
//
// IK Chain Editor panel: provides a DrawBatcher-based draw path for the
// DockSpace shell (apps/editor). Renders a 2D side-view projection of a
// cd::animation::ik::IkChain and the result from the most recent CCD solve.
//
// Sprint-1 renders (XZ side-view: joint.local_position[0] = X, [1] = Y):
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
//       accent_warning orange.
//   * Converged indicator (bottom-left, when result is bound):
//       green check quad  if result.converged == true
//       red   cross quad  if result.converged == false
//
// State API:
//   set_chain(const IkChain*)           — bind chain (nullptr = detach).
//   set_last_result(const IkResult*)    — bind last solve result (nullptr = clear).
//   selected_joint() const              — returns selected joint index or nullopt.
//   simulate_click(x, y, bounds)        — hit-test joints; updates selection.
//
// Lifetime contract:
//   IkChainEditor is default-constructible and holds non-owning raw pointers
//   to IkChain and IkResult. Callers must ensure the pointed-to objects outlive
//   the panel. Calling set_chain(nullptr) or set_last_result(nullptr) detaches
//   safely.
//
// MOMENT: An animator drops a 5-joint leg chain + ground-target into the panel,
// sees the IK solve converge to plant the foot — visual debugging without
// launching the game.
// =============================================================================
#pragma once

#include <cd/animation/ik/Ik.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <optional>

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

    /// Bind the IK chain to visualize.  nullptr detaches.
    /// The pointer is stored non-owning; the caller must ensure it outlives the
    /// panel, or call set_chain(nullptr) before the object is destroyed.
    void set_chain(const cd::animation::ik::IkChain* chain) noexcept;

    /// Bind the most recent solve result.  nullptr clears the result overlay
    /// (convergence indicator + solved state).
    void set_last_result(const cd::animation::ik::IkResult* result) noexcept;

    /// Returns the index of the currently selected joint, or std::nullopt when
    /// nothing is selected or no chain is bound.
    [[nodiscard]] std::optional<std::size_t> selected_joint() const noexcept;

    /// Perform a hit-test at (x, y) in the same coordinate space as `bounds`.
    /// Selects the nearest joint within kJointRadius pixels.
    /// Does nothing when no chain is bound.
    void simulate_click(float x, float y,
                        const cd::ui::widgets::Rect& bounds) noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (in order):
    ///   1. Panel background quad.
    ///   2. Accent separator bar.
    ///   3. Joint chain lines (foreground, accent-dim colour).
    ///   4. Joint circles (root=blue, mid=white, end=success-green).
    ///   5. Selected joint ring (accent outline).
    ///   6. Target X marker (warning-orange).
    ///   7. Convergence indicator quad (bottom-left corner).
    ///
    /// No-op when the chain pointer is null or bounds are invalid.
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&   batcher,
              const cd::ui::widgets::Theme&    theme,
              const cd::ui::widgets::Rect&     bounds) const;

private:
    // ---- Bound data (non-owning) --------------------------------------------

    const cd::animation::ik::IkChain*  chain_  { nullptr };
    const cd::animation::ik::IkResult* result_ { nullptr };

    // ---- Selection ----------------------------------------------------------

    std::optional<std::size_t> selected_ {};

    // ---- Layout / geometry constants ----------------------------------------

    static constexpr float kPad        = 6.0F;   ///< Horizontal/vertical padding.
    static constexpr float kBarH       = 4.0F;   ///< Title separator bar height.
    static constexpr float kHeaderH    = kPad + kBarH + kPad;

    static constexpr float kJointR     = 6.0F;   ///< Joint circle visual radius (px).
    static constexpr float kJointRadius= 10.0F;  ///< Hit-test radius for simulate_click.
    static constexpr float kLineH      = 2.0F;   ///< Bone-line thickness.
    static constexpr float kTargetHalf = 5.0F;   ///< Half-size of the target X marker.
    static constexpr float kIndicatorW = 14.0F;  ///< Convergence indicator quad size.

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

    // ---- Chain geometry helpers ---------------------------------------------

    /// Compute flat world-space XY positions from the chain's local_position
    /// fields (treating local_position[0] as X and local_position[1] as Y for
    /// 2D projection; the panel is a XY side-view).
    ///
    /// positions[0] = chain.joints[0].local_position (root world position).
    /// positions[i] = positions[i-1] shifted by this joint's local_position.
    [[nodiscard]] static std::vector<std::array<float, 2>> compute_joint_positions_2d(
        const cd::animation::ik::IkChain& chain) noexcept;
};

}  // namespace cd::editor::panel::ik_chain_editor
