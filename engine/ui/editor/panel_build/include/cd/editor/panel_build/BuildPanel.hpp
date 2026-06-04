// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_build/BuildPanel.hpp
//
// phase699 — cd::editor::panel::build  (panel_build library)
//
// Build Panel: live compile-progress view for the editor.
//
// The top half shows the current build Status as a colour-coded badge:
//   kIdle       — grey    (no build running)
//   kCompiling  — yellow  (build in flight)
//   kSuccess    — green   (last build succeeded)
//   kFailed     — red     (last build had errors)
//
// The bottom half is a scrollable event log — each BuildEvent shows a
// timestamp (ms), source file, line number, and message, colour-coded by
// the event's severity (which is also expressed as a Status value).
//
// Sprint-1 interaction:
//   Clicking a BuildEvent that has a non-empty source_file emits the index
//   of that event into selected_event_index_ (Sprint-2 will open the file
//   in the code editor at the given line).
//
// Class API
// ─────────
//   set_status(Status)             — update the current build status badge
//   push_event(const BuildEvent&)  — append a new event to the log
//   clear_events()                 — discard all events
//   current_status() const         — query the current status
//   event_count() const            — number of events in the log
//   selected_event_index() const   — last event index clicked (nullopt if none)
//   simulate_click(x, y, bounds)   — hit-test the event list; selects the row
//   draw(DrawBatcher&, Theme&, Rect&) — emit draw commands
//
// Lifetime contract:
//   Default-constructible; owns all data by value.
//   Thread-safe for read; write API must be called from a single thread.
//
// MOMENT: A dev presses Build, sees the compile progress live in the
// BuildPanel — green check when done, red errors with file+line on failure.
// No external terminal needed.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cd::editor::panel::build
{

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
/// Build lifecycle state — drives the badge colour and per-event tinting.
enum class Status : uint8_t
{
    kIdle       = 0,  ///< No build running.
    kCompiling  = 1,  ///< Build in flight.
    kSuccess    = 2,  ///< Last build succeeded.
    kFailed     = 3,  ///< Last build had errors.
};

// ---------------------------------------------------------------------------
// BuildEvent
// ---------------------------------------------------------------------------
/// A single diagnostic/progress entry emitted during a build.
struct BuildEvent
{
    double      timestamp_ms {};              ///< Monotonic time of the event (ms).
    std::string message      {};              ///< Human-readable message.
    std::string source_file  {};              ///< Source file path (may be empty).
    uint32_t    line         { 0U };          ///< Line number (0 = not applicable).
    Status      severity     { Status::kIdle }; ///< Colour-codes the row.
};

// ---------------------------------------------------------------------------
// BuildPanel
// ---------------------------------------------------------------------------
class BuildPanel
{
public:
    // Default-constructible; starts kIdle with an empty event log.
    BuildPanel() noexcept = default;

    // ---- State API ----------------------------------------------------------

    /// Update the current build status badge.
    void set_status(Status status) noexcept;

    /// Append a new event to the log.
    void push_event(const BuildEvent& event);

    /// Discard all events and reset the selected index.
    void clear_events() noexcept;

    /// Query the current build status.
    [[nodiscard]] Status current_status() const noexcept;

    /// Number of events currently in the log.
    [[nodiscard]] std::size_t event_count() const noexcept;

    /// Index of the last event that was clicked (nullopt if none).
    [[nodiscard]] std::optional<std::size_t> selected_event_index() const noexcept;

    // ---- Interaction helpers (testing + shell integration) ------------------

    /// Hit-test at (x, y) in the same coordinate space as `bounds`.
    /// Selects the event row under the click if it has a non-empty source_file.
    void simulate_click(float x, float y,
                        const cd::ui::widgets::Rect& bounds) noexcept;

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Renders (in order):
    ///   1. Panel background fill.
    ///   2. Status badge in the top half (colour-coded rectangle).
    ///   3. Accent separator bar between the two halves.
    ///   4. Event log rows in the bottom half (colour-coded by severity).
    ///      Rows that overflow the bottom of the panel are clipped.
    ///
    /// Thread-safety: call from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    // ---- Data ---------------------------------------------------------------
    Status                       status_   { Status::kIdle };
    std::vector<BuildEvent>      events_   {};
    std::optional<std::size_t>   selected_ {};

    // ---- Layout constants ---------------------------------------------------
    static constexpr float kPad        =  6.0F;   ///< Outer padding.
    static constexpr float kBadgeH     = 32.0F;   ///< Height of the status badge.
    static constexpr float kBarH       =  4.0F;   ///< Accent separator bar height.
    static constexpr float kRowH       = 18.0F;   ///< Height of one event row.
    static constexpr float kRowGap     =  2.0F;   ///< Gap between rows.
    static constexpr float kTsColW     = 56.0F;   ///< Timestamp column width.

    /// Y offset (relative to bounds.y) where the first event row begins.
    static constexpr float kListOffsetY =
        kPad + kBadgeH + kPad + kBarH + kPad;

    /// Y coordinate of event row `i` relative to bounds.y.
    [[nodiscard]] static float row_top(std::size_t i) noexcept
    {
        return kListOffsetY + static_cast<float>(i) * (kRowH + kRowGap);
    }

    /// Returns the RGBA colour used for a given Status in the badge / rows.
    [[nodiscard]] static cd::ui::renderer::Color status_color(Status s) noexcept;
};

}  // namespace cd::editor::panel::build
