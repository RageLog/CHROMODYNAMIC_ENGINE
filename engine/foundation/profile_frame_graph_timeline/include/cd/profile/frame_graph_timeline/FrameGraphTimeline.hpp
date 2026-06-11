// =============================================================================
// CHROMODYNAMIC — cd/profile/frame_graph_timeline/FrameGraphTimeline.hpp
//
// Phase 593 — GPU-side complement to cd::profile::cpu_marker_overlay.
//
// Provides:
//   PassRecord       — one recorded GPU render-pass event (name, gpu_start_ms,
//                      gpu_duration_ms, graph_node_id).
//   Rect             — lightweight axis-aligned rect for draw bounds.
//   Timeline         — per-frame accumulator of PassRecords; caller feeds GPU
//                      query readback results via record_pass().
//   TimelineOverlay  — horizontal Gantt-chart renderer that emits one coloured
//                      quad per pass into a cd::ui::renderer::DrawBatcher.
//
// Design notes:
//   * Timeline is NOT thread-safe by design. GPU query readback happens on a
//     single render thread; no mutex overhead required.
//   * Two internal double-buffers: the "current" frame accumulates records
//     while begin_frame/end_frame are in flight; the "last" frame is what
//     last_frame_passes() / last_frame_total_ms() expose. This lets the UI
//     read the previous frame's data while the new frame builds up.
//   * TimelineOverlay depends on cd::ui_renderer (DrawBatcher). The include
//     is in the .cpp only; the header forward-declares DrawBatcher so this
//     header remains lightweight.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Forward-declare to avoid pulling the full ui renderer header into every
// translation unit that includes this header.
namespace cd::ui::renderer
{
class DrawBatcher;
} // namespace cd::ui::renderer

namespace cd::profile::frame_graph_timeline
{

// ---------------------------------------------------------------------------
// PassRecord — one completed GPU render-pass timing
// ---------------------------------------------------------------------------

/// A single recorded GPU render-pass event.
struct PassRecord
{
    std::string   pass_name;         ///< Human-readable pass label.
    double        gpu_start_ms;      ///< GPU start time in milliseconds (from query readback).
    double        gpu_duration_ms;   ///< GPU duration in milliseconds.
    std::uint32_t graph_node_id;     ///< Frame-graph node identifier (opaque to Timeline).
};

// ---------------------------------------------------------------------------
// Rect — lightweight axis-aligned rect for draw bounds
// ---------------------------------------------------------------------------

struct Rect
{
    float x      { 0.0F };
    float y      { 0.0F };
    float width  { 0.0F };
    float height { 0.0F };
};

// ---------------------------------------------------------------------------
// Timeline — per-frame accumulator of GPU pass timings
// ---------------------------------------------------------------------------

/// Accumulates GPU render-pass timing records for one logical frame.
///
/// Usage:
///   timeline.begin_frame();
///   // For each GPU query readback result:
///   timeline.record_pass("ShadowMap", start_ms, dur_ms, node_id);
///   timeline.end_frame();
///   // Safe to inspect until next begin_frame():
///   auto passes = timeline.last_frame_passes();
///   double total = timeline.last_frame_total_ms();
///
/// NOT thread-safe — call from a single render thread.
class Timeline
{
public:
    Timeline() = default;

    Timeline(const Timeline&)            = delete;
    Timeline& operator=(const Timeline&) = delete;
    Timeline(Timeline&&)                 = default;
    Timeline& operator=(Timeline&&)      = default;

    /// Reset the current-frame accumulator. Must be called before record_pass.
    void begin_frame();

    /// Append one GPU pass timing to the current frame.
    /// May be called multiple times per frame (once per render pass readback).
    void record_pass(std::string_view pass_name,
                     double           gpu_start_ms,
                     double           gpu_duration_ms,
                     std::uint32_t    node_id);

    /// Finalise the current frame: compute total_ms and swap current → last.
    /// After end_frame() returns, last_frame_passes() reflects this frame.
    void end_frame();

    /// Read-only view of the passes recorded in the last completed frame.
    /// The span is valid until the next begin_frame() call.
    [[nodiscard]] std::span<const PassRecord> last_frame_passes() const noexcept;

    /// Sum of all gpu_duration_ms in the last completed frame.
    /// Returns 0.0 before the first end_frame().
    [[nodiscard]] double last_frame_total_ms() const noexcept;

private:
    std::vector<PassRecord> current_passes_;  ///< Accumulates during current frame.
    std::vector<PassRecord> last_passes_;     ///< Snapshot from the last end_frame().
    double                  last_total_ms_  { 0.0 };
};

// ---------------------------------------------------------------------------
// TimelineOverlay — Gantt-chart renderer into a DrawBatcher
// ---------------------------------------------------------------------------

/// Renders PassRecords as a horizontal Gantt chart within `bounds`.
///
/// Time axis: left = min(pass.gpu_start_ms), right = min + window_ms.
/// Each pass is mapped to a single horizontal bar whose width reflects
/// gpu_duration_ms. Passes with duration == 0 are drawn at least 1 pixel wide.
/// Bar colour is derived from a stable hash of pass_name (similar to Tracy).
///
/// The caller is responsible for framing (begin_frame / end_frame) on the
/// DrawBatcher. draw() only emits quads — it does not flush or submit.
class TimelineOverlay
{
public:
    /// window_ms controls the visible time range on the x-axis.
    /// Passes entirely outside [min_start, min_start+window_ms] are culled.
    explicit TimelineOverlay(double window_ms = 16.0);

    /// Emit one coloured quad per visible pass into `batcher`.
    /// `passes` must remain valid for the duration of this call.
    void draw(cd::ui::renderer::DrawBatcher&   batcher,
              std::span<const PassRecord>       passes,
              Rect                              bounds) const;

    void   set_window_ms(double window_ms) noexcept { window_ms_ = window_ms; }
    [[nodiscard]] double window_ms() const noexcept { return window_ms_; }

private:
    double window_ms_;
};

}  // namespace cd::profile::frame_graph_timeline
