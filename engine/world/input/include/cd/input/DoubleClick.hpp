// =============================================================================
// CHROMODYNAMIC — cd/input/DoubleClick.hpp
// Phase 30.B / Wave 199 — double-click / N-click detector.
//
// Tracks consecutive press events to detect double-clicks, triple-clicks,
// or arbitrary N-clicks within a configurable time window.
//
// Time is supplied by the caller (seconds, monotonically increasing —
// typically the editor's frame time). This avoids coupling to a
// platform clock and keeps the detector trivially testable with
// deterministic time inputs.
//
// Semantics:
//   * `click(t)` registers a press at time `t`. Returns the current
//     consecutive-click count after this press.
//   * If the gap since the previous click exceeds `threshold_sec` (set
//     via `set_threshold`), the counter resets to 1.
//   * `reset()` zeroes the counter (e.g., when input focus is lost).
//
// Typical pattern for double-click on a UI widget:
//   if (was_pressed && dc.click(t) == 2) { open_inspector(); }
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::input
{

class DoubleClick
{
public:
    void set_threshold(float seconds) noexcept
    {
        if (seconds < 0.0F) seconds = 0.0F;
        threshold_ = seconds;
    }

    [[nodiscard]] float threshold() const noexcept { return threshold_; }

    [[nodiscard]] std::uint32_t count() const noexcept { return count_; }

    /// Register a press at time `t` (seconds, monotonic). Returns the
    /// new consecutive-click count (>= 1).
    std::uint32_t click(float t) noexcept
    {
        if (count_ > 0 && (t - last_t_) <= threshold_)
            ++count_;
        else
            count_ = 1;
        last_t_ = t;
        return count_;
    }

    void reset() noexcept
    {
        count_ = 0;
        last_t_ = 0.0F;
    }

private:
    float         threshold_ { 0.4F };
    float         last_t_ { 0.0F };
    std::uint32_t count_ { 0 };
};

}  // namespace cd::input
