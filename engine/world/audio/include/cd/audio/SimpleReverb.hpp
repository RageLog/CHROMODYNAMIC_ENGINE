// =============================================================================
// CHROMODYNAMIC — cd/audio/SimpleReverb.hpp
// Phase 29.D / Wave 198 — minimal feedback-delay reverb DSP node.
//
// A header-only single-tap feedback reverb suitable for low-cost
// ambience and stand-in reverb tails on the editor preview path. The
// production reverb (Phase 30+) will be a Schroeder/FDN with multiple
// comb + allpass stages; this is the simplest stable version of that
// topology so call sites can compile against the same API.
//
//   y[n] = x[n] + feedback * y[n - delay_samples]
//
// Bounded by `|feedback| < 1.0` (we clamp on set). Internal ring buffer
// is heap-allocated once in `prepare()`; `process()` is O(1) per sample
// and lock-free. This node is mono in / mono out; stereo reverb sits
// on top of two independent instances with optional cross-feed (not
// in scope for this primitive).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <vector>

namespace cd::audio
{

class SimpleReverb
{
public:
    /// Allocate the delay line for `delay_samples` taps. Subsequent calls
    /// reset internal state and reallocate.
    void prepare(std::size_t delay_samples)
    {
        buffer_.assign(delay_samples == 0 ? 1u : delay_samples, 0.0F);
        write_ = 0;
    }

    void set_feedback(float fb) noexcept
    {
        if (fb < -0.99F) fb = -0.99F;
        if (fb >  0.99F) fb =  0.99F;
        feedback_ = fb;
    }

    [[nodiscard]] float feedback() const noexcept { return feedback_; }

    [[nodiscard]] std::size_t delay_samples() const noexcept { return buffer_.size(); }

    /// Single-sample tick. Reads the tap at the current write position
    /// (= the oldest sample, exactly `delay_samples` ago), mixes the
    /// dry input with `feedback * delayed`, writes that mix back into
    /// the buffer, and advances the write head.
    [[nodiscard]] float process(float x) noexcept
    {
        const float delayed = buffer_[write_];
        const float y = x + feedback_ * delayed;
        buffer_[write_] = y;
        ++write_;
        if (write_ >= buffer_.size()) write_ = 0;
        return y;
    }

    void reset() noexcept
    {
        for (auto& s : buffer_) s = 0.0F;
        write_ = 0;
    }

private:
    std::vector<float> buffer_;
    std::size_t        write_ { 0 };
    float              feedback_ { 0.5F };
};

}  // namespace cd::audio
