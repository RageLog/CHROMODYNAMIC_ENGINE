// =============================================================================
// CHROMODYNAMIC — cd/profile/frame_graph_timeline/FrameGraphTimeline.cpp
// =============================================================================
#include <cd/profile/frame_graph_timeline/FrameGraphTimeline.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <ranges>

namespace cd::profile::frame_graph_timeline
{

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

void Timeline::begin_frame()
{
    current_passes_.clear();
}

void Timeline::record_pass(std::string_view pass_name,
                           double           gpu_start_ms,
                           double           gpu_duration_ms,
                           std::uint32_t    node_id)
{
    current_passes_.emplace_back(
        std::string(pass_name),
        gpu_start_ms,
        gpu_duration_ms,
        node_id
    );
}

void Timeline::end_frame()
{
    if (current_passes_.empty())
    {
        last_total_ms_ = 0.0;
        last_min_ms_   = 0.0;
        last_max_ms_   = 0.0;
        last_passes_   = std::move(current_passes_);
        current_passes_.clear();
        return;
    }

    // Compute total, min, and max GPU durations before swapping.
    const auto proj = [](const PassRecord& pr) noexcept { return pr.gpu_duration_ms; };
    last_total_ms_ = std::accumulate(
        current_passes_.begin(), current_passes_.end(), 0.0,
        [](double acc, const PassRecord& pr) { return acc + pr.gpu_duration_ms; });
    last_min_ms_ = proj(*std::ranges::min_element(current_passes_, {}, proj));
    last_max_ms_ = proj(*std::ranges::max_element(current_passes_, {}, proj));

    // Swap: current becomes last, clearing current for the next frame.
    last_passes_ = std::move(current_passes_);
    current_passes_.clear();
}

std::span<const PassRecord> Timeline::last_frame_passes() const noexcept
{
    return {last_passes_.data(), last_passes_.size()};
}

double Timeline::last_frame_total_ms() const noexcept
{
    return last_total_ms_;
}

double Timeline::last_frame_min_pass_ms() const noexcept
{
    return last_min_ms_;
}

double Timeline::last_frame_max_pass_ms() const noexcept
{
    return last_max_ms_;
}

void Timeline::reserve(std::size_t n)
{
    current_passes_.reserve(n);
    last_passes_.reserve(n);
}

void Timeline::reset() noexcept
{
    current_passes_.clear();
    last_passes_.clear();
    last_total_ms_ = 0.0;
    last_min_ms_   = 0.0;
    last_max_ms_   = 0.0;
}

// ---------------------------------------------------------------------------
// TimelineOverlay
// ---------------------------------------------------------------------------

TimelineOverlay::TimelineOverlay(double window_ms)
    : window_ms_(window_ms)
{
}

namespace
{

/// djb2 hash of a pass name → stable pastel colour for the Gantt bar.
[[nodiscard]] cd::ui::renderer::Color pass_colour(std::string_view name) noexcept
{
    std::uint32_t h = 5381U;
    for (const char c : name)
        h = ((h << 5U) + h) + static_cast<std::uint32_t>(static_cast<unsigned char>(c));

    const auto r = static_cast<std::uint8_t>((h         & 0xFFu));
    const auto g = static_cast<std::uint8_t>(((h >>  8U) & 0xFFu));
    const auto b = static_cast<std::uint8_t>(((h >> 16U) & 0xFFu));
    return cd::ui::renderer::Color{ r, g, b, 210U };
}

}  // namespace

void TimelineOverlay::draw(cd::ui::renderer::DrawBatcher&  batcher,
                           std::span<const PassRecord>      passes,
                           Rect                             bounds) const
{
    if (passes.empty() || bounds.width <= 0.0F || bounds.height <= 0.0F)
        return;

    // Determine the earliest start time in the pass list.
    const auto start_proj = [](const PassRecord& p) noexcept { return p.gpu_start_ms; };
    const double min_start = start_proj(*std::ranges::min_element(passes, {}, start_proj));

    const double time_range   = (window_ms_ > 0.0) ? window_ms_ : 1.0;
    const float  pixels_per_ms = bounds.width / static_cast<float>(time_range);

    // Single horizontal lane for all GPU passes (GPU work is serialised within
    // a command list; passes share one timeline row).
    const float lane_height = bounds.height;
    const float bar_h       = lane_height * 0.75F;  // 75% height, 25% top padding.
    const float bar_y       = bounds.y + (lane_height - bar_h) * 0.5F;

    for (const auto& p : passes)
    {
        const double rel_start = p.gpu_start_ms - min_start;

        // Cull passes entirely outside the visible window.
        if (rel_start + p.gpu_duration_ms < 0.0)
            continue;
        if (rel_start > time_range)
            continue;

        const float bar_x = bounds.x + static_cast<float>(rel_start) * pixels_per_ms;
        // Minimum 1 pixel wide so zero-duration passes are visible.
        const float bar_w = std::max(1.0F, static_cast<float>(p.gpu_duration_ms) * pixels_per_ms);

        batcher.quad(bar_x, bar_y, bar_w, bar_h, pass_colour(p.pass_name));
    }
}

}  // namespace cd::profile::frame_graph_timeline
