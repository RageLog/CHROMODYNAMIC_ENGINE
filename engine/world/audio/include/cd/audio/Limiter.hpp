// =============================================================================
// CHROMODYNAMIC — cd/audio/Limiter.hpp
// Phase 40.A / Wave 208 — soft-knee peak limiter.
//
// Single-channel feed-forward peak limiter. Maintains an instantaneous
// envelope that follows |x| with separate attack / release time
// constants:
//
//   env[n] = max(|x[n]|, env[n-1] * release_coef)   (release path)
//   env[n] = max(|x[n]|, env[n-1] * attack_coef)    (attack path)
//
// Where coef = exp(-1 / (rate * tau)). When env exceeds threshold T,
// gain = T / env clamps the sample. Soft knee is achieved by smoothing
// the gain via a one-pole low-pass instead of hard divide.
//
// Use as a safety net on the master bus to prevent clipping when many
// reverb tails / events stack. Not a mastering limiter — quality of
// modern broadcast limiters (LUFS-aware) is out of scope.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>

namespace cd::audio
{

class Limiter
{
public:
    void prepare(float sample_rate_hz, float threshold = 0.95F,
                 float attack_sec = 0.001F, float release_sec = 0.050F) noexcept
    {
        rate_      = sample_rate_hz > 0.0F ? sample_rate_hz : 48000.0F;
        threshold_ = (threshold > 0.0F) ? threshold : 1.0F;
        attack_a_  = std::exp(-1.0F / (rate_ * (attack_sec > 0.0F ? attack_sec : 0.001F)));
        release_a_ = std::exp(-1.0F / (rate_ * (release_sec > 0.0F ? release_sec : 0.050F)));
        env_       = 0.0F;
        gain_      = 1.0F;
    }

    [[nodiscard]] float process(float x) noexcept
    {
        const float ax = std::fabs(x);
        const float coef = (ax > env_) ? attack_a_ : release_a_;
        env_ = ax + coef * (env_ - ax);
        const float target = (env_ > threshold_) ? (threshold_ / env_) : 1.0F;
        // One-pole smoothing on the gain so the knee is soft.
        gain_ = target + 0.5F * (gain_ - target);
        return x * gain_;
    }

    [[nodiscard]] float current_gain() const noexcept { return gain_; }
    [[nodiscard]] float envelope() const noexcept { return env_; }

    void reset() noexcept { env_ = 0.0F; gain_ = 1.0F; }

private:
    float rate_ { 48000.0F };
    float threshold_ { 0.95F };
    float attack_a_ { 0.0F };
    float release_a_ { 0.0F };
    float env_ { 0.0F };
    float gain_ { 1.0F };
};

}  // namespace cd::audio
