// =============================================================================
// CHROMODYNAMIC — cd/input/Cursor.hpp
// Phase 98.A / Wave 266 — cursor mode enum + state.
//
// `CursorMode` describes how the platform layer should expose mouse
// movement to the engine:
//
//   * kNormal  — OS cursor visible, absolute position events.
//   * kHidden  — cursor invisible but still absolute (UI hover, no draw).
//   * kLocked  — cursor centered, only relative delta events
//                (FPS / orbit camera mode).
//
// `CursorState` is the engine-side cache: which mode + last absolute
// position + last delta. Editor sets mode via `request(mode)`; the
// platform window applies it on the next event pump.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::input
{

enum class CursorMode : std::uint8_t
{
    kNormal  = 0,
    kHidden  = 1,
    kLocked  = 2,
};

class CursorState
{
public:
    void request(CursorMode mode) noexcept { requested_ = mode; }

    [[nodiscard]] CursorMode requested() const noexcept { return requested_; }
    [[nodiscard]] CursorMode applied()   const noexcept { return applied_; }

    /// Platform layer calls this when the OS state actually changes.
    void mark_applied(CursorMode mode) noexcept { applied_ = mode; }

    void set_absolute(float x, float y) noexcept
    {
        last_dx_ = x - last_x_;
        last_dy_ = y - last_y_;
        last_x_ = x;
        last_y_ = y;
    }

    [[nodiscard]] float x()       const noexcept { return last_x_; }
    [[nodiscard]] float y()       const noexcept { return last_y_; }
    [[nodiscard]] float delta_x() const noexcept { return last_dx_; }
    [[nodiscard]] float delta_y() const noexcept { return last_dy_; }

    void clear_delta() noexcept
    {
        last_dx_ = 0.0F;
        last_dy_ = 0.0F;
    }

private:
    CursorMode requested_ { CursorMode::kNormal };
    CursorMode applied_   { CursorMode::kNormal };
    float      last_x_    { 0.0F };
    float      last_y_    { 0.0F };
    float      last_dx_   { 0.0F };
    float      last_dy_   { 0.0F };
};

}  // namespace cd::input
