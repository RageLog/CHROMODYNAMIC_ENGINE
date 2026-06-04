// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_auto_save_indicator/AutoSaveIndicator.hpp
//
// phase745 — cd::editor::panel::auto_save_indicator (panel_auto_save_indicator)
//
// Auto-Save Indicator: a compact status-bar widget that tells the user
// whether their work is persisted.
//
// Five-state Status enum drives the displayed icon + label + colour:
//
//   kIdle           — "Idle"              grey / divider colour
//   kPendingDirty   — "Unsaved changes"   accent_warning (amber)
//   kSaving         — "Saving..."         yellow (spinner implied)
//   kJustSaved      — "Saved Ns ago"      green
//   kError          — "Save failed"       accent_error (red)
//
// Class AutoSaveIndicator (50 px high; fits in a status bar or corner)
// ──────────────────────────────────────────────────────────────────────
//   set_status(Status)              — drive the badge state explicitly
//   mark_dirty()                    — transitions → kPendingDirty
//   mark_saved()                    — transitions → kJustSaved, resets timer
//   set_last_save_ms_ago(double)    — update the elapsed-since-save counter
//   current_status() const          — query current state
//   draw(DrawBatcher&, Theme&, Rect&) — emit draw commands
//
// Layout:
//   [ 6px pad | 12×12 icon quad | 4px gap | label text area | 6px pad ]
//   Single row, vertically centered.  No scroll.  No multi-line.
//   Actual glyph rendering is the caller's concern; this class emits the
//   background fill quad (colour-coded) and the icon/label background rects.
//
// MOMENT: A user edits a scene, makes 5 changes — the badge switches from
// "Idle" to "Unsaved changes" (amber) immediately.  Auto-save fires:
// "Saving..." (yellow) → "Saved just now" (green).  Trust signal at a glance.
//
// Thread safety:
//   All mutating methods — single render/logic thread.
//   draw() / current_status() — read-only; safe from any thread that does NOT
//   race with a write call.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>

#include <cstdint>

// Forward-declare layout types to keep this header lightweight.
namespace cd::ui::theme
{
struct Theme;
}
namespace cd::ui::widgets
{
struct Rect;
}

namespace cd::editor::panel::auto_save_indicator
{

// ---------------------------------------------------------------------------
// Status — five-state save lifecycle
// ---------------------------------------------------------------------------

/// Lifecycle state of the auto-save subsystem.
enum class Status : std::uint8_t
{
    kIdle          = 0,  ///< No unsaved changes; display "Idle".
    kPendingDirty  = 1,  ///< Unsaved changes present; display "Unsaved changes".
    kSaving        = 2,  ///< Save in flight; display "Saving...".
    kJustSaved     = 3,  ///< Save completed recently; display "Saved Ns ago".
    kError         = 4,  ///< Save failed; display "Save failed".
};

// ---------------------------------------------------------------------------
// AutoSaveIndicator
// ---------------------------------------------------------------------------

class AutoSaveIndicator
{
public:
    // Construction -----------------------------------------------------------

    /// Default-construct in kIdle state with last_save_ms_ago_ == 0.
    AutoSaveIndicator() noexcept = default;

    // Non-copyable; moveable.
    AutoSaveIndicator(const AutoSaveIndicator&)            = delete;
    AutoSaveIndicator& operator=(const AutoSaveIndicator&) = delete;
    AutoSaveIndicator(AutoSaveIndicator&&)                 = default;
    AutoSaveIndicator& operator=(AutoSaveIndicator&&)      = default;

    // State API --------------------------------------------------------------

    /// Directly set the status.  Prefer mark_dirty() / mark_saved() for the
    /// common transitions; this exists for explicit override (e.g. kError).
    void set_status(Status s) noexcept;

    /// Transition to kPendingDirty (call whenever the document becomes dirty).
    void mark_dirty() noexcept;

    /// Transition to kJustSaved and reset last_save_ms_ago_ to 0.
    void mark_saved() noexcept;

    /// Update the elapsed time since the last save (call each frame while in
    /// kJustSaved state to keep the "Saved Ns ago" counter current).
    void set_last_save_ms_ago(double ms) noexcept;

    /// Return the current status.
    [[nodiscard]] Status current_status() const noexcept;

    /// Return elapsed ms since last save (meaningful only in kJustSaved).
    [[nodiscard]] double last_save_ms_ago() const noexcept;

    // Draw API ---------------------------------------------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Layout (left → right, vertically centered):
    ///   Background fill (colour-coded per Status).
    ///   Icon quad  (12×12, same colour, higher alpha).
    ///   Label background quad (remaining width, slightly transparent).
    ///
    /// No-op when bounds.w <= 0 or bounds.h <= 0.
    /// Does NOT call begin_frame() / end_frame() on the batcher.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::theme::Theme&    theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    Status status_           { Status::kIdle };
    double last_save_ms_ago_ { 0.0 };

    // Layout constants -------------------------------------------------------
    static constexpr float kPad      =  6.0F;   ///< Horizontal outer padding.
    static constexpr float kIconSize = 12.0F;   ///< Icon quad side length.
    static constexpr float kIconGap  =  4.0F;   ///< Gap between icon and label.
    static constexpr float kBorderW  =  1.0F;   ///< Border quad width.

    /// Return the fill colour (RGBA u8) for the given status.
    [[nodiscard]] static cd::ui::renderer::Color
    status_fill_color(Status s) noexcept;

    /// Return the icon/accent colour for the given status.
    [[nodiscard]] static cd::ui::renderer::Color
    status_icon_color(Status s) noexcept;
};

}  // namespace cd::editor::panel::auto_save_indicator
