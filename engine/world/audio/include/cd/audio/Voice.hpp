// =============================================================================
// CHROMODYNAMIC — cd/audio/Voice.hpp
// Phase 94.A / Wave 262 — playback voice state machine.
//
// A `Voice` is one active sound instance: pointer to its sample
// buffer, current read position, gain, pitch, loop flag, and
// playback state. The mixer iterates active voices, mixes samples,
// and advances state.
//
// State machine:
//   kIdle → kPlaying via `play()`.
//   kPlaying → kPaused via `pause()`.
//   kPaused → kPlaying via `resume()`.
//   kPlaying / kPaused → kFinished when read_pos passes the end (and
//   loop=false).
//   Any state → kIdle via `stop()`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cd::audio
{

enum class VoiceState : std::uint8_t
{
    kIdle      = 0,
    kPlaying   = 1,
    kPaused    = 2,
    kFinished  = 3,
};

class Voice
{
public:
    void play(std::size_t sample_count, bool loop = false) noexcept
    {
        sample_count_ = sample_count;
        read_pos_ = 0.0F;
        loop_ = loop;
        state_ = VoiceState::kPlaying;
    }

    void pause() noexcept
    {
        if (state_ == VoiceState::kPlaying) state_ = VoiceState::kPaused;
    }

    void resume() noexcept
    {
        if (state_ == VoiceState::kPaused) state_ = VoiceState::kPlaying;
    }

    void stop() noexcept
    {
        state_ = VoiceState::kIdle;
        read_pos_ = 0.0F;
    }

    /// Advance read position by `step` samples; transition to
    /// kFinished or wrap on loop when end is reached.
    void advance(float step) noexcept
    {
        if (state_ != VoiceState::kPlaying) return;
        read_pos_ += step;
        if (read_pos_ >= static_cast<float>(sample_count_))
        {
            if (loop_ && sample_count_ > 0)
            {
                // Preserve fractional overshoot so looped playback does not
                // lose sub-sample timing at the wrap point.
                const auto len = static_cast<float>(sample_count_);
                read_pos_ -= len * std::floor(read_pos_ / len);
            }
            else { state_ = VoiceState::kFinished; }
        }
    }

    void set_gain(float g) noexcept  { gain_  = g; }
    void set_pitch(float p) noexcept { pitch_ = p; }

    [[nodiscard]] VoiceState state()       const noexcept { return state_; }
    [[nodiscard]] float      gain()        const noexcept { return gain_; }
    [[nodiscard]] float      pitch()       const noexcept { return pitch_; }
    [[nodiscard]] float      read_pos()    const noexcept { return read_pos_; }
    [[nodiscard]] bool       is_active()   const noexcept
    {
        return state_ == VoiceState::kPlaying || state_ == VoiceState::kPaused;
    }

private:
    std::size_t sample_count_ { 0 };
    float       read_pos_     { 0.0F };
    float       gain_         { 1.0F };
    float       pitch_        { 1.0F };
    bool        loop_         { false };
    VoiceState  state_        { VoiceState::kIdle };
};

}  // namespace cd::audio
