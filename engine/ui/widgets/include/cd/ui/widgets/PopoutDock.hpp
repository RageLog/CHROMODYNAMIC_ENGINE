// =============================================================================
// CHROMODYNAMIC -- cd/ui/widgets/PopoutDock.hpp
//
// Phase 689 — PopoutDock: Sprint-1 internal detach/reattach state machine.
//
// Design intent
// -------------
// PopoutDock TRACKS which panels a user has "torn off" from the main
// DockSpace and where they would live in a multi-window layout.
// It does NOT create native OS windows — that is Sprint-2, gated on
// cd::platform multi-window support.
//
// apps/editor iterates `detached_windows()` each frame and renders those
// panels as floating-internal overlays (drawn on top of the main dock
// rect). When cd::platform gains multi-window support, the same state
// object drives the real OS-window placement without API changes.
//
// Moment
// ------
// A designer mentally models a two-monitor layout: drag 'Inspector' out
// of the dock, drag 'Console' to a second logical position. The panel
// positions survive across the editor tick. When Sprint-2 ships, those
// exact positions become the initial native-window geometry — zero
// migration cost.
//
// Thread model: single-threaded UI thread only. No locking.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::ui::widgets
{

// ---------------------------------------------------------------------------
// PopoutWindow
// ---------------------------------------------------------------------------

/// Describes a single panel that has been torn off the main dock.
///
/// `position` and `size` are in logical screen pixels (top-left origin).
/// Sprint-2 will promote these to native-window creation parameters.
struct PopoutWindow
{
    std::string            panel_id  {};
    std::array<float, 2U>  position  { 0.0F, 0.0F };  ///< top-left (x, y)
    std::array<float, 2U>  size      { 0.0F, 0.0F };  ///< (w, h)
    bool                   is_active { true };         ///< false = minimised / hidden
};

// ---------------------------------------------------------------------------
// PopoutDock
// ---------------------------------------------------------------------------

/// Sprint-1 state machine for multi-panel detach / reattach workflows.
///
/// Rules:
///   * A panel_id can appear AT MOST ONCE in the detached list.
///   * `detach_panel` returns false when the panel is already detached.
///   * `reattach_panel` returns false when the panel is not detached.
///   * `simulate_drag` is a no-op when the panel is not detached.
///   * `detached_windows()` returns a stable span; iterating it while
///     calling mutation methods is undefined behaviour (same as std::vector).
class PopoutDock
{
public:
    PopoutDock() = default;

    PopoutDock(const PopoutDock&)            = delete;
    PopoutDock& operator=(const PopoutDock&) = delete;
    PopoutDock(PopoutDock&&)                 = default;
    PopoutDock& operator=(PopoutDock&&)      = default;

    // ---- Detach / reattach -------------------------------------------------

    /// Detach `panel_id` into a floating popout at `position` with `size`.
    /// Returns false (no-op) if the panel is already detached.
    [[nodiscard]] bool detach_panel(std::string_view panel_id,
                                    std::array<float, 2U> position,
                                    std::array<float, 2U> size);

    /// Reattach a previously detached panel back to the main dock.
    /// Returns false when `panel_id` is not currently detached.
    [[nodiscard]] bool reattach_panel(std::string_view panel_id);

    // ---- Query ------------------------------------------------------------

    /// True when `panel_id` is currently in the detached list.
    [[nodiscard]] bool is_detached(std::string_view panel_id) const noexcept;

    /// Read-only view of all currently detached windows.
    /// The span is invalidated by any call to `detach_panel` or
    /// `reattach_panel` (the backing vector may reallocate).
    [[nodiscard]] std::span<const PopoutWindow> detached_windows() const noexcept;

    // ---- Simulation -------------------------------------------------------

    /// Update the stored position of an already-detached panel.
    /// Models a drag gesture; the new position becomes the "current" top-left
    /// origin that apps/editor uses for floating-internal rendering.
    /// No-op when `panel_id` is not detached.
    void simulate_drag(std::string_view panel_id,
                       std::array<float, 2U> new_position) noexcept;

private:
    /// Linear search is acceptable: typical detached panel count < 10.
    [[nodiscard]] PopoutWindow* find(std::string_view panel_id) noexcept;
    [[nodiscard]] const PopoutWindow* find(std::string_view panel_id) const noexcept;

    std::vector<PopoutWindow> windows_;
};

}  // namespace cd::ui::widgets
