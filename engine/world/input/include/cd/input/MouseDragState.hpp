// =============================================================================
// CHROMODYNAMIC — cd/input/MouseDragState.hpp
// Phase 79.A / Wave 247 — mouse drag state machine.
//
// Tracks a press → drag → release lifecycle around a single mouse
// button. Caller feeds events; the state machine reports:
//   * `is_pressed()` — button currently held.
//   * `is_dragging()` — held and moved past threshold.
//   * `drag_delta()` — (current - press_origin), updated every move.
//   * `press_origin()` — pixel position where button went down.
//
// Threshold (default 4 px) prevents jittery clicks from being
// classified as drags. Reset on release; ready for the next drag.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>

namespace cd::input
{

class MouseDragState
{
public:
    void set_threshold(float pixels) noexcept
    {
        if (pixels < 0.0F) pixels = 0.0F;
        threshold_ = pixels;
    }

    void on_press(float x, float y) noexcept
    {
        pressed_ = true;
        dragging_ = false;
        origin_x_ = x;
        origin_y_ = y;
        current_x_ = x;
        current_y_ = y;
    }

    void on_move(float x, float y) noexcept
    {
        if (!pressed_) return;
        current_x_ = x;
        current_y_ = y;
        if (!dragging_)
        {
            const float dx = x - origin_x_;
            const float dy = y - origin_y_;
            if (std::sqrt(dx * dx + dy * dy) >= threshold_)
                dragging_ = true;
        }
    }

    void on_release() noexcept
    {
        pressed_  = false;
        dragging_ = false;
    }

    [[nodiscard]] bool  is_pressed()  const noexcept { return pressed_; }
    [[nodiscard]] bool  is_dragging() const noexcept { return dragging_; }
    [[nodiscard]] float drag_delta_x() const noexcept { return current_x_ - origin_x_; }
    [[nodiscard]] float drag_delta_y() const noexcept { return current_y_ - origin_y_; }
    [[nodiscard]] float press_origin_x() const noexcept { return origin_x_; }
    [[nodiscard]] float press_origin_y() const noexcept { return origin_y_; }
    [[nodiscard]] float threshold() const noexcept { return threshold_; }

private:
    bool  pressed_   { false };
    bool  dragging_  { false };
    float threshold_ { 4.0F };
    float origin_x_  { 0.0F };
    float origin_y_  { 0.0F };
    float current_x_ { 0.0F };
    float current_y_ { 0.0F };
};

}  // namespace cd::input
