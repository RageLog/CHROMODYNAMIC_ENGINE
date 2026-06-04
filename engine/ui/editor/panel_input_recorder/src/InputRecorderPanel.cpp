// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_input_recorder/src/InputRecorderPanel.cpp
//
// phase665 — cd::editor::panel::input_recorder  implementation
// =============================================================================
#include <cd/editor/panel_input_recorder/InputRecorderPanel.hpp>

#include <algorithm>
#include <cstddef>

namespace cd::editor::panel::input_recorder
{

// ---------------------------------------------------------------------------
// Recorder / Replayer binding API
// ---------------------------------------------------------------------------

void InputRecorderPanel::set_recorder(cd::game::input_recorder::Recorder* recorder) noexcept
{
    recorder_ = recorder;
}

void InputRecorderPanel::set_replayer(cd::game::input_recorder::Replayer* replayer) noexcept
{
    replayer_ = replayer;
}

// ---------------------------------------------------------------------------
// Button simulation API
// ---------------------------------------------------------------------------

void InputRecorderPanel::simulate_click_record_button(const cd::ui::widgets::Rect& /*bounds*/)
{
    if (recorder_ == nullptr)
        return;

    if (recorder_->is_recording())
        recorder_->stop_recording();
    else
        recorder_->start_recording();
}

void InputRecorderPanel::simulate_click_replay_button(const cd::ui::widgets::Rect& /*bounds*/)
{
    if (replayer_ == nullptr)
        return;

    // Rewind and deliver the first event (t=0) to confirm the replayer advances.
    replayer_->reset();
    (void)replayer_->next_event(0.0);
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void InputRecorderPanel::draw(cd::ui::renderer::DrawBatcher& batcher,
                               const cd::ui::widgets::Theme&  theme,
                               const cd::ui::widgets::Rect&   bounds) const
{
    // ---- Background fill ----------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    constexpr float kPad  = 6.0F;
    constexpr float kBarH = 4.0F;
    const float     row_w = bounds.w - 2.0F * kPad;

    // ---- Separator bar (accent colour) --------------------------------------
    batcher.quad(bounds.x + kPad, bounds.y + kPad,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r,
                     theme.accent.g,
                     theme.accent.b,
                     theme.accent.a });

    float cursor_y = bounds.y + kPad * 2.0F + kBarH;

    // ---- Sample state -------------------------------------------------------
    const bool        is_recording = (recorder_ != nullptr) && recorder_->is_recording();
    const std::size_t rec_count    = (recorder_ != nullptr) ? recorder_->event_count() : 0U;

    const bool        is_replaying = (replayer_ != nullptr) && !replayer_->is_finished();
    const std::size_t rep_total    = (replayer_ != nullptr) ? replayer_->event_count() : 0U;

    // ---- Record / Stop / Replay button strips --------------------------------
    // Layout: [Record] [Stop] [Replay]  — each kBtnW wide, kBtnH tall, kGap apart.
    {
        constexpr float kBtnW = 48.0F;
        constexpr float kBtnH = 18.0F;
        constexpr float kGap  =  6.0F;

        // Record button: red when recording, grey otherwise.
        const cd::ui::renderer::Color rec_col =
            is_recording
                ? cd::ui::renderer::Color { 220U,  60U,  60U, 230U }  // red  — recording
                : cd::ui::renderer::Color { 140U, 140U, 140U, 180U }; // grey — idle
        batcher.quad(bounds.x + kPad, cursor_y, kBtnW, kBtnH, rec_col);

        // Stop button: always visible; slightly dimmed when nothing is active.
        const bool       any_active = is_recording || is_replaying;
        const cd::ui::renderer::Color stop_col =
            any_active
                ? cd::ui::renderer::Color { 200U, 120U,  40U, 220U }  // orange — active
                : cd::ui::renderer::Color { 100U, 100U, 100U, 140U }; // dark grey — idle
        batcher.quad(bounds.x + kPad + (kBtnW + kGap), cursor_y, kBtnW, kBtnH, stop_col);

        // Replay button: green when replaying, grey otherwise.
        const cd::ui::renderer::Color rep_col =
            is_replaying
                ? cd::ui::renderer::Color {  60U, 200U,  80U, 230U }  // green — replaying
                : cd::ui::renderer::Color { 140U, 140U, 140U, 180U }; // grey  — idle
        batcher.quad(bounds.x + kPad + 2.0F * (kBtnW + kGap), cursor_y, kBtnW, kBtnH, rep_col);

        cursor_y += kBtnH + kPad;
    }

    // ---- Sub-separator ------------------------------------------------------
    batcher.quad(bounds.x + kPad, cursor_y,
                 row_w, kBarH,
                 cd::ui::renderer::Color {
                     theme.accent.r, theme.accent.g, theme.accent.b, 60U });
    cursor_y += kBarH + kPad * 0.5F;

    // ---- Timestamp / progress display strip ---------------------------------
    // Shows "recorded N events" fill when recorder is active/post-record,
    // or "replaying X/N" fill when replayer is active.
    {
        constexpr float kProgH    = 12.0F;
        constexpr float kMaxCount = 10000.0F;  // visual saturation at 10 k events

        // Track background.
        batcher.quad(bounds.x + kPad, cursor_y,
                     row_w, kProgH,
                     cd::ui::renderer::Color {
                         theme.surface_hover.r, theme.surface_hover.g,
                         theme.surface_hover.b, theme.surface_hover.a });

        if (is_recording && rec_count > 0U)
        {
            // Red fill proportional to recorded event count (saturates at kMaxCount).
            const float norm = std::clamp(static_cast<float>(rec_count) / kMaxCount, 0.0F, 1.0F);
            batcher.quad(bounds.x + kPad, cursor_y,
                         row_w * norm, kProgH,
                         cd::ui::renderer::Color { 220U, 60U, 60U, 180U });
        }
        else if (is_replaying && rep_total > 0U)
        {
            // Green fill: replayer cursor position (not directly exposed, but we can
            // estimate from event_count and is_finished state).
            // Use a simple heuristic: full green fill when replayer has loaded events
            // and is not finished. A full frame would expose cursor_, but we only have
            // the public API (event_count + is_finished). Show 100% minus one quantum.
            constexpr float kReplayFill = 0.85F;  // placeholder visual — replayer in flight
            batcher.quad(bounds.x + kPad, cursor_y,
                         row_w * kReplayFill, kProgH,
                         cd::ui::renderer::Color { 60U, 200U, 80U, 180U });
        }
        else if (!is_recording && rec_count > 0U)
        {
            // Post-recording: show the event count in dim accent (full bar = kMaxCount).
            const float norm = std::clamp(static_cast<float>(rec_count) / kMaxCount, 0.0F, 1.0F);
            batcher.quad(bounds.x + kPad, cursor_y,
                         row_w * norm, kProgH,
                         cd::ui::renderer::Color {
                             theme.accent.r, theme.accent.g, theme.accent.b, 120U });
        }
    }
}

}  // namespace cd::editor::panel::input_recorder
