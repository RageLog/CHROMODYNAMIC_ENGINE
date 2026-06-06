// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_status_bar_fps_sparkline/FpsSparkline.hpp
//
// phase787 — cd::editor::status_bar::FpsSparkline
//
// Live FPS history visualiser for the status bar.
//
// Ring buffer of up to 120 per-frame times (in milliseconds). Each call to
// push(frame_ms) appends one sample, overwriting the oldest when the buffer
// is full. draw() renders the waveform as a series of thin quads (1 px high)
// scaled to the available height, occupying a 200 px wide strip.
//
// Colour encoding (based on average frame time over the filled window):
//   avg < 16.67 ms  → accent_success  (60+ fps  — green)
//   avg < 33.33 ms  → accent_warning  (30–60 fps — amber)
//   avg >= 33.33 ms → accent_error    (<30 fps   — red)
//
// The background fill is a dark strip so the waveform is legible on any
// status bar colour. The tallest bar always fills the full height so the
// sparkline self-scales; the floor maps to 0 ms and the ceiling maps to the
// worst sample in the window (min 33.33 ms so the scale doesn't shrink on a
// perfectly smooth 120 fps run).
//
// Layout (left → right inside `bounds`):
//   [2px pad] [waveform bars × N] [2px pad]
//
// Typical integration:
//   // Once per frame, before draw_status_bar():
//   g_fps_sparkline.push(dt_ms);
//
//   // Inside the status bar draw pass:
//   const uw::Rect spark_rect { sparkline_x, bar_y + 2.0F,
//                               FpsSparkline::kWidth, kStatusBarH - 4.0F };
//   g_fps_sparkline.draw(batcher, theme, spark_rect);
//
// MOMENT: a dev drops a scene asset, watches the sparkline dip from green to
// red, immediately spots the streaming hitch — no profiler needed.
//
// Thread safety:
//   push() / draw() — single render/logic thread only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// Forward declarations.
namespace cd::ui::widgets
{
struct Theme;
struct Rect;
}  // namespace cd::ui::widgets

namespace cd::editor::status_bar
{

// ---------------------------------------------------------------------------
// FpsSparkline
// ---------------------------------------------------------------------------

class FpsSparkline
{
public:
    // Width occupied in the status bar, pixels.
    static constexpr float kWidth = 200.0F;

    // Ring capacity: 120 frames (~2 s @ 60 Hz).
    static constexpr std::size_t kCapacity = 120;

    // Frame-time thresholds for colour coding (milliseconds).
    static constexpr float kThresholdGreen  = 16.67F;  ///< 60 fps
    static constexpr float kThresholdAmber  = 33.33F;  ///< 30 fps

    // Construction -----------------------------------------------------------

    FpsSparkline() noexcept = default;

    // Non-copyable; moveable.
    FpsSparkline(const FpsSparkline&)            = delete;
    FpsSparkline& operator=(const FpsSparkline&) = delete;
    FpsSparkline(FpsSparkline&&)                 = default;
    FpsSparkline& operator=(FpsSparkline&&)      = default;

    // Data API ---------------------------------------------------------------

    /// Append one frame's time in milliseconds (must be > 0).
    /// Negative / zero values are silently ignored.
    void push(float frame_ms) noexcept;

    /// Reset the ring to empty.
    void reset() noexcept;

    /// Number of valid samples currently stored.
    [[nodiscard]] std::size_t filled() const noexcept;

    /// Arithmetic mean of the filled window in milliseconds (0 if empty).
    [[nodiscard]] float avg_ms() const noexcept;

    // Draw API ---------------------------------------------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// Layout:
    ///   1. Dark background fill (full bounds).
    ///   2. Per-sample bar quads: column width = bounds.w / kCapacity;
    ///      height proportional to frame_ms relative to the window maximum.
    ///   3. Top separator line (1 px, divider color, bottom of bounds).
    ///
    /// Colour = accent_success / accent_warning / accent_error depending on avg_ms().
    /// No-op when bounds.w <= 0 or bounds.h <= 0.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Theme&  theme,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    // Ring buffer storage.
    std::array<float, kCapacity> buf_ {};
    std::size_t write_idx_ { 0 };
    std::size_t filled_    { 0 };

    // Layout constant.
    static constexpr float kPad = 2.0F;

    // Minimum scale ceiling so the bars don't fill to the top on perfect runs.
    static constexpr float kMinCeiling = kThresholdAmber;  // 33.33 ms

    /// Return the max value in the filled window (or kMinCeiling if empty).
    [[nodiscard]] float window_max_ms() const noexcept;

    /// Colour corresponding to the current avg_ms tier.
    [[nodiscard]] cd::ui::renderer::Color
    sparkline_color(const cd::ui::widgets::Theme& theme) const noexcept;
};

}  // namespace cd::editor::status_bar
