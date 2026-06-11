// =============================================================================
// CHROMODYNAMIC — cd/ui/Tooltip.hpp
// Phase 48.A / Wave 216 — hover-delay tooltip state machine.
//
// Tooltip UX:
//   * User must hover the same target for `delay` seconds before the
//     tooltip appears.
//   * Moving onto a *different* target restarts the timer.
//   * Moving off all targets (target = 0) clears the tooltip immediately.
//
// State stored per Tooltip instance:
//   * current_target — 32-bit ID (caller-defined; typically widget hash).
//   * hover_started  — when the hover began (caller-supplied monotonic time).
//   * visible        — true once delay elapses.
//
// Use as:
//   tooltip.update(widget_id, time_now);
//   if (tooltip.visible()) draw_tooltip_for(widget_id);
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>

namespace cd::ui
{

class Tooltip
{
public:
    void set_delay(float seconds) noexcept
    {
        delay_ = std::max(seconds, 0.0F);
    }

    [[nodiscard]] float delay() const noexcept { return delay_; }

    void update(std::uint32_t target_id, float time_now) noexcept
    {
        if (target_id == 0)
        {
            current_target_ = 0;
            visible_ = false;
            return;
        }
        if (target_id != current_target_)
        {
            current_target_ = target_id;
            hover_started_ = time_now;
            visible_ = false;
            return;
        }
        if (!visible_ && (time_now - hover_started_) >= delay_)
            visible_ = true;
    }

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] std::uint32_t target() const noexcept { return current_target_; }

    void reset() noexcept
    {
        current_target_ = 0;
        hover_started_ = 0.0F;
        visible_ = false;
    }

private:
    float         delay_ { 0.5F };       // VS Code / IDE default
    std::uint32_t current_target_ { 0 };
    float         hover_started_ { 0.0F };
    bool          visible_ { false };
};

}  // namespace cd::ui
