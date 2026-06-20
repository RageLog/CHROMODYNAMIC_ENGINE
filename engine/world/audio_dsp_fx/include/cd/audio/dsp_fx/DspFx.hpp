// =============================================================================
// CHROMODYNAMIC — cd/audio/dsp_fx/DspFx.hpp
// Phase 562 — cd::audio::dsp_fx Sprint-1 public API.
//
// Provides four digital signal processing primitives:
//
//   BiquadCoeffs  — Raw second-order IIR filter coefficient bundle.
//   LowPass       — Biquad low-pass filter (direct-form II transposed).
//   HighPass      — Biquad high-pass filter (same topology).
//   DelayLine     — Simple integer-sample delay with read/write interface.
//   Reverb        — Schroeder/Freeverb-style FDN: 4 parallel low-pass feedback
//                   comb filters (RT60-derived feedback, mutually-prime delays)
//                   feeding 2 series allpass diffusers, with a wet/dry mix.
//                   The comb is the Freeverb LBCF (one-pole damping in the
//                   feedback path); the allpass is the true Schroeder/Gardner
//                   allpass H(z) = (-g + z^-N)/(1 - g·z^-N) (unity magnitude at
//                   every frequency). Feedback gains are RT60-derived
//                   (g = 10^(-3·N/(RT60·Fs))) and clamped < 1 for stability.
//                   Scalar (no SIMD; SIMD is a perf promote-on-need —
//                   ADR-20260616-band3-world-scope §2.3 — the scalar path is
//                   the correctness contract a future SIMD bank must match).
//
// Filter coefficient formulae follow:
//   [unverified] Robert Bristow-Johnson, "Cookbook formulae for audio EQ
//   biquad filter coefficients," Audio EQ Cookbook (web/public domain).
//   PDF + BibTeX entry pending; research/library/MANIFEST.csv does not yet
//   contain a verified entry. Academic-researcher pass queued before this
//   citation may be treated as verified under the Demir Kural.
//
// All classes are mono, sample-by-sample or span-based. Caller iterates
// over channels for stereo/surround. Thread-safety: none — caller must
// serialise if needed.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <array>
#include <vector>

namespace cd::audio::dsp_fx
{

// =============================================================================
// flush_denormal — clamp subnormal floats to zero.
//
// The comb filter's one-pole "filterstore" is a recursive accumulator: as an
// impulse response decays past ~1e-38 the values become subnormal, which on
// many x86/ARM cores triggers a microcode-assisted slow path (the classic
// "reverb tail CPU spike"). Flushing subnormals to zero costs one compare and
// is audibly transparent (the magnitudes involved are ~150 dB below unity).
// Reference: Freeverb's `undenormalise` macro, generalised here.
// =============================================================================
[[nodiscard]] inline float flush_denormal(float x) noexcept
{
    // std::abs of a subnormal is still subnormal; FLT_MIN is the smallest
    // *normal* float, so anything strictly below it (and non-zero) is flushed.
    return (std::abs(x) < std::numeric_limits<float>::min()) ? 0.0F : x;
}

// =============================================================================
// BiquadCoeffs — second-order IIR coefficient set (direct-form II transposed)
//
// Transfer function: H(z) = (b0 + b1*z^-1 + b2*z^-2)
//                          / (1  + a1*z^-1 + a2*z^-2)
//
// Note: a0 is normalised to 1 before storage; a1, a2 are already divided
// by the raw a0 from the cookbook.
// =============================================================================
struct BiquadCoeffs
{
    float b0 { 1.0F };
    float b1 { 0.0F };
    float b2 { 0.0F };
    float a1 { 0.0F };
    float a2 { 0.0F };

    /// Returns the identity coefficients (all-pass, gain = 1).
    [[nodiscard]] static constexpr BiquadCoeffs identity() noexcept
    {
        return BiquadCoeffs{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F };
    }
};

// =============================================================================
// BiquadState — per-instance delay elements for direct-form II transposed
// =============================================================================
struct BiquadState
{
    float w1 { 0.0F };
    float w2 { 0.0F };

    void reset() noexcept { w1 = 0.0F; w2 = 0.0F; }
};

// =============================================================================
// LowPass — second-order Butterworth-style low-pass filter
//
// configure() computes biquad coefficients from cutoff + sample rate
// using the Audio EQ Cookbook LPF formulae [unverified; see file header].
// process(span) runs the filter in-place over a block of samples.
// process(float) ticks a single sample.
// =============================================================================
class LowPass
{
public:
    /// Compute LPF coefficients.
    /// @param cutoff_hz   -3dB cutoff frequency in Hz (clamped > 0).
    /// @param sample_rate Sampling rate in Hz (clamped > 0).
    void configure(float cutoff_hz, float sample_rate) noexcept
    {
        const float fs = (sample_rate > 0.0F) ? sample_rate : 48000.0F;
        const float fc = (cutoff_hz   > 0.0F) ? cutoff_hz   : 1000.0F;

        // Cookbook LPF: w0 = 2*pi*f0/Fs, alpha = sin(w0)/(2*Q), Q=0.7071
        constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
        constexpr float kQ     = std::numbers::sqrt2_v<float> / 2.0F;  // 1/sqrt(2)
        const float w0    = kTwoPi * fc / fs;
        const float cosw0 = std::cos(w0);
        const float sinw0 = std::sin(w0);
        const float alpha = sinw0 / (2.0F * kQ);

        const float a0_inv = 1.0F / (1.0F + alpha);
        coeffs_.b0 = ((1.0F - cosw0) / 2.0F) * a0_inv;
        coeffs_.b1 = (1.0F  - cosw0)          * a0_inv;
        coeffs_.b2 = coeffs_.b0;
        coeffs_.a1 = (-2.0F * cosw0)           * a0_inv;
        coeffs_.a2 = (1.0F  - alpha)            * a0_inv;

        state_.reset();
    }

    /// Process a block of samples in-place.
    void process(std::span<float> samples) noexcept
    {
        for (float& s : samples)
            s = tick(s);
    }

    /// Process a single sample.
    [[nodiscard]] float process(float x) noexcept { return tick(x); }

    /// Read the current coefficients (for testing / inspection).
    [[nodiscard]] const BiquadCoeffs& coeffs() const noexcept { return coeffs_; }

    void reset() noexcept { state_.reset(); }

private:
    BiquadCoeffs coeffs_ { BiquadCoeffs::identity() };
    BiquadState  state_  {};

    [[nodiscard]] float tick(float x) noexcept
    {
        const float y  = coeffs_.b0 * x + state_.w1;
        state_.w1      = coeffs_.b1 * x - coeffs_.a1 * y + state_.w2;
        state_.w2      = coeffs_.b2 * x - coeffs_.a2 * y;
        return y;
    }
};

// =============================================================================
// HighPass — second-order high-pass filter (HPF cookbook formulae)
// =============================================================================
class HighPass
{
public:
    /// Compute HPF coefficients.
    /// @param cutoff_hz   -3dB cutoff frequency in Hz (clamped > 0).
    /// @param sample_rate Sampling rate in Hz (clamped > 0).
    void configure(float cutoff_hz, float sample_rate) noexcept
    {
        const float fs = (sample_rate > 0.0F) ? sample_rate : 48000.0F;
        const float fc = (cutoff_hz   > 0.0F) ? cutoff_hz   : 1000.0F;

        // Cookbook HPF: same alpha, different b0/b1/b2.
        constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
        constexpr float kQ     = std::numbers::sqrt2_v<float> / 2.0F;
        const float w0    = kTwoPi * fc / fs;
        const float cosw0 = std::cos(w0);
        const float sinw0 = std::sin(w0);
        const float alpha = sinw0 / (2.0F * kQ);

        const float a0_inv = 1.0F / (1.0F + alpha);
        coeffs_.b0 = ((1.0F + cosw0) / 2.0F) * a0_inv;
        coeffs_.b1 = (-(1.0F + cosw0))        * a0_inv;
        coeffs_.b2 = coeffs_.b0;
        coeffs_.a1 = (-2.0F * cosw0)          * a0_inv;
        coeffs_.a2 = (1.0F  - alpha)           * a0_inv;

        state_.reset();
    }

    /// Process a block of samples in-place.
    void process(std::span<float> samples) noexcept
    {
        for (float& s : samples)
            s = tick(s);
    }

    /// Process a single sample.
    [[nodiscard]] float process(float x) noexcept { return tick(x); }

    /// Read the current coefficients.
    [[nodiscard]] const BiquadCoeffs& coeffs() const noexcept { return coeffs_; }

    void reset() noexcept { state_.reset(); }

private:
    BiquadCoeffs coeffs_ { BiquadCoeffs::identity() };
    BiquadState  state_  {};

    [[nodiscard]] float tick(float x) noexcept
    {
        const float y  = coeffs_.b0 * x + state_.w1;
        state_.w1      = coeffs_.b1 * x - coeffs_.a1 * y + state_.w2;
        state_.w2      = coeffs_.b2 * x - coeffs_.a2 * y;
        return y;
    }
};

// =============================================================================
// DelayLine — integer-sample delay with circular buffer
//
// configure(max_samples) allocates the ring buffer (heap, once).
// write(x)              pushes one sample at the current write head.
// read(samples_back)    reads the sample `samples_back` positions ago
//                       (1 = last written; max_samples = oldest available).
// =============================================================================
class DelayLine
{
public:
    /// Allocate delay buffer.
    /// @param max_samples Maximum delay in samples (must be >= 1).
    void configure(std::size_t max_samples)
    {
        const std::size_t sz = (max_samples >= 1u) ? max_samples : 1u;
        buffer_.assign(sz, 0.0F);
        write_ = 0;
    }

    /// Push one sample into the delay line.
    void write(float x) noexcept
    {
        buffer_[write_] = x;
        ++write_;
        if (write_ >= buffer_.size())
            write_ = 0;
    }

    /// Read a sample `samples_back` positions behind the *next* write head.
    /// samples_back = 1 → the last written sample.
    /// samples_back = max_samples → the oldest sample.
    /// Out-of-range values are clamped to [1, buffer_.size()].
    [[nodiscard]] float read(std::size_t samples_back) const noexcept
    {
        const std::size_t sz = buffer_.size();
        const std::size_t sb = std::clamp(samples_back, std::size_t{1}, sz);
        // write_ points to the *next* slot to overwrite, so write_-1 was last written.
        const std::size_t idx = (write_ + sz - sb) % sz;
        return buffer_[idx];
    }

    [[nodiscard]] std::size_t max_samples() const noexcept { return buffer_.size(); }

    void reset() noexcept
    {
        for (float& s : buffer_) s = 0.0F;
        write_ = 0;
    }

private:
    std::vector<float> buffer_;
    std::size_t        write_ { 0 };
};

// =============================================================================
// Reverb — Schroeder comb-filter + allpass network (real scalar FDN)
//
// 4 parallel Low-Pass Feedback Comb Filters (LBCF, Freeverb topology) for the
// decaying late field, fed into 2 series Schroeder allpass filters for echo
// diffusion. Comb feedback gains are derived from a room-size-mapped RT60 so
// that the impulse-response envelope decays by 60 dB over the target time;
// delay lengths are mutually prime to avoid coincident echo flutter.
// =============================================================================
class Reverb
{
public:
    struct Config
    {
        float room_size   { 0.5F };   ///< [0,1] — affects comb delay lengths
        float damping     { 0.5F };   ///< [0,1] — high-freq rolloff in feedback path
        float wet_dry_mix { 0.5F };   ///< [0,1] — 0 = dry only, 1 = wet only
        float sample_rate { 48000.0F }; ///< Sample rate in Hz
    };

    /// Configure the reverb unit.
    void configure(const Config& cfg) noexcept;

    /// Process a block of samples.
    void process(std::span<const float> input, std::span<float> output) noexcept;

    /// Reset filter state and delay lines.
    void reset() noexcept;

    [[nodiscard]] const Config& config() const noexcept { return config_; }

private:
    struct CombFilter
    {
        DelayLine delay_line;
        float     s_prev { 0.0F };
        float     feedback { 0.0F };
        float     damping { 0.0F };

        void configure(std::size_t delay_samples, float g, float d) noexcept;
        float tick(float x) noexcept;
        void reset() noexcept;
    };

    struct AllpassFilter
    {
        DelayLine delay_line;
        float     feedback { 0.5F };

        void configure(std::size_t delay_samples, float g) noexcept;
        float tick(float x) noexcept;
        void reset() noexcept;
    };

    Config config_ {};
    std::array<CombFilter, 4> combs_;
    std::array<AllpassFilter, 2> allpasses_;
};

}  // namespace cd::audio::dsp_fx
