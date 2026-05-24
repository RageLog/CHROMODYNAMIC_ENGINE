// =============================================================================
// CHROMODYNAMIC — cd/input/GamepadState.hpp
// Phase 58.A / Wave 226 — Xbox-shaped gamepad snapshot + deadzone helper.
//
// Platform layer fills this struct per-tick:
//   * `left_stick_x/y` ∈ [-1, 1] — raw analog reading.
//   * `right_stick_x/y` ∈ [-1, 1].
//   * `left_trigger / right_trigger` ∈ [0, 1].
//   * `buttons` — bitmask of `GamepadButton` enum values.
//
// `apply_deadzone(stick_x, stick_y, deadzone)` is the standard radial
// deadzone: if |(x, y)| < deadzone → output (0, 0); else scale so
// `|deadzone| = 0` maps to `0` and `|1| = 1` maps to `1` (linear in
// magnitude). Without this, sticks drift "off-center" jitter is felt
// by every player.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>
#include <cstdint>

namespace cd::input
{

enum class GamepadButton : std::uint32_t
{
    kA          = 1u << 0,
    kB          = 1u << 1,
    kX          = 1u << 2,
    kY          = 1u << 3,
    kLB         = 1u << 4,
    kRB         = 1u << 5,
    kStart      = 1u << 6,
    kBack       = 1u << 7,
    kLStickDown = 1u << 8,
    kRStickDown = 1u << 9,
    kDpadUp     = 1u << 10,
    kDpadDown   = 1u << 11,
    kDpadLeft   = 1u << 12,
    kDpadRight  = 1u << 13,
};

struct GamepadState
{
    float          left_stick_x   { 0.0F };
    float          left_stick_y   { 0.0F };
    float          right_stick_x  { 0.0F };
    float          right_stick_y  { 0.0F };
    float          left_trigger   { 0.0F };
    float          right_trigger  { 0.0F };
    std::uint32_t  buttons        { 0 };
    bool           connected      { false };
};

[[nodiscard]] inline bool is_button_down(const GamepadState& s, GamepadButton b) noexcept
{
    return (s.buttons & static_cast<std::uint32_t>(b)) != 0;
}

/// Returns the deadzone-corrected `(x, y)`. Both 0 if magnitude is
/// inside the deadzone; otherwise re-scaled so the deadzone edge maps
/// to (0, 0) magnitude and unit-circle to unit-circle.
inline void apply_deadzone(float& x, float& y, float deadzone = 0.15F) noexcept
{
    if (deadzone <= 0.0F) return;
    const float mag = std::sqrt(x * x + y * y);
    if (mag < deadzone)
    {
        x = 0.0F;
        y = 0.0F;
        return;
    }
    const float t = (mag - deadzone) / (1.0F - deadzone);
    const float scale = t / mag;
    x *= scale;
    y *= scale;
}

}  // namespace cd::input
