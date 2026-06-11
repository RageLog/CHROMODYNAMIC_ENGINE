// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_perf_profiler/PerfProfiler.hpp
//
// phase700 — cd::editor::panel::perf_profiler (panel_perf_profiler library)
//
// Deep, frame-by-frame performance profiler panel for the editor.
// Aggregates CPU markers, GPU markers, and frame-graph pass records into a
// per-frame snapshot and exposes a 60-frame rolling history with visual
// over/under-budget colour coding, drill-down into a selected frame, and
// p99/avg/min/max statistics.
//
// Provides:
//   FrameSnapshot   — POD capture of one frame: total_ms + CPU markers +
//                     GPU marker samples + GPU pass records.
//   PerfProfiler    — ring of FrameSnapshots; draw() emits the full three-
//                     section layout (history bars / drill-down / stats strip)
//                     into a cd::ui::renderer::DrawBatcher.
//
// Layout (top → bottom):
//   Section A — 60-frame bar chart.  Each bar's height is proportional to
//               total_ms.  Bars exceeding the target budget are drawn in the
//               theme's error colour; in-budget bars are green.  Clicking a
//               bar (not modelled here — caller sets selected_frame_index_)
//               reveals its details in Section B.
//
//   Section B — drill-down list.  CPU markers (name + duration_ms each as a
//               horizontal proportional bar) followed by GPU pass records
//               (same layout).  Empty when no frame is selected.
//
//   Section C — stats strip.  avg / min / max / p99 total_ms over the last
//               min(recorded_count, 60) frames drawn as four labelled quads.
//
// API:
//   void capture_frame(const FrameSnapshot&)
//     — Append one snapshot to the ring.  Oldest entry is evicted when the
//       ring is full (capacity == kHistoryCapacity).
//
//   [[nodiscard]] std::size_t recorded_frame_count() const
//     — Number of snapshots currently stored (0..kHistoryCapacity).
//
//   void clear()
//     — Discard all snapshots and reset selection.
//
//   void set_target_fps(float fps)
//     — Recompute the budget line (budget_ms_ = 1000.0 / fps).
//
//   void select_frame(std::size_t ring_index)
//     — Pin the drill-down to the snapshot at the given ring index.
//       Out-of-range indices clamp to the last frame.
//
//   void draw(DrawBatcher&, const Theme&, const Rect& bounds) const
//     — Emit the three-section layout.  No-op for zero-sized bounds.
//
// MOMENT: a perf-sensitive dev captures 60 frames, sees a spike at frame 23,
// selects it, and reads the 'TLAS rebuild' GPU pass at 12 ms — root cause
// spotted in seconds without leaving the editor.
//
// Thread safety:
//   capture_frame / clear / set_target_fps — single-thread (render thread).
//   draw / recorded_frame_count — read-only; safe to call from any thread
//   that does NOT race with a write call.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cd/profile/cpu_marker_overlay/CpuMarkerOverlay.hpp>
#include <cd/profile/frame_graph_timeline/FrameGraphTimeline.hpp>
#include <cd/profile/gpu_marker/GpuMarker.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Forward-declare heavy UI types so this header stays lightweight.
namespace cd::ui::renderer
{
class DrawBatcher;
} // namespace cd::ui::renderer
namespace cd::ui::theme
{
struct Theme;
} // namespace cd::ui::theme
namespace cd::ui::widgets
{
struct Rect;
} // namespace cd::ui::widgets

namespace cd::editor::panel::perf_profiler
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Maximum number of FrameSnapshots kept in the history ring.
inline constexpr std::size_t kHistoryCapacity = 60U;

/// Default target frame budget, milliseconds (60 fps).
inline constexpr float kDefaultBudgetMs = 16.67F;

// ---------------------------------------------------------------------------
// FrameSnapshot — one frame worth of profiling data
// ---------------------------------------------------------------------------

/// A complete capture of one rendered frame for later inspection.
struct FrameSnapshot
{
    /// Total wall-clock time for the frame in milliseconds.
    double total_ms { 0.0 };

    /// All CPU marker samples collected during this frame via
    /// cd::profile::cpu_marker_overlay::Collector.
    std::vector<cd::profile::cpu_marker_overlay::MarkerSample> cpu_markers;

    /// All GPU marker samples (after Recorder::resolve()) for this frame.
    std::vector<cd::profile::gpu_marker::GpuMarkerSample> gpu_markers;

    /// GPU frame-graph pass records for this frame (after Timeline::end_frame()).
    std::vector<cd::profile::frame_graph_timeline::PassRecord> gpu_passes;
};

// ---------------------------------------------------------------------------
// PerfProfiler
// ---------------------------------------------------------------------------

class PerfProfiler
{
public:
    // Construction -----------------------------------------------------------

    /// Default-construct with a 60 fps target budget.
    PerfProfiler() noexcept = default;

    // Non-copyable; moveable.
    PerfProfiler(const PerfProfiler&)            = delete;
    PerfProfiler& operator=(const PerfProfiler&) = delete;
    PerfProfiler(PerfProfiler&&)                 = default;
    PerfProfiler& operator=(PerfProfiler&&)      = default;

    // Data API ---------------------------------------------------------------

    /// Append a snapshot to the rolling ring.  When the ring is full the
    /// oldest snapshot is evicted (FIFO).  Sets the selection to the newest
    /// frame after each capture.
    void capture_frame(const FrameSnapshot& snap);

    /// Number of snapshots currently in the ring [0..kHistoryCapacity].
    [[nodiscard]] std::size_t recorded_frame_count() const noexcept;

    /// Discard all snapshots, reset selection index, and clear stats cache.
    void clear() noexcept;

    /// Set the per-frame time budget used for the over/under-budget colour
    /// coding.  fps must be > 0; values <= 0 are silently ignored.
    void set_target_fps(float fps) noexcept;

    /// Returns the current budget in milliseconds.
    [[nodiscard]] float budget_ms() const noexcept { return budget_ms_; }

    /// Pin the drill-down to `ring_index`.  Out-of-range values clamp to
    /// the most recently captured frame (recorded_frame_count() - 1).
    void select_frame(std::size_t ring_index) noexcept;

    /// Returns the currently selected ring index (or 0 when empty).
    [[nodiscard]] std::size_t selected_frame_index() const noexcept;

    // Draw API ---------------------------------------------------------------

    /// Emit the three-section layout into `batcher` within `bounds`.
    ///
    ///   Section A (top 40 %) — 60-frame bar chart.
    ///   Section B (middle 40 %) — drill-down for the selected frame.
    ///   Section C (bottom 20 %) — stats strip (avg/min/max/p99).
    ///
    /// No-op when bounds.w <= 0 or bounds.h <= 0.
    /// Does NOT call begin_frame() / end_frame() on the batcher.
    void draw(cd::ui::renderer::DrawBatcher&   batcher,
              const cd::ui::theme::Theme&      theme,
              const cd::ui::widgets::Rect&     bounds) const;

private:
    // ---- Ring buffer --------------------------------------------------------

    /// Fixed-capacity ring storing FrameSnapshots.
    std::array<FrameSnapshot, kHistoryCapacity> ring_ {};

    /// Index of the next write slot (circular).
    std::size_t head_      { 0U };

    /// Number of valid entries currently in the ring.
    std::size_t count_     { 0U };

    /// Currently selected frame (ring-relative oldest-first index).
    std::size_t selected_  { 0U };

    // ---- Config -------------------------------------------------------------

    float budget_ms_ { kDefaultBudgetMs };

    // ---- Section renderers --------------------------------------------------

    /// Draw the 60-frame bar chart in the top portion of `content`.
    void draw_history_bars(cd::ui::renderer::DrawBatcher& batcher,
                           const cd::ui::theme::Theme&    theme,
                           const cd::ui::widgets::Rect&   section) const;

    /// Draw the drill-down marker list for the selected frame.
    void draw_drill_down(cd::ui::renderer::DrawBatcher& batcher,
                         const cd::ui::theme::Theme&    theme,
                         const cd::ui::widgets::Rect&   section) const;

    /// Draw the avg / min / max / p99 stats strip.
    void draw_stats_strip(cd::ui::renderer::DrawBatcher& batcher,
                          const cd::ui::theme::Theme&    theme,
                          const cd::ui::widgets::Rect&   section) const;

    // ---- Ring helpers -------------------------------------------------------

    /// Return the snapshot at ring-relative index `i` (0 = oldest).
    /// Precondition: i < count_.
    [[nodiscard]] const FrameSnapshot& snapshot_at(std::size_t i) const noexcept;

    // ---- Layout constants ---------------------------------------------------

    /// Fraction of total height allocated to the history bar chart.
    static constexpr float kHistoryFraction   = 0.40F;
    /// Fraction of total height allocated to the drill-down section.
    static constexpr float kDrillDownFraction = 0.40F;
    /// Remaining fraction for the stats strip (kStatsFraction = 1 - above).

    static constexpr float kBorderW  = 1.0F;
    static constexpr float kRowH     = 8.0F;   ///< Height of one marker row in Section B.
    static constexpr float kStatH    = 16.0F;  ///< Height of one stat quad in Section C.
};

}  // namespace cd::editor::panel::perf_profiler
