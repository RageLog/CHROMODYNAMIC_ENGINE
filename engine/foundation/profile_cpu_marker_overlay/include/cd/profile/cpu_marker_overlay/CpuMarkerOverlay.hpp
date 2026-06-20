// =============================================================================
// CHROMODYNAMIC — cd/profile/cpu_marker_overlay/CpuMarkerOverlay.hpp
//
// Phase 587 — Tracy-style CPU marker bar-chart overlay.
//
// Provides:
//   MarkerSample  — one recorded CPU marker event (name, start_ms,
//                   duration_ms, thread_id).
//   MarkerHandle  — opaque index returned by Collector::begin().
//   Collector     — thread-safe ring of MarkerSamples; begin/end
//                   bracket a named CPU region.
//   Scope         — RAII wrapper around Collector::begin / end.
//   Overlay       — CPU-only draw helper that emits one solid quad per
//                   sample into a cd::ui::renderer::DrawBatcher; callers
//                   supply bounds and the sample slice to render.
//
// Design notes:
//   * Collector is lock-protected and designed for infrequent per-frame
//     calls, not hot-path zero-overhead instrumentation. Hot-path tracing
//     belongs to cd::profile (Scope + BufferSink).
//   * samples_since() returns a std::vector so callers can sort/filter
//     before handing the slice to Overlay::draw().
//   * Overlay depends on cd::ui_renderer (DrawBatcher). The include is
//     only in the .cpp; the header forward-declares DrawBatcher so this
//     header remains lightweight.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <memory>
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

namespace cd::profile::cpu_marker_overlay
{

// ---------------------------------------------------------------------------
// MarkerSample — one completed CPU region
// ---------------------------------------------------------------------------

/// A single recorded CPU marker event.
struct MarkerSample
{
    std::string   name;                 ///< Region label (copy; safe after Collector flush).
    double        start_ms { 0.0 };      ///< Absolute steady_clock time in milliseconds.
    double        duration_ms { 0.0 };   ///< Duration in milliseconds.
    std::uint32_t thread_id { 0 };       ///< OS-level thread ID (truncated to 32 bits).
};

// ---------------------------------------------------------------------------
// MarkerHandle — opaque reference to an in-flight begin()
// ---------------------------------------------------------------------------

/// Returned by Collector::begin(); consumed by Collector::end().
/// The value is an index into the Collector's internal in-flight table.
struct MarkerHandle
{
    std::uint32_t index { ~0U };  ///< ~0 = invalid / not begun.

    [[nodiscard]] bool valid() const noexcept { return index != ~0U; }
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
// MarkerAggregate — per-name statistics over a sample slice
// ---------------------------------------------------------------------------

/// Aggregated statistics for one marker name across N samples.
struct MarkerAggregate
{
    std::string   name;        ///< Marker name (copy).
    std::size_t   count   { 0 };    ///< Number of completed samples with this name.
    double        total_ms{ 0.0 };  ///< Sum of duration_ms across all samples.
    double        avg_ms  { 0.0 };  ///< total_ms / count; 0 if count == 0.
    double        max_ms  { 0.0 };  ///< Maximum duration_ms observed.
};

// ---------------------------------------------------------------------------
// Collector — thread-safe ring-buffer of MarkerSamples
// ---------------------------------------------------------------------------

class Collector
{
public:
    /// Capacity is the maximum number of completed samples the ring retains.
    explicit Collector(std::size_t capacity = 4096);
    ~Collector();

    Collector(const Collector&)            = delete;
    Collector& operator=(const Collector&) = delete;
    Collector(Collector&&)                 = delete;
    Collector& operator=(Collector&&)      = delete;

    /// Open a named region on the calling thread. Returns an opaque handle
    /// that MUST be passed to end(). Thread-safe.
    [[nodiscard]] MarkerHandle begin(std::string_view name);

    /// Close the region identified by handle and commit the MarkerSample to
    /// the ring buffer. Calling end() with an invalid handle is a no-op.
    void end(MarkerHandle handle);

    /// Return all samples whose start_ms >= cutoff_ms. O(n) scan.
    [[nodiscard]] std::vector<MarkerSample> samples_since(double cutoff_ms) const;

    /// Return per-name aggregated statistics for samples whose
    /// start_ms >= cutoff_ms. Order of entries matches first-seen order of
    /// distinct names in the ring. O(n) scan.
    [[nodiscard]] std::vector<MarkerAggregate> aggregate(double cutoff_ms = 0.0) const;

    /// Total number of completed samples retained in the ring (capped by capacity).
    [[nodiscard]] std::size_t sample_count() const noexcept;

    /// Clear all completed samples.
    void clear() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// Scope — RAII wrapper for Collector::begin / end
// ---------------------------------------------------------------------------

/// Construct at the top of a named region; the destructor calls end().
class Scope
{
public:
    Scope(Collector& collector, std::string_view name)
        : collector_(collector)
        , handle_(collector.begin(name))
    {
    }

    ~Scope()
    {
        if (handle_.valid())
            collector_.end(handle_);
    }

    Scope(const Scope&)            = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&)                 = delete;
    Scope& operator=(Scope&&)      = delete;

private:
    Collector&   collector_;
    MarkerHandle handle_;
};

// ---------------------------------------------------------------------------
// BarRect — host-side geometry for one marker bar (no GPU dependency)
// ---------------------------------------------------------------------------

/// The axis-aligned rectangle and colour hash for one marker bar as
/// computed by Overlay::compute_bars().  Useful for host-side layout
/// tests and for custom renderers that do not use cd::ui::renderer.
struct BarRect
{
    float         x           { 0.0F };  ///< Left edge in the supplied bounds space.
    float         y           { 0.0F };  ///< Top edge in the supplied bounds space.
    float         w           { 0.0F };  ///< Width in pixels (>= 1.0).
    float         h           { 0.0F };  ///< Height in pixels (80 % of lane height).
    std::uint32_t colour_hash { 0U   };  ///< djb2 hash of the marker name; drives colour.
};

// ---------------------------------------------------------------------------
// Overlay — Tracy-style horizontal bar chart into a DrawBatcher
// ---------------------------------------------------------------------------

/// Renders MarkerSamples as coloured horizontal bars within `bounds`.
///
/// Time axis: left = min(sample.start_ms), right = min+window_ms.
/// Each thread gets a horizontal lane. Bars whose duration == 0 are drawn
/// as 1-pixel-wide markers.
///
/// The caller is responsible for framing (begin_frame / end_frame) on the
/// DrawBatcher. draw() only emits quads — it does not flush or submit.
class Overlay
{
public:
    /// window_ms controls the visible time range. Samples that fall
    /// entirely outside [min_start, min_start+window_ms] are culled.
    explicit Overlay(double window_ms = 16.0);

    /// Emit one coloured quad per visible sample into `batcher`.
    /// samples must remain valid for the duration of this call.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              std::span<const MarkerSample>  samples,
              Rect                           bounds) const;

    /// Pure host-side layout: returns the BarRect for every visible sample
    /// without touching a DrawBatcher. Useful for unit testing the geometry
    /// math and for custom rendering back-ends.
    /// Samples that fall entirely outside the window are absent from the result.
    [[nodiscard]] std::vector<BarRect>
    compute_bars(std::span<const MarkerSample> samples,
                 Rect                          bounds) const;

    void set_window_ms(double window_ms) noexcept { window_ms_ = window_ms; }
    [[nodiscard]] double window_ms() const noexcept { return window_ms_; }

private:
    double window_ms_;
};

}  // namespace cd::profile::cpu_marker_overlay
