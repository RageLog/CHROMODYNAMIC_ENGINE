// =============================================================================
// CHROMODYNAMIC — cd/math/SmoothingFilter.hpp
// Phase 24.B / Wave 190 — header-only EMA + linear smoother.
//
// Both filters take a per-tick target and return a smoothed
// approximation. EMA = exponential moving average (alpha-blend);
// Linear = approach target at fixed velocity (max-step per tick).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>

namespace cd::math
{

class EmaSmoother
{
public:
    explicit EmaSmoother(float alpha = 0.2F, float initial = 0.0F) noexcept
        : alpha_ { alpha }, value_ { initial }
    {
    }

    /// Push a new sample; returns the smoothed value.
    float update(float target) noexcept
    {
        value_ = value_ + alpha_ * (target - value_);
        return value_;
    }

    [[nodiscard]] float value() const noexcept { return value_; }
    void reset(float v = 0.0F) noexcept { value_ = v; }
    void set_alpha(float a) noexcept { alpha_ = a; }

private:
    float alpha_ { 0.2F };
    float value_ { 0.0F };
};

class LinearSmoother
{
public:
    explicit LinearSmoother(float max_step_per_tick = 1.0F, float initial = 0.0F) noexcept
        : max_step_ { max_step_per_tick }, value_ { initial }
    {
    }

    float update(float target) noexcept
    {
        const float diff = target - value_;
        if (std::abs(diff) <= max_step_)
            value_ = target;
        else
            value_ += (diff > 0.0F ? max_step_ : -max_step_);
        return value_;
    }

    [[nodiscard]] float value() const noexcept { return value_; }
    void reset(float v = 0.0F) noexcept { value_ = v; }
    void set_max_step(float s) noexcept { max_step_ = s; }

private:
    float max_step_ { 1.0F };
    float value_ { 0.0F };
};

}  // namespace cd::math
