// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_status_bar_fps_sparkline/src/
//                 FpsSparkline.cpp
//
// phase787 — cd::editor::status_bar::FpsSparkline implementation.
//
// Draw layout (left → right inside bounds):
//   [dark background fill]
//   [N thin-quad columns, heights proportional to frame_ms / window_max]
//   [1 px bottom separator]
//
// The ring buffer is a fixed std::array<float, 120>; push() advances write_idx_
// mod kCapacity. copy_in_order() yields the filled portion oldest-first so
// draw() can iterate left-to-right without needing a separate scratch vector.
// =============================================================================
#include <cd/editor/panel_status_bar_fps_sparkline/FpsSparkline.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <algorithm>
#include <cstdint>

namespace cd::editor::status_bar
{

// ===========================================================================
// Data API
// ===========================================================================

void FpsSparkline::push(float frame_ms) noexcept
{
    if (frame_ms <= 0.0F) { return; }
    buf_[write_idx_] = frame_ms;
    write_idx_ = (write_idx_ + 1U) % kCapacity;
    if (filled_ < kCapacity) { ++filled_; }
}

void FpsSparkline::reset() noexcept
{
    write_idx_ = 0U;
    filled_    = 0U;
}

std::size_t FpsSparkline::filled() const noexcept
{
    return filled_;
}

float FpsSparkline::avg_ms() const noexcept
{
    if (filled_ == 0U) { return 0.0F; }
    double sum = 0.0;
    for (std::size_t i = 0U; i < filled_; ++i) { sum += static_cast<double>(buf_[i]); }
    return static_cast<float>(sum / static_cast<double>(filled_));
}

// ===========================================================================
// Private helpers
// ===========================================================================

float FpsSparkline::window_max_ms() const noexcept
{
    if (filled_ == 0U) { return kMinCeiling; }
    float mx = kMinCeiling;
    for (std::size_t i = 0U; i < filled_; ++i)
    {
        if (buf_[i] > mx) { mx = buf_[i]; }
    }
    return mx;
}

cd::ui::renderer::Color
FpsSparkline::sparkline_color(const cd::ui::widgets::Theme& theme) const noexcept
{
    const float avg = avg_ms();
    if (avg <= 0.0F || avg < kThresholdGreen)
    {
        return { theme.accent_success.r,
                 theme.accent_success.g,
                 theme.accent_success.b,
                 theme.accent_success.a };
    }
    if (avg < kThresholdAmber)
    {
        return { theme.accent_warning.r,
                 theme.accent_warning.g,
                 theme.accent_warning.b,
                 theme.accent_warning.a };
    }
    return { theme.accent_error.r,
             theme.accent_error.g,
             theme.accent_error.b,
             theme.accent_error.a };
}

// ===========================================================================
// Draw
// ===========================================================================

void FpsSparkline::draw(cd::ui::renderer::DrawBatcher& batcher,
                        const cd::ui::widgets::Theme&  theme,
                        const cd::ui::widgets::Rect&   bounds) const
{
    if (bounds.w <= 0.0F || bounds.h <= 0.0F) { return; }

    // ---- 1. Background fill ------------------------------------------------
    constexpr cd::ui::renderer::Color kBg { 18U, 18U, 24U, 200U };
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h, kBg);

    if (filled_ == 0U) { return; }

    // ---- 2. Per-sample bar quads -------------------------------------------
    //
    // Column width = bounds.w / kCapacity (sub-pixel; accumulate error with
    // a running float accumulator so columns tile without gaps or overlap).
    const float  bar_w    = bounds.w / static_cast<float>(kCapacity);
    const float  ceiling  = window_max_ms();
    const float  usable_h = bounds.h - kPad;  // leave kPad at top for the separator
    const cd::ui::renderer::Color bar_color = sparkline_color(theme);

    for (std::size_t slot = 0U; slot < filled_; ++slot)
    {
        // oldest-first: the oldest sample is at write_idx_ in the ring (when
        // filled_ == kCapacity) or at slot 0 (when filling up).
        const std::size_t ring_idx =
            (write_idx_ + slot + (kCapacity - filled_)) % kCapacity;
        const float ms = buf_[ring_idx];

        // Height proportional to ms / ceiling, clamped to [1, usable_h].
        const float norm   = std::clamp(ms / ceiling, 0.0F, 1.0F);
        const float bar_h  = std::max(1.0F, norm * usable_h);

        const float col_x = bounds.x + static_cast<float>(slot) * bar_w;
        const float col_y = bounds.y + bounds.h - bar_h;  // bottom-anchored

        batcher.quad(col_x, col_y, bar_w, bar_h, bar_color);
    }

    // ---- 3. Top separator (1 px, divider colour) ---------------------------
    const cd::ui::renderer::Color sep {
        theme.divider.r,
        theme.divider.g,
        theme.divider.b,
        theme.divider.a };
    batcher.quad(bounds.x, bounds.y, bounds.w, 1.0F, sep);
}

}  // namespace cd::editor::status_bar
