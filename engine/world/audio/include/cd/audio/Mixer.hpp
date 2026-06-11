// =============================================================================
// CHROMODYNAMIC — cd/audio/Mixer.hpp
// Phase 54.A / Wave 222 — N-channel mono mix bus with per-channel gain.
//
// `Mixer` accumulates input samples from N named channels into a
// single mono output. Each channel has its own gain (linear, not dB —
// caller converts if needed). `mix(channel, x)` writes `x * gain` into
// the bus accumulator; `pull()` returns the accumulated value and
// clears for the next tick.
//
// Channels are addressed by index (0..N-1); typical pattern is to
// pin "music = 0, sfx = 1, voice = 2" at startup.
//
// Bus output can be passed through a Limiter (Phase 41) for safety
// before the actual audio backend write.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace cd::audio
{

template <std::size_t kChannels = 8>
class Mixer
{
public:
    Mixer() noexcept
    {
        for (auto& g : gains_) g = 1.0F;
    }

    void set_gain(std::size_t channel, float gain) noexcept
    {
        if (channel >= kChannels) return;
        gain = std::max(gain, 0.0F);
        gains_[channel] = gain;
    }

    [[nodiscard]] float gain(std::size_t channel) const noexcept
    {
        return (channel < kChannels) ? gains_[channel] : 0.0F;
    }

    /// Accumulate one sample of channel `channel` into the bus.
    void mix(std::size_t channel, float x) noexcept
    {
        if (channel >= kChannels) return;
        acc_ += x * gains_[channel];
    }

    /// Return the mixed sample and reset the accumulator.
    [[nodiscard]] float pull() noexcept
    {
        const float r = acc_;
        acc_ = 0.0F;
        return r;
    }

    [[nodiscard]] static constexpr std::size_t channel_count() noexcept { return kChannels; }

private:
    std::array<float, kChannels> gains_ {};
    float                        acc_ { 0.0F };
};

}  // namespace cd::audio
