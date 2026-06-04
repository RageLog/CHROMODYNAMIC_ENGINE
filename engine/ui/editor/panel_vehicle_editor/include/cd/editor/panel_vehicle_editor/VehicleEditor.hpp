// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_vehicle_editor/VehicleEditor.hpp
//
// phase676 — cd::editor::panel::vehicle_editor  (panel_vehicle_editor library)
//
// Vehicle Editor panel: provides a DrawBatcher-based draw path for the
// DockSpace shell (apps/editor). Renders three collapsible sections:
//
//   Section A — Engine
//     * Max torque (N·m) strip
//     * 6 gear-ratio strips (indexed 1st–6th)
//     * Idle RPM strip  (range 0..2000)
//     * Max RPM strip   (range 0..10000)
//
//   Section B — Wheels  (4 rows, one per corner: FL / FR / RL / RR)
//     Each row (collapsed by default) shows:
//       radius / mass / suspension stiffness
//
//   Section C — Chassis
//     * Mass strip (kg, range 0..5000)
//     * Dimensions strip trio  (length / width / height, each 0..10 m)
//
//   Live-state readout (read-only, only shown when set_state() was called):
//     * speed_kph bar   (range -250..+250 km/h)
//     * rpm bar         (range 0..max_rpm)
//     * current gear label strip
//     * throttle bar    [0..1], green
//     * brake bar       [0..1], red
//     * steer bar       [-1..1], blue
//
// State API:
//   set_config(const VehicleConfig*)  — bind authored parameters (non-owning).
//   set_state (const VehicleState*)   — bind live simulation output (non-owning).
//   simulate_click_apply(bounds)      — placeholder: registers an "apply" event
//                                        within the panel bounds (Sprint-2 will
//                                        push edits back into a VehicleConfig
//                                        callback; here we just toggle a flag).
//   apply_pending() const             — returns true when simulate_click_apply
//                                        has been invoked and not yet consumed.
//   consume_apply()                   — clears the pending-apply flag.
//
// Lifetime contract:
//   VehicleEditor is default-constructible and owns no external resources.
//   Pointers passed to set_config / set_state must remain valid for the
//   lifetime of any subsequent draw() call.
//
// MOMENT: A racing dev tweaks gear ratios via simulate_click_apply, then sees
// the live speed/RPM bars reflect the new acceleration curve on the very next
// frame — without a recompile.
// =============================================================================
#pragma once

#include <cd/physics/vehicle/Vehicle.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

namespace cd::editor::panel::vehicle_editor
{

// ---------------------------------------------------------------------------
// VehicleEditor
// ---------------------------------------------------------------------------
class VehicleEditor
{
public:
    // Default-constructible; starts with no bound config/state.
    VehicleEditor() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Bind authored vehicle parameters. Pass nullptr to clear.
    /// The pointer is non-owning; caller owns the VehicleConfig lifetime.
    void set_config(const cd::physics::vehicle::VehicleConfig* cfg) noexcept;

    /// Bind live simulation output. Pass nullptr to clear.
    /// The pointer is non-owning; caller owns the VehicleState lifetime.
    void set_state(const cd::physics::vehicle::VehicleState* state) noexcept;

    /// Simulate an "Apply" click within the given panel bounds.
    /// Sets the pending-apply flag so the caller can react on the next tick
    /// (Sprint-2 will expose an on_apply callback for direct config mutation).
    void simulate_click_apply(const cd::ui::widgets::Rect& bounds) noexcept;

    /// Returns true if simulate_click_apply() was called and not yet consumed.
    [[nodiscard]] bool apply_pending() const noexcept;

    /// Clears the pending-apply flag.
    void consume_apply() noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Layout (top-to-bottom):
    ///   1. Panel background quad.
    ///   2. Accent separator bar.
    ///   3. Section A — Engine (torque + 6 gear ratios + idle/max RPM).
    ///   4. Section B — Wheels (4 collapsed rows: radius / mass / stiffness).
    ///   5. Section C — Chassis (mass + dimensions trio).
    ///   6. Live state readout (speed / rpm / gear / throttle / brake / steer).
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    const cd::physics::vehicle::VehicleConfig* config_ { nullptr };
    const cd::physics::vehicle::VehicleState*  state_  { nullptr };
    bool apply_pending_ { false };

    // ---- Internal geometry constants ----------------------------------------
    static constexpr float kPad      = 6.0F;   ///< Horizontal/vertical padding.
    static constexpr float kBarH     = 4.0F;   ///< Title separator bar height.
    static constexpr float kRowH     = 18.0F;  ///< Height of one data row.
    static constexpr float kRowGap   = 2.0F;   ///< Gap between rows.
    static constexpr float kHeaderH  = kPad + kBarH + kPad;  ///< Space above first section.
    static constexpr float kSectionGap = 8.0F; ///< Gap between sections.

    // ---- Private draw helpers -----------------------------------------------

    /// Draw a background + partial-fill strip.
    static void draw_strip(cd::ui::renderer::DrawBatcher& batcher,
                           const cd::ui::widgets::Theme&  theme,
                           float x, float y, float w, float h,
                           float norm_fill,
                           cd::ui::renderer::Color fill_color) noexcept;

    /// Draw a section-header separator (thin accent line + label placeholder).
    static void draw_section_header(cd::ui::renderer::DrawBatcher& batcher,
                                    const cd::ui::widgets::Theme&  theme,
                                    float x, float y, float w) noexcept;
};

}  // namespace cd::editor::panel::vehicle_editor
