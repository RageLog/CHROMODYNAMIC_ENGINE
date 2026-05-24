// =============================================================================
// CHROMODYNAMIC — cd/audio/Compressor.hpp
// Phase 75.A / Wave 243 — dynamic range compressor (feed-forward).
//
// Detects RMS envelope of input above `threshold`, applies `1/ratio`
// gain reduction with `attack` / `release` smoothing on the gain
// coefficient.
//
// Use case: dialogue bus that has occasional spikes; compressor
// pulls peaks down while preserving low-level detail. Unlike Limiter
// (Phase 41, hard ceiling), compressor is a soft musical tool.
//
// API:
//   * `prepare(rate, threshold=0.5, ratio=4, attack=5ms, release=100ms)`.
//   * `process(x)` per-sample.
//   * `gain_db()` — last applied gain in dB (negative = compression).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>

namespace cd::audio
{

class Compressor
{
public:
    void prepare(float sample_rate_hz,
                 float threshold = 0.5F,
                 float ratio     = 4.0F,
                 float attack_sec  = 0.005F,
                 float release_sec = 0.100F) noexcept
    {
        const float rate = (sample_rate_hz > 0.0F) ? sample_rate_hz : 48000.0F;
        threshold_ = (threshold > 0.0F) ? threshold : 0.5F;
        inv_ratio_ = (ratio > 1.0F) ? (1.0F / ratio) : 0.5F;
        attack_a_  = std::exp(-1.0F / (rate * (attack_sec  > 0.0F ? attack_sec  : 0.005F)));
        release_a_ = std::exp(-1.0F / (rate * (release_sec > 0.0F ? release_sec : 0.100F)));
        env_  = 0.0F;
        gain_ = 1.0F;
    }

    [[nodiscard]] float process(float x) noexcept
    {
        const float ax = std::fabs(x);
        const float coef = (ax > env_) ? attack_a_ : release_a_;
        env_ = ax + coef * (env_ - ax);
        float target = 1.0F;
        if (env_ > threshold_)
        {
            const float over = env_ / threshold_;
            const float compressed = std::pow(over, inv_ratio_);
            target = compressed * threshold_ / env_;
        }
        gain_ = target + 0.5F * (gain_ - target);
        return x * gain_;
    }

    [[nodiscard]] float gain_db() const noexcept
    {
        // Clamp before log10 so a stalled signal can't drive the dB
        // readout to -inf. `std::max` would need <algorithm> just for
        // one scalar compare — inline the branch instead.
        const float g = (gain_ > 1e-6F) ? gain_ : 1e-6F;
        return 20.0F * std::log10(g);
    }

    void reset() noexcept { env_ = 0.0F; gain_ = 1.0F; }

private:
    float threshold_ { 0.5F };
    float inv_ratio_ { 0.25F };
    float attack_a_  { 0.0F };
    float release_a_ { 0.0F };
    float env_       { 0.0F };
    float gain_      { 1.0F };
};

}  // namespace cd::audio
