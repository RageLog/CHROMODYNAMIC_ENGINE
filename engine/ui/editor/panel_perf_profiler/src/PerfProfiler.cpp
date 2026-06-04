// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_perf_profiler/src/PerfProfiler.cpp
//
// phase700 — cd::editor::panel::perf_profiler::PerfProfiler implementation.
//
// Three-section layout:
//   A. History bar chart (top 40 %)
//      Each slot = one FrameSnapshot.  Bar height proportional to total_ms,
//      scaled against 2 × budget_ms_.  Over-budget = error colour (red/amber);
//      in-budget = green.  Selected frame gets a bright accent border.
//
//   B. Drill-down (middle 40 %)
//      Renders CPU markers first (one row each), then GPU passes (one row each).
//      Each row is a dark-bg quad + a proportional coloured bar whose width
//      encodes duration_ms relative to budget_ms_.
//
//   C. Stats strip (bottom 20 %)
//      Four equally-wide cells: avg / min / max / p99 of total_ms over the
//      current history.  Each cell is a coloured rect (accent-coloured border,
//      dark fill).  Actual text rendering requires a font atlas — we emit
//      the background geometry here; the label is the caller's concern.
//
// All draw calls use DrawBatcher::quad() only (no RHI, no ImGui).
// =============================================================================
#include <cd/editor/panel_perf_profiler/PerfProfiler.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/theme/Theme.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <vector>

namespace cd::editor::panel::perf_profiler
{

// ===========================================================================
// Data API
// ===========================================================================

void PerfProfiler::capture_frame(const FrameSnapshot& snap)
{
    ring_[head_] = snap;
    head_ = (head_ + 1U) % kHistoryCapacity;
    if (count_ < kHistoryCapacity)
    {
        ++count_;
    }
    // Auto-select the newest frame (index = count_ - 1, i.e. the last one).
    selected_ = count_ > 0U ? count_ - 1U : 0U;
}

std::size_t PerfProfiler::recorded_frame_count() const noexcept
{
    return count_;
}

void PerfProfiler::clear() noexcept
{
    head_      = 0U;
    count_     = 0U;
    selected_  = 0U;
    // Zero-init the ring so stale data does not bleed in.
    for (auto& s : ring_)
    {
        s = FrameSnapshot{};
    }
}

void PerfProfiler::set_target_fps(float fps) noexcept
{
    if (fps <= 0.0F) { return; }
    budget_ms_ = 1000.0F / fps;
}

void PerfProfiler::select_frame(std::size_t ring_index) noexcept
{
    if (count_ == 0U) { selected_ = 0U; return; }
    selected_ = std::min(ring_index, count_ - 1U);
}

std::size_t PerfProfiler::selected_frame_index() const noexcept
{
    return selected_;
}

// ===========================================================================
// Ring helper
// ===========================================================================

const FrameSnapshot& PerfProfiler::snapshot_at(std::size_t i) const noexcept
{
    // Ring stores oldest-first when iterated forward from
    // (head_ - count_) % kHistoryCapacity.
    const std::size_t start = (head_ + kHistoryCapacity - count_) % kHistoryCapacity;
    return ring_[(start + i) % kHistoryCapacity];
}

// ===========================================================================
// Draw — top-level
// ===========================================================================

void PerfProfiler::draw(cd::ui::renderer::DrawBatcher&  batcher,
                        const cd::ui::theme::Theme&     theme,
                        const cd::ui::widgets::Rect&    bounds) const
{
    if (bounds.w <= 0.0F || bounds.h <= 0.0F) { return; }

    // ---- Outer dark background + border ------------------------------------
    const cd::ui::renderer::Color bg_color { 14U, 14U, 18U, 240U };
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h, bg_color);

    // Accent border using the theme's primary colour.
    const auto& primary = theme.color(cd::ui::theme::PaletteSlot::kPrimary);
    const cd::ui::renderer::Color border_color {
        static_cast<std::uint8_t>(std::clamp(primary.r * 255.0F, 0.0F, 255.0F)),
        static_cast<std::uint8_t>(std::clamp(primary.g * 255.0F, 0.0F, 255.0F)),
        static_cast<std::uint8_t>(std::clamp(primary.b * 255.0F, 0.0F, 255.0F)),
        180U
    };
    // Top / bottom / left / right border quads.
    batcher.quad(bounds.x,                         bounds.y,                          bounds.w,  kBorderW,    border_color);
    batcher.quad(bounds.x,                         bounds.y + bounds.h - kBorderW,   bounds.w,  kBorderW,    border_color);
    batcher.quad(bounds.x,                         bounds.y,                          kBorderW,  bounds.h,    border_color);
    batcher.quad(bounds.x + bounds.w - kBorderW,   bounds.y,                          kBorderW,  bounds.h,    border_color);

    // ---- Compute section rects (inside the border) -------------------------
    const float inner_x = bounds.x + kBorderW;
    const float inner_y = bounds.y + kBorderW;
    const float inner_w = bounds.w - 2.0F * kBorderW;
    const float inner_h = bounds.h - 2.0F * kBorderW;

    if (inner_w <= 0.0F || inner_h <= 0.0F) { return; }

    const float history_h   = inner_h * kHistoryFraction;
    const float drill_down_h = inner_h * kDrillDownFraction;
    const float stats_h     = inner_h - history_h - drill_down_h;

    const cd::ui::widgets::Rect history_rect {
        inner_x, inner_y,
        inner_w, history_h
    };
    const cd::ui::widgets::Rect drill_rect {
        inner_x, inner_y + history_h,
        inner_w, drill_down_h
    };
    const cd::ui::widgets::Rect stats_rect {
        inner_x, inner_y + history_h + drill_down_h,
        inner_w, stats_h
    };

    // ---- Section A — history bars ------------------------------------------
    if (history_h > 0.0F)
    {
        draw_history_bars(batcher, theme, history_rect);
    }

    // ---- Section divider: thin horizontal rule between A and B -------------
    const cd::ui::renderer::Color divider { 50U, 50U, 60U, 160U };
    batcher.quad(inner_x, inner_y + history_h, inner_w, 1.0F, divider);

    // ---- Section B — drill-down --------------------------------------------
    if (drill_down_h > 0.0F)
    {
        draw_drill_down(batcher, theme, drill_rect);
    }

    // ---- Section divider: between B and C ----------------------------------
    batcher.quad(inner_x, inner_y + history_h + drill_down_h, inner_w, 1.0F, divider);

    // ---- Section C — stats strip -------------------------------------------
    if (stats_h > 0.0F)
    {
        draw_stats_strip(batcher, theme, stats_rect);
    }
}

// ===========================================================================
// Section A — 60-frame history bar chart
// ===========================================================================

void PerfProfiler::draw_history_bars(cd::ui::renderer::DrawBatcher& batcher,
                                     const cd::ui::theme::Theme&    theme,
                                     const cd::ui::widgets::Rect&   section) const
{
    // Dark section background.
    batcher.quad(section.x, section.y, section.w, section.h,
                 cd::ui::renderer::Color { 10U, 10U, 14U, 200U });

    if (count_ == 0U) { return; }

    // Budget reference line at 50 % height (= 1 × budget_ms).
    const float budget_y = section.y + section.h * 0.5F;
    batcher.quad(section.x, budget_y, section.w, 1.0F,
                 cd::ui::renderer::Color { 90U, 90U, 100U, 140U });

    // Bar dimensions: fit all kHistoryCapacity slots into section width.
    const float bar_slot_w = section.w / static_cast<float>(kHistoryCapacity);
    const float bar_w      = std::max(bar_slot_w - 1.0F, 1.0F);  // 1 px gap

    // Scale: 2 × budget_ms maps to full section height.
    const float max_ms = budget_ms_ * 2.0F;

    // Colour constants.
    const cd::ui::renderer::Color ok_color   {  60U, 190U,  80U, 210U };
    const cd::ui::renderer::Color over_color { 210U,  60U,  60U, 210U };

    // Selected-frame highlight colour (from theme primary).
    const auto& primary_tok = theme.color(cd::ui::theme::PaletteSlot::kPrimary);
    const cd::ui::renderer::Color sel_color {
        static_cast<std::uint8_t>(std::clamp(primary_tok.r * 255.0F, 0.0F, 255.0F)),
        static_cast<std::uint8_t>(std::clamp(primary_tok.g * 255.0F, 0.0F, 255.0F)),
        static_cast<std::uint8_t>(std::clamp(primary_tok.b * 255.0F, 0.0F, 255.0F)),
        255U
    };

    for (std::size_t i = 0U; i < count_; ++i)
    {
        const FrameSnapshot& snap = snapshot_at(i);
        const auto ms       = static_cast<float>(snap.total_ms);
        const float norm     = std::clamp(ms / max_ms, 0.0F, 1.0F);
        const float bar_h    = std::max(norm * section.h, 1.0F);
        const float bar_x    = section.x + static_cast<float>(i) * bar_slot_w;
        const float bar_y    = section.y + section.h - bar_h;

        const auto& color = (ms > budget_ms_) ? over_color : ok_color;
        batcher.quad(bar_x, bar_y, bar_w, bar_h, color);

        // Selected-frame: 2 px bright top cap.
        if (i == selected_)
        {
            batcher.quad(bar_x, bar_y, bar_w, 2.0F, sel_color);
            // 1 px left/right border on the selected bar.
            batcher.quad(bar_x,              bar_y, 1.0F, bar_h, sel_color);
            batcher.quad(bar_x + bar_w - 1.0F, bar_y, 1.0F, bar_h, sel_color);
        }
    }
}

// ===========================================================================
// Section B — drill-down into the selected frame
// ===========================================================================

void PerfProfiler::draw_drill_down(cd::ui::renderer::DrawBatcher& batcher,
                                   const cd::ui::theme::Theme&    theme,
                                   const cd::ui::widgets::Rect&   section) const
{
    // Dark section background.
    batcher.quad(section.x, section.y, section.w, section.h,
                 cd::ui::renderer::Color { 12U, 12U, 16U, 200U });

    if (count_ == 0U) { return; }

    const FrameSnapshot& snap = snapshot_at(selected_);

    float cursor_y = section.y + 2.0F;  // 2 px top padding

    // Colour constants.
    const cd::ui::renderer::Color cpu_bar_color { 80U, 160U, 230U, 200U };  // blue-ish
    const cd::ui::renderer::Color gpu_bar_color { 220U, 140U, 50U, 200U };  // orange
    const cd::ui::renderer::Color pass_bar_color { 90U, 200U, 130U, 200U }; // green
    const cd::ui::renderer::Color row_bg         { 22U,  22U,  28U, 180U };

    // Max visible duration: budget_ms_ (bars are capped at full width).
    const float max_ms = budget_ms_ > 0.0F ? budget_ms_ : 1.0F;

    // ---- CPU markers -------------------------------------------------------
    for (const auto& m : snap.cpu_markers)
    {
        if (cursor_y + kRowH > section.y + section.h) { break; }

        // Row background.
        batcher.quad(section.x, cursor_y, section.w, kRowH - 1.0F, row_bg);

        // Proportional bar.
        const auto dur     = static_cast<float>(m.duration_ms);
        const float norm    = std::clamp(dur / max_ms, 0.0F, 1.0F);
        const float bar_w   = std::max(norm * section.w, 1.0F);
        batcher.quad(section.x, cursor_y, bar_w, kRowH - 1.0F, cpu_bar_color);

        cursor_y += kRowH;
    }

    // ---- GPU markers -------------------------------------------------------
    for (const auto& m : snap.gpu_markers)
    {
        if (cursor_y + kRowH > section.y + section.h) { break; }

        batcher.quad(section.x, cursor_y, section.w, kRowH - 1.0F, row_bg);

        const auto dur   = static_cast<float>(m.duration_ms_computed);
        const float norm  = std::clamp(dur / max_ms, 0.0F, 1.0F);
        const float bar_w = std::max(norm * section.w, 1.0F);
        batcher.quad(section.x, cursor_y, bar_w, kRowH - 1.0F, gpu_bar_color);

        // Suppress unused variable warning — name is present for future text.
        (void)m.name;

        cursor_y += kRowH;
    }

    // ---- GPU pass records --------------------------------------------------
    for (const auto& p : snap.gpu_passes)
    {
        if (cursor_y + kRowH > section.y + section.h) { break; }

        batcher.quad(section.x, cursor_y, section.w, kRowH - 1.0F, row_bg);

        const auto dur   = static_cast<float>(p.gpu_duration_ms);
        const float norm  = std::clamp(dur / max_ms, 0.0F, 1.0F);
        const float bar_w = std::max(norm * section.w, 1.0F);
        batcher.quad(section.x, cursor_y, bar_w, kRowH - 1.0F, pass_bar_color);

        // Suppress unused variable warning — name is present for future text.
        (void)p.pass_name;

        cursor_y += kRowH;
    }

    // Suppress unused parameter warning for theme (used in future text rendering).
    (void)theme;
}

// ===========================================================================
// Section C — stats strip  (avg / min / max / p99)
// ===========================================================================

void PerfProfiler::draw_stats_strip(cd::ui::renderer::DrawBatcher& batcher,
                                    const cd::ui::theme::Theme&    theme,
                                    const cd::ui::widgets::Rect&   section) const
{
    // Dark section background.
    batcher.quad(section.x, section.y, section.w, section.h,
                 cd::ui::renderer::Color { 10U, 10U, 14U, 200U });

    if (count_ == 0U) { return; }

    // Collect total_ms values for statistics.
    std::vector<float> values;
    values.reserve(count_);
    for (std::size_t i = 0U; i < count_; ++i)
    {
        values.push_back(static_cast<float>(snapshot_at(i).total_ms));
    }

    // ---- Compute statistics ------------------------------------------------
    const float sum = std::accumulate(values.begin(), values.end(), 0.0F);
    const float avg = sum / static_cast<float>(count_);
    const float min_val = *std::ranges::min_element(values);
    const float max_val = *std::ranges::max_element(values);

    // p99 — sort a copy and pick the 99th-percentile element.
    std::vector<float> sorted = values;
    std::ranges::sort(sorted);
    const std::size_t p99_idx = std::max(
        std::size_t{0},
        static_cast<std::size_t>(static_cast<float>(sorted.size()) * 0.99F) );
    const float p99_val = sorted[std::min(p99_idx, sorted.size() - 1U)];

    // ---- Layout: four equal cells ------------------------------------------
    const float cell_w   = section.w / 4.0F;
    const float cell_h   = std::min(kStatH, section.h);
    const float cell_y   = section.y + (section.h - cell_h) * 0.5F;

    // Accent colour from theme error (red) for over-budget, primary for labels.
    const auto& err_tok = theme.color(cd::ui::theme::PaletteSlot::kError);
    const cd::ui::renderer::Color err_accent {
        static_cast<std::uint8_t>(std::clamp(err_tok.r * 255.0F, 0.0F, 255.0F)),
        static_cast<std::uint8_t>(std::clamp(err_tok.g * 255.0F, 0.0F, 255.0F)),
        static_cast<std::uint8_t>(std::clamp(err_tok.b * 255.0F, 0.0F, 255.0F)),
        200U
    };
    const cd::ui::renderer::Color ok_accent    {  60U, 190U,  80U, 200U };
    const cd::ui::renderer::Color neutral_color{ 120U, 120U, 140U, 200U };

    // Helper lambda: pick accent based on value vs. budget.
    const auto stat_color = [&](float val) -> cd::ui::renderer::Color
    {
        return (val > budget_ms_) ? err_accent : ok_accent;
    };

    const float stats[4] = { avg, min_val, max_val, p99_val };
    const cd::ui::renderer::Color cell_colors[4] = {
        stat_color(avg),
        ok_accent,          // min is always "good"
        stat_color(max_val),
        stat_color(p99_val)
    };

    for (int ci = 0; ci < 4; ++ci)
    {
        const float cx = section.x + static_cast<float>(ci) * cell_w;

        // Cell dark background.
        batcher.quad(cx, cell_y, cell_w - 1.0F, cell_h,
                     cd::ui::renderer::Color { 20U, 20U, 26U, 200U });

        // Proportional fill bar (width encodes value vs 2*budget).
        const float norm    = std::clamp(stats[ci] / (budget_ms_ * 2.0F), 0.0F, 1.0F);
        const float fill_w  = std::max(norm * (cell_w - 1.0F), 1.0F);
        batcher.quad(cx, cell_y + cell_h - 3.0F, fill_w, 3.0F, cell_colors[ci]);

        // Top border in cell accent colour.
        batcher.quad(cx, cell_y, cell_w - 1.0F, 1.0F, cell_colors[ci]);

        // Suppress unused neutral_color in this iteration.
        (void)neutral_color;
    }

    // Suppress unused parameter warning for theme (used in future text rendering).
    (void)theme;
}

}  // namespace cd::editor::panel::perf_profiler
