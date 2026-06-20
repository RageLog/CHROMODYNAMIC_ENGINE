// =============================================================================
// CHROMODYNAMIC — cd/input/Hold.hpp
// Phase 90.A / Wave 258 — long-press (hold) detector.
//
// Tracks "is button held for at least `threshold` seconds?":
//   * `on_press(t)` records press time.
//   * `on_release(t)` clears.
//   * `is_held(now)` true iff press is active AND (now - press_t) ≥ threshold.
//   * `held_duration(now)` — seconds since press, or 0 if released.
//
// Caller drives monotonic time. Used by editor "click-and-hold to
// open context menu", "hold W for sprint", "hold Esc to confirm
// dismiss" patterns.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>

namespace cd::input
{

class Hold
{
public:
    void set_threshold(float seconds) noexcept
    {
        threshold_ = std::max(seconds, 0.0F);
    }

    [[nodiscard]] float threshold() const noexcept { return threshold_; }

    void on_press(float now) noexcept
    {
        pressed_ = true;
        press_t_ = now;
    }

    void on_release() noexcept
    {
        pressed_ = false;
    }

    [[nodiscard]] bool is_pressed() const noexcept { return pressed_; }

    [[nodiscard]] bool is_held(float now) const noexcept
    {
        return pressed_ && (now - press_t_) >= threshold_;
    }

    [[nodiscard]] float held_duration(float now) const noexcept
    {
        return pressed_ ? (now - press_t_) : 0.0F;
    }

private:
    float threshold_ { 0.5F };
    float press_t_   { 0.0F };
    bool  pressed_   { false };
};

}  // namespace cd::input
