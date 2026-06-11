// =============================================================================
// CHROMODYNAMIC — cd/audio/LowPass.hpp
// Phase 65.A / Wave 233 — one-pole low-pass DSP filter.
//
// Classic exponential moving average:
//
//   y[n] = a * x[n] + (1 - a) * y[n-1]
//
// where `a = dt / (RC + dt)` and `RC = 1 / (2π fc)`. The cutoff
// frequency `fc` (Hz) is the -3dB point. Compared to the existing
// SmoothingFilter (Phase 24, time-constant API), LowPass is the
// DSP-oriented sibling that takes a cutoff frequency and a sample
// rate — the natural axes for an audio engineer.
//
// Mono single-channel; caller iterates over channels.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>
#include <numbers>

namespace cd::audio
{

class LowPass
{
public:
    void prepare(float sample_rate_hz, float cutoff_hz) noexcept
    {
        const float rate = (sample_rate_hz > 0.0F) ? sample_rate_hz : 48000.0F;
        const float fc   = (cutoff_hz > 0.0F)      ? cutoff_hz      : 1000.0F;
        const float dt = 1.0F / rate;
        const float rc = 1.0F / (2.0F * std::numbers::pi_v<float> * fc);
        a_ = dt / (rc + dt);
        y_ = 0.0F;
    }

    [[nodiscard]] float process(float x) noexcept
    {
        y_ = a_ * x + (1.0F - a_) * y_;
        return y_;
    }

    void reset() noexcept { y_ = 0.0F; }

    [[nodiscard]] float alpha() const noexcept { return a_; }

private:
    float a_ { 0.0F };
    float y_ { 0.0F };
};

}  // namespace cd::audio
