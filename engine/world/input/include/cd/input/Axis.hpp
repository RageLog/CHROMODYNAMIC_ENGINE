// =============================================================================
// CHROMODYNAMIC — cd/input/Axis.hpp
// Phase 22.B / Wave 186 — multi-source normalized input axis.
//
// An `Axis` combines a "positive" + "negative" key into a single
// scalar [-1, 1]:
//   * neg held → -1.0
//   * pos held → +1.0
//   * both → 0.0
//   * neither → 0.0
//
// `set_keys(neg_pressed, pos_pressed)` is the per-frame setter that
// the editor / gameplay loop calls; ImGui::IsKeyDown(...) is one
// reasonable source.
//
// Future: stub for a gamepad analog value override; smoothing
// (lerp toward target) for camera-relative axes. Header-only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

namespace cd::input
{

class Axis
{
public:
    /// Update the axis state from "negative key held" + "positive
    /// key held" booleans. Returns the new value in [-1, 1].
    float set_keys(bool neg, bool pos) noexcept
    {
        if (neg && !pos) value_ = -1.0F;
        else if (pos && !neg) value_ = 1.0F;
        else value_ = 0.0F;
        return value_;
    }

    /// Direct analog override (gamepad). Clamped to [-1, 1].
    void set_analog(float v) noexcept
    {
        if (v < -1.0F) v = -1.0F;
        else if (v > 1.0F) v = 1.0F;
        value_ = v;
    }

    [[nodiscard]] float value() const noexcept { return value_; }

private:
    float value_ { 0.0F };
};

}  // namespace cd::input
