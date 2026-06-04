// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_input_recorder/InputRecorderPanel.hpp
//
// phase665 — cd::editor::panel::input_recorder  (panel_input_recorder library)
//
// Editor panel that drives cd::game::input_recorder::Recorder and
// cd::game::input_recorder::Replayer.
// Provides a DrawBatcher-based draw path for the DockSpace shell (apps/editor).
// Renders:
//
//   * Coloured panel background + separator bar.
//   * Record button  — red when actively recording, grey otherwise.
//   * Stop button    — always rendered; dims when nothing is in flight.
//   * Replay button  — green when replayer is active, grey otherwise.
//   * Timestamp display strip: proportional-fill bar showing
//       "recorded N events" progress or "replaying frame X/N" progress.
//
// State API:
//   set_recorder(Recorder*)        — observe the runtime recorder (nullable).
//   set_replayer(Replayer*)        — observe the runtime replayer (nullable).
//   simulate_click_record_button() — programmatic Record button press (test hook).
//   simulate_click_replay_button() — programmatic Replay button press (test hook).
//
// Lifetime contract:
//   InputRecorderPanel is default-constructible; it holds non-owning raw
//   pointers to the recorder and replayer.  The caller is responsible for
//   ensuring both objects outlive the panel.
//
// MOMENT: A QA tester clicks Record, plays for 30 s, clicks Stop, clicks
//   Replay — and watches the player avatar reproduce the exact sequence
//   frame-accurate.  The panel makes record/replay one-button-easy.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/game/input_recorder/InputRecorder.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>

namespace cd::editor::panel::input_recorder
{

// ---------------------------------------------------------------------------
// InputRecorderPanel
// ---------------------------------------------------------------------------
class InputRecorderPanel
{
public:
    // Default-constructible; starts with null recorder and replayer.
    InputRecorderPanel() noexcept = default;

    // ---- Recorder / Replayer binding API ------------------------------------

    /// Observe a Recorder. Pass nullptr to detach.
    /// The panel reads is_recording() and event_count() each draw.
    /// Ownership remains with the caller — the panel never deletes the pointer.
    void set_recorder(cd::game::input_recorder::Recorder* recorder) noexcept;

    /// Observe a Replayer. Pass nullptr to detach.
    /// The panel reads is_finished(), event_count(), and cursor progress each draw.
    /// Ownership remains with the caller — the panel never deletes the pointer.
    void set_replayer(cd::game::input_recorder::Replayer* replayer) noexcept;

    // ---- Button simulation API (test / programmatic trigger) ----------------

    /// Simulate a click on the Record button within `bounds`.
    /// If a recorder is set and not currently recording, calls start_recording().
    /// If a recorder is set and currently recording, calls stop_recording().
    void simulate_click_record_button(const cd::ui::widgets::Rect& bounds);

    /// Simulate a click on the Replay button within `bounds`.
    /// If a replayer is set, resets the cursor and advances by one event at t=0.
    void simulate_click_replay_button(const cd::ui::widgets::Rect& bounds);

    // ---- DrawBatcher path (DockSpace / apps/editor) -------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    /// Renders: background, separator, Record/Stop/Replay button strips,
    ///          and timestamp/progress display strip.
    ///
    /// Thread-safety: must be called from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher&   batcher,
              const cd::ui::widgets::Theme&    theme,
              const cd::ui::widgets::Rect&     bounds) const;

private:
    cd::game::input_recorder::Recorder* recorder_ { nullptr };  ///< Non-owning observer.
    cd::game::input_recorder::Replayer* replayer_ { nullptr };  ///< Non-owning observer.
};

}  // namespace cd::editor::panel::input_recorder
