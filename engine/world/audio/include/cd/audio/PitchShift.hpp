// =============================================================================
// CHROMODYNAMIC — cd/audio/PitchShift.hpp
// Phase 89.B / Wave 257 — resample-based pitch shift DSP node.
//
// Simplest possible pitch shifter: change playback speed of an
// underlying sample buffer. semitones positive → faster (higher
// pitch); negative → slower (lower).
//
// Caller provides the original sample buffer; `process()` returns
// the next sample at the current `read_pos_`, then advances by
// `2^(semitones / 12)` samples. Linear interpolation between
// neighboring samples handles fractional reads.
//
// Real-time-quality pitch (preserving duration, e.g. phase vocoder)
// is out of scope — this is the SFX-pitch knob.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cmath>
#include <cstddef>
#include <span>

namespace cd::audio
{

class PitchShift
{
public:
    void prepare(std::span<const float> source, float semitones = 0.0F) noexcept
    {
        source_ = source;
        read_pos_ = 0.0F;
        step_ = std::pow(2.0F, semitones / 12.0F);
    }

    void set_semitones(float semitones) noexcept
    {
        step_ = std::pow(2.0F, semitones / 12.0F);
    }

    [[nodiscard]] float process() noexcept
    {
        if (source_.empty()) return 0.0F;
        const std::size_t i0 = static_cast<std::size_t>(read_pos_);
        if (i0 >= source_.size())
        {
            read_pos_ = 0.0F;   // wrap loop
            return source_[0];
        }
        const std::size_t i1 = (i0 + 1 < source_.size()) ? (i0 + 1) : i0;
        const float frac = read_pos_ - static_cast<float>(i0);
        const float y = source_[i0] + (source_[i1] - source_[i0]) * frac;
        read_pos_ += step_;
        return y;
    }

    void reset() noexcept { read_pos_ = 0.0F; }

    [[nodiscard]] float step() const noexcept { return step_; }
    [[nodiscard]] float position() const noexcept { return read_pos_; }

private:
    std::span<const float> source_;
    float                  read_pos_ { 0.0F };
    float                  step_     { 1.0F };
};

}  // namespace cd::audio
