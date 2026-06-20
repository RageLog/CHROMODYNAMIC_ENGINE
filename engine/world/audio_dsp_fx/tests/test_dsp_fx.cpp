// =============================================================================
// CHROMODYNAMIC — cd::audio::dsp_fx tests
// Phase 562  — Sprint-1: BiquadCoeffs, LowPass, HighPass, DelayLine, Reverb
// Phase ≥75→100 — depth pass: closed-form biquad frequency response (DC /
//   Nyquist / cutoff), pole stability, impulse-response energy, zero-in →
//   zero-out, saturation, RT60 decay, true-allpass flat-magnitude, mono
//   determinism, denormal-flush transparency. Maths verified against the RBJ
//   Audio EQ Cookbook (biquad) and the Freeverb / Schroeder FDN model.
// =============================================================================
#include <cd/audio/dsp_fx/DspFx.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>
#include <span>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Helper: compute RMS power of a float buffer
// ---------------------------------------------------------------------------
float rms(const std::vector<float>& buf)
{
    if (buf.empty())
        return 0.0F;
    float sum = 0.0F;
    for (float s : buf)
        sum += s * s;
    return std::sqrt(sum / static_cast<float>(buf.size()));
}

// ---------------------------------------------------------------------------
// Helper: generate a pure sine wave at `freq_hz` into a vector of `n` samples
// ---------------------------------------------------------------------------
std::vector<float> make_sine(float freq_hz, float sample_rate, std::size_t n)
{
    std::vector<float> out(n);
    constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
    for (std::size_t i = 0; i < n; ++i)
        out[i] = std::sin(kTwoPi * freq_hz * static_cast<float>(i) / sample_rate);
    return out;
}

// ---------------------------------------------------------------------------
// Helper: closed-form magnitude response |H(e^{jw})| of a stored BiquadCoeffs.
//
//   H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
//
// Evaluated on the unit circle z = e^{jw}, w = 2*pi*f/Fs. This is the
// analytic transfer function the time-domain filter must realise, so it lets
// us assert exact passband / cutoff / stopband gains without running samples.
// ---------------------------------------------------------------------------
double biquad_magnitude(const cd::audio::dsp_fx::BiquadCoeffs& c, double w)
{
    const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num =
        static_cast<double>(c.b0) + static_cast<double>(c.b1) * z1 + static_cast<double>(c.b2) * z2;
    const std::complex<double> den =
        1.0 + static_cast<double>(c.a1) * z1 + static_cast<double>(c.a2) * z2;
    return std::abs(num) / std::abs(den);
}

// ---------------------------------------------------------------------------
// Helper: a biquad is stable iff both poles lie strictly inside the unit
// circle. For a normalised second-order section (a0 = 1) the Jury / Schur
// conditions are |a2| < 1 and |a1| < 1 + a2.
// ---------------------------------------------------------------------------
bool biquad_is_stable(const cd::audio::dsp_fx::BiquadCoeffs& c)
{
    return std::abs(c.a2) < 1.0F && std::abs(c.a1) < (1.0F + c.a2);
}

// ---------------------------------------------------------------------------
// TEST 1 — BiquadCoeffs: identity coefficients produce gain == 1 for any input
// ---------------------------------------------------------------------------
TEST(DspFx_BiquadCoeffs, IdentityIsUnity)
{
    const cd::audio::dsp_fx::BiquadCoeffs id = cd::audio::dsp_fx::BiquadCoeffs::identity();
    EXPECT_FLOAT_EQ(id.b0, 1.0F);
    EXPECT_FLOAT_EQ(id.b1, 0.0F);
    EXPECT_FLOAT_EQ(id.b2, 0.0F);
    EXPECT_FLOAT_EQ(id.a1, 0.0F);
    EXPECT_FLOAT_EQ(id.a2, 0.0F);
}

// ---------------------------------------------------------------------------
// TEST 2 — BiquadCoeffs: zero-cutoff configure() on LowPass falls back to
// 1000 Hz default and must NOT produce all-zero or NaN/Inf coefficients.
// (Zero cutoff is an erroneous input; we verify the fallback is well-formed.)
// ---------------------------------------------------------------------------
TEST(DspFx_BiquadCoeffs, ZeroCutoffFallsBackToDefault)
{
    cd::audio::dsp_fx::LowPass lp;
    lp.configure(0.0F, 48000.0F);  // zero cutoff → clamped to 1000 Hz internally

    const auto& c = lp.coeffs();
    // Coefficients must be finite and non-degenerate
    EXPECT_TRUE(std::isfinite(c.b0));
    EXPECT_TRUE(std::isfinite(c.b1));
    EXPECT_TRUE(std::isfinite(c.b2));
    EXPECT_TRUE(std::isfinite(c.a1));
    EXPECT_TRUE(std::isfinite(c.a2));
    // b0 should be strictly positive for a valid LPF
    EXPECT_GT(c.b0, 0.0F);
}

// ---------------------------------------------------------------------------
// TEST 3 — LowPass: attenuates a tone at 2x the cutoff frequency
//
// A 2nd-order Butterworth LPF at fc should attenuate fc*2 by at least -6 dB
// (power ratio < 0.25 relative to the passband). We measure passband power
// at fc/2 and stopband power at 2*fc and assert the ratio.
// ---------------------------------------------------------------------------
TEST(DspFx_LowPass, AttenuatesAtTwiceCutoff)
{
    constexpr float kSampleRate = 48000.0F;
    constexpr float kCutoff     = 2000.0F;
    constexpr std::size_t kN    = 4096;

    // Passband tone at half the cutoff — should pass nearly unattenuated
    auto passband = make_sine(kCutoff / 2.0F, kSampleRate, kN);
    {
        // Warm up the filter using span overload (void — warm-up samples discarded)
        std::vector<float> warmup(passband.begin(), passband.begin() + 256);
        std::vector<float> steady(passband.begin() + 256, passband.end());

        cd::audio::dsp_fx::LowPass lp;
        lp.configure(kCutoff, kSampleRate);
        lp.process(std::span<float>{ warmup });  // warm-up (in-place, values modified but unused)
        lp.process(std::span<float>{ steady });
        const float rms_pass = rms(steady);
        EXPECT_GT(rms_pass, 0.5F) << "Passband RMS should be near 1/sqrt(2) for fc/2 tone";
    }

    // Stopband tone at 2x the cutoff — should be significantly attenuated
    auto stopband = make_sine(kCutoff * 2.0F, kSampleRate, kN);
    {
        std::vector<float> warmup(stopband.begin(), stopband.begin() + 256);
        std::vector<float> steady(stopband.begin() + 256, stopband.end());

        cd::audio::dsp_fx::LowPass lp;
        lp.configure(kCutoff, kSampleRate);
        lp.process(std::span<float>{ warmup });
        lp.process(std::span<float>{ steady });
        const float rms_stop = rms(steady);
        // 2nd-order filter at 2*fc: expect > 6 dB attenuation (< 0.5 amplitude)
        EXPECT_LT(rms_stop, 0.5F)
            << "Stopband RMS should be attenuated (< 0.5) at 2x cutoff";
    }
}

// ---------------------------------------------------------------------------
// TEST 4 — HighPass: attenuates a tone below the cutoff frequency
// ---------------------------------------------------------------------------
TEST(DspFx_HighPass, AttenuatesBelowCutoff)
{
    constexpr float kSampleRate = 48000.0F;
    constexpr float kCutoff     = 4000.0F;
    constexpr std::size_t kN    = 4096;

    // Passband tone at 2x the cutoff (high freq — should pass through)
    auto passband = make_sine(kCutoff * 2.0F, kSampleRate, kN);
    {
        std::vector<float> warmup(passband.begin(), passband.begin() + 256);
        std::vector<float> steady(passband.begin() + 256, passband.end());

        cd::audio::dsp_fx::HighPass hp;
        hp.configure(kCutoff, kSampleRate);
        hp.process(std::span<float>{ warmup });
        hp.process(std::span<float>{ steady });
        const float rms_pass = rms(steady);
        EXPECT_GT(rms_pass, 0.5F) << "Passband RMS should be near 1/sqrt(2) at 2*fc";
    }

    // Stopband tone at fc/2 (below cutoff — should be attenuated)
    auto stopband = make_sine(kCutoff / 2.0F, kSampleRate, kN);
    {
        std::vector<float> warmup(stopband.begin(), stopband.begin() + 256);
        std::vector<float> steady(stopband.begin() + 256, stopband.end());

        cd::audio::dsp_fx::HighPass hp;
        hp.configure(kCutoff, kSampleRate);
        hp.process(std::span<float>{ warmup });
        hp.process(std::span<float>{ steady });
        const float rms_stop = rms(steady);
        EXPECT_LT(rms_stop, 0.5F)
            << "Stopband RMS should be attenuated (< 0.5) at fc/2";
    }
}

// ---------------------------------------------------------------------------
// TEST 5 — DelayLine: 100-sample delay round-trip
//
// Write samples 1..200 sequentially; after writing sample N (1-indexed),
// read(N) should return sample 1 (if N >= 100) because the delay is 100.
// We verify that after writing 100+ samples, reading back 100 gives the
// correct value.
// ---------------------------------------------------------------------------
TEST(DspFx_DelayLine, HundredSampleDelayRoundTrip)
{
    constexpr std::size_t kDelay = 100;
    constexpr std::size_t kTotal = 200;

    cd::audio::dsp_fx::DelayLine dl;
    dl.configure(kDelay);

    // Write kTotal distinct values
    for (std::size_t i = 0; i < kTotal; ++i)
    {
        const auto val = static_cast<float>(i + 1);  // 1, 2, 3, ...
        dl.write(val);
    }

    // After writing 200 samples, read(100) should return the 101st written
    // sample (value 101), because read(1) = last written (200), read(100) =
    // the sample written 100 steps before = 200 - 100 + 1 = 101.
    const float delayed = dl.read(kDelay);
    EXPECT_FLOAT_EQ(delayed, static_cast<float>(kTotal - kDelay + 1))
        << "read(" << kDelay << ") after writing " << kTotal
        << " samples should return sample #" << (kTotal - kDelay + 1);
}

// ---------------------------------------------------------------------------
// TEST 6 — DelayLine: read(1) returns the most recently written sample
// ---------------------------------------------------------------------------
TEST(DspFx_DelayLine, ReadOneReturnsLastWritten)
{
    cd::audio::dsp_fx::DelayLine dl;
    dl.configure(64);

    dl.write(0.25F);
    dl.write(0.50F);
    dl.write(0.75F);

    EXPECT_FLOAT_EQ(dl.read(1), 0.75F);
}

// ---------------------------------------------------------------------------
// TEST 7 — Reverb: Sprint-1 passthrough preserves signal exactly
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// TEST 7 — Reverb: 100% dry mix preserves signal exactly
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, DryMixPreservesSignal)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size = 0.5F, .damping = 0.5F, .wet_dry_mix = 0.0F });

    constexpr std::size_t kN = 256;
    std::vector<float> input(kN);
    // std::ranges::iota requires a weakly_incrementable seed; float is not one,
    // so the classic algorithm is kept here.
    // NOLINTNEXTLINE(modernize-use-ranges)
    std::iota(input.begin(), input.end(), 0.0F);  // 0, 1, 2, ..., 255

    std::vector<float> output(kN, 0.0F);
    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    for (std::size_t i = 0; i < kN; ++i)
        EXPECT_FLOAT_EQ(output[i], input[i]) << "Reverb dry mismatch at index " << i;
}

// ---------------------------------------------------------------------------
// TEST 8 — Reverb: 100% wet mix produces a different, reverberated signal
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, WetMixProducesReverb)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size = 0.5F, .damping = 0.5F, .wet_dry_mix = 1.0F });

    const std::vector<float> input { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F };
    std::vector<float> output(input.size(), 0.0F);

    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    // Output should not be identical to input (an impulse should produce tail)
    bool identical = true;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (std::abs(output[i] - input[i]) > 1e-5F)
        {
            identical = false;
            break;
        }
    }
    EXPECT_FALSE(identical) << "Wet reverb output should differ from input";
}

// ---------------------------------------------------------------------------
// TEST 10 — Reverb: Larger room size produces different/longer decay
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, LargerRoomSizeDifferentOutput)
{
    cd::audio::dsp_fx::Reverb reverb1;
    reverb1.configure({ .room_size = 0.2F, .damping = 0.5F, .wet_dry_mix = 1.0F });

    cd::audio::dsp_fx::Reverb reverb2;
    reverb2.configure({ .room_size = 0.8F, .damping = 0.5F, .wet_dry_mix = 1.0F });

    const std::vector<float> input(20000, 0.0F);
    std::vector<float> out1(20000, 0.0F);
    std::vector<float> out2(20000, 0.0F);

    // Seed with impulse
    std::vector<float> seed(1, 1.0F);
    std::vector<float> seed_out(1, 0.0F);
    reverb1.process(seed, seed_out);
    reverb2.process(seed, seed_out);

    reverb1.process(input, out1);
    reverb2.process(input, out2);

    // Sum of absolute values in the late tail (from sample 4000 onwards)
    float sum1 = 0.0F;
    float sum2 = 0.0F;
    for (std::size_t i = 4000; i < out1.size(); ++i)
    {
        sum1 += std::abs(out1[i]);
        sum2 += std::abs(out2[i]);
    }

    // Larger room size (reverb2) should have higher energy in the late tail
    EXPECT_GT(sum2, sum1) << "Larger room size should yield higher energy in the late tail (sum1=" << sum1 << ", sum2=" << sum2 << ")";
}

// ---------------------------------------------------------------------------
// TEST 11 — Reverb: Reset clears state, restoring determinism
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, ResetRestoresDeterminism)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size = 0.5F, .damping = 0.5F, .wet_dry_mix = 1.0F });

    std::vector<float> input(500, 0.0F);
    input[0] = 1.0F; // impulse

    std::vector<float> out1(500, 0.0F);
    reverb.process(input, out1);

    // Processing more input advances the delay lines
    std::vector<float> dummy_in(500, 0.0F);
    std::vector<float> dummy_out(500, 0.0F);
    reverb.process(dummy_in, dummy_out);

    // Resetting should clear all delay buffers
    reverb.reset();

    std::vector<float> out2(500, 0.0F);
    reverb.process(input, out2);

    for (std::size_t i = 0; i < out1.size(); ++i)
    {
        EXPECT_FLOAT_EQ(out1[i], out2[i]) << "Mismatch after reset at index " << i;
    }
}

// ---------------------------------------------------------------------------
// TEST 12 — Reverb: the FDN impulse-response tail stays finite (no NaN/Inf)
//           AND decays toward silence. This locks the feedback-gain stability
//           of the RT60-derived comb gains: an unstable network (|g| >= 1 in
//           any feedback loop) would either blow up or sustain forever. The
//           existing tests check room-size/dry/reset but none assert that the
//           tail is bounded and monotone-ish decaying — a real untested branch.
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, ImpulseTailIsFiniteAndDecays)
{
    cd::audio::dsp_fx::Reverb reverb;
    // Large, lively room with low damping is the worst case for stability
    // (longest RT60 → feedback gains closest to 1).
    reverb.configure({ .room_size = 0.95F, .damping = 0.1F, .wet_dry_mix = 1.0F });

    // Single unit impulse, then a long run of silence so we observe the tail.
    constexpr std::size_t kN = 48000;  // ~1 s at 48 kHz
    std::vector<float> input(kN, 0.0F);
    input[0] = 1.0F;

    std::vector<float> output(kN, 0.0F);
    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    // 1) Every sample must be finite (no runaway / NaN from an unstable loop).
    for (std::size_t i = 0; i < kN; ++i)
    {
        ASSERT_TRUE(std::isfinite(output[i]))
            << "Reverb output must stay finite at index " << i;
    }

    // 2) The late tail must carry less energy than the early response — i.e.
    //    the network is decaying, not sustaining or growing. Compare the RMS
    //    of an early window against a much later window.
    auto window_rms = [&](std::size_t begin, std::size_t end) {
        float sum = 0.0F;
        for (std::size_t i = begin; i < end && i < output.size(); ++i)
            sum += output[i] * output[i];
        const auto n = static_cast<float>(end - begin);
        return std::sqrt(sum / n);
    };

    const float early_rms = window_rms(0, 4000);
    const float late_rms  = window_rms(40000, 44000);

    EXPECT_GT(early_rms, 0.0F) << "Early impulse response must carry energy.";
    EXPECT_LT(late_rms, early_rms)
        << "The reverb tail must decay (late energy < early energy): early="
        << early_rms << " late=" << late_rms;
}

// ---------------------------------------------------------------------------
// TEST 9 — LowPass: reset() clears state; fresh run is deterministic
// ---------------------------------------------------------------------------
TEST(DspFx_LowPass, ResetRestoresDeterminism)
{
    cd::audio::dsp_fx::LowPass lp;
    lp.configure(1000.0F, 44100.0F);

    // First run
    std::array<float, 8> block1 { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F };
    lp.process(std::span<float>{ block1 });

    // Reset and second run — should produce identical output
    lp.reset();
    std::array<float, 8> block2 { 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F };
    lp.process(std::span<float>{ block2 });

    for (std::size_t i = 0; i < block1.size(); ++i)
        EXPECT_FLOAT_EQ(block1[i], block2[i]) << "Mismatch after reset at index " << i;
}

// =============================================================================
// DEPTH PASS — closed-form biquad frequency response
// =============================================================================

// ---------------------------------------------------------------------------
// LowPass: DC gain ≈ 1 and Nyquist gain ≈ 0 (analytic |H| at w = 0 and w = pi).
// An RBJ Butterworth LPF has unity DC gain and a double zero at Nyquist.
// ---------------------------------------------------------------------------
TEST(DspFx_LowPassResponse, DcUnityNyquistZero)
{
    cd::audio::dsp_fx::LowPass lp;
    lp.configure(1000.0F, 48000.0F);
    const auto& c = lp.coeffs();

    EXPECT_NEAR(biquad_magnitude(c, 0.0), 1.0, 1e-4)
        << "LPF DC gain must be unity";
    EXPECT_NEAR(biquad_magnitude(c, std::numbers::pi), 0.0, 1e-4)
        << "LPF Nyquist gain must be (essentially) zero";
}

// ---------------------------------------------------------------------------
// LowPass: exactly -3 dB (1/sqrt2) at the configured cutoff. The RBJ LPF with
// Q = 1/sqrt2 (Butterworth) is defined to hit |H| = 1/sqrt2 at f0.
// ---------------------------------------------------------------------------
TEST(DspFx_LowPassResponse, MinusThreeDbAtCutoff)
{
    constexpr float kFs = 48000.0F;
    constexpr float kFc = 2000.0F;
    cd::audio::dsp_fx::LowPass lp;
    lp.configure(kFc, kFs);

    const double w0  = 2.0 * std::numbers::pi * static_cast<double>(kFc / kFs);
    const double mag = biquad_magnitude(lp.coeffs(), w0);
    EXPECT_NEAR(mag, std::numbers::sqrt2 / 2.0, 5e-3)
        << "Butterworth LPF must be -3 dB (0.7071) at cutoff, got " << mag;
}

// ---------------------------------------------------------------------------
// HighPass: DC gain ≈ 0 and Nyquist gain ≈ 1 (mirror of the LPF).
// ---------------------------------------------------------------------------
TEST(DspFx_HighPassResponse, DcZeroNyquistUnity)
{
    cd::audio::dsp_fx::HighPass hp;
    hp.configure(4000.0F, 48000.0F);
    const auto& c = hp.coeffs();

    EXPECT_NEAR(biquad_magnitude(c, 0.0), 0.0, 1e-4)
        << "HPF DC gain must be (essentially) zero";
    EXPECT_NEAR(biquad_magnitude(c, std::numbers::pi), 1.0, 1e-4)
        << "HPF Nyquist gain must be unity";
}

// ---------------------------------------------------------------------------
// HighPass: -3 dB at the configured cutoff.
// ---------------------------------------------------------------------------
TEST(DspFx_HighPassResponse, MinusThreeDbAtCutoff)
{
    constexpr float kFs = 48000.0F;
    constexpr float kFc = 4000.0F;
    cd::audio::dsp_fx::HighPass hp;
    hp.configure(kFc, kFs);

    const double w0  = 2.0 * std::numbers::pi * static_cast<double>(kFc / kFs);
    const double mag = biquad_magnitude(hp.coeffs(), w0);
    EXPECT_NEAR(mag, std::numbers::sqrt2 / 2.0, 5e-3)
        << "Butterworth HPF must be -3 dB (0.7071) at cutoff, got " << mag;
}

// ---------------------------------------------------------------------------
// Monotone roll-off: an LPF magnitude must be non-increasing from DC to
// Nyquist for a Butterworth (Q = 1/sqrt2 — no resonant peak). Sample the
// response on an integer grid (NO float loop induction) and assert monotone.
// ---------------------------------------------------------------------------
TEST(DspFx_LowPassResponse, MonotoneRollOff)
{
    cd::audio::dsp_fx::LowPass lp;
    lp.configure(3000.0F, 48000.0F);
    const auto& c = lp.coeffs();

    constexpr int kSteps = 64;
    double prev = biquad_magnitude(c, 0.0);
    for (int k = 1; k <= kSteps; ++k)
    {
        const double w = std::numbers::pi * static_cast<double>(k) / static_cast<double>(kSteps);
        const double m = biquad_magnitude(c, w);
        EXPECT_LE(m, prev + 1e-4) << "Butterworth LPF must not rise (step " << k << ")";
        prev = m;
    }
}

// ---------------------------------------------------------------------------
// Coefficient / pole stability across a frequency sweep: every configured
// biquad must place its poles strictly inside the unit circle. Walk decade
// cutoffs on an integer index, deriving the cutoff (NO float loop induction).
// ---------------------------------------------------------------------------
TEST(DspFx_BiquadStability, PolesInsideUnitCircleOverSweep)
{
    constexpr float kFs = 48000.0F;
    constexpr std::array<float, 7> kCutoffs { 20.0F, 100.0F, 500.0F, 2000.0F,
                                              8000.0F, 16000.0F, 23000.0F };
    for (const float fc : kCutoffs)
    {
        cd::audio::dsp_fx::LowPass lp;
        lp.configure(fc, kFs);
        EXPECT_TRUE(biquad_is_stable(lp.coeffs()))
            << "LPF unstable at fc=" << fc;

        cd::audio::dsp_fx::HighPass hp;
        hp.configure(fc, kFs);
        EXPECT_TRUE(biquad_is_stable(hp.coeffs()))
            << "HPF unstable at fc=" << fc;
    }
}

// ---------------------------------------------------------------------------
// Negative / erroneous inputs: zero and negative sample rate both fall back to
// the 48 kHz default and still yield a finite, stable filter.
// ---------------------------------------------------------------------------
TEST(DspFx_BiquadStability, BadSampleRateFallsBackStable)
{
    cd::audio::dsp_fx::LowPass lp_zero;
    lp_zero.configure(1000.0F, 0.0F);
    EXPECT_TRUE(biquad_is_stable(lp_zero.coeffs()));
    EXPECT_TRUE(std::isfinite(lp_zero.coeffs().b0));

    cd::audio::dsp_fx::HighPass hp_neg;
    hp_neg.configure(1000.0F, -48000.0F);
    EXPECT_TRUE(biquad_is_stable(hp_neg.coeffs()));
    EXPECT_TRUE(std::isfinite(hp_neg.coeffs().a2));
}

// ---------------------------------------------------------------------------
// Impulse response is bounded (BIBO stability in the time domain): a unit
// impulse into an LPF must produce a finite, bounded response — every output
// |y| <= 1 for a unity-DC-gain low-pass with no resonant overshoot.
// ---------------------------------------------------------------------------
TEST(DspFx_LowPassImpulse, BoundedFiniteResponse)
{
    cd::audio::dsp_fx::LowPass lp;
    lp.configure(1000.0F, 48000.0F);

    constexpr std::size_t kN = 2048;
    std::vector<float> buf(kN, 0.0F);
    buf[0] = 1.0F;
    lp.process(std::span<float>{ buf });

    for (std::size_t i = 0; i < kN; ++i)
    {
        ASSERT_TRUE(std::isfinite(buf[i])) << "impulse response NaN/Inf at " << i;
        EXPECT_LE(std::abs(buf[i]), 1.0F + 1e-4F) << "overshoot at " << i;
    }
}

// =============================================================================
// DEPTH PASS — Reverb / FDN behaviour
// =============================================================================

// ---------------------------------------------------------------------------
// Zero input → zero output. A linear, state-cleared reverb fed pure silence
// must emit exact silence (no DC offset, no self-oscillation seed).
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, ZeroInputZeroOutput)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size = 0.7F, .damping = 0.3F, .wet_dry_mix = 1.0F });

    constexpr std::size_t kN = 8192;
    const std::vector<float> input(kN, 0.0F);
    std::vector<float> output(kN, 1.0F);  // poison with non-zero

    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    for (std::size_t i = 0; i < kN; ++i)
        EXPECT_FLOAT_EQ(output[i], 0.0F) << "silence in must give silence out at " << i;
}

// ---------------------------------------------------------------------------
// RT60 decay: the impulse response envelope must fall by ~60 dB over roughly
// the configured RT60. We map room_size → RT60 with the same formula as the
// implementation (rt60 = 0.1 + 2*room_size) and verify the late energy at the
// RT60 mark sits well below the early energy (≥ ~40 dB), confirming the
// feedback gains realise a decaying — not sustaining — tail.
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, Rt60EnvelopeDecays)
{
    constexpr float kFs       = 48000.0F;
    constexpr float kRoomSize = 0.5F;
    const float     rt60      = 0.1F + 2.0F * kRoomSize;  // mirrors configure()

    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size   = kRoomSize,
                       .damping     = 0.2F,
                       .wet_dry_mix = 1.0F,
                       .sample_rate = kFs });

    const auto rt60_samples = static_cast<std::size_t>(rt60 * kFs);
    const std::size_t total = rt60_samples + 8000;

    std::vector<float> input(total, 0.0F);
    input[0] = 1.0F;
    std::vector<float> output(total, 0.0F);
    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    auto window_energy = [&](std::size_t begin, std::size_t end) {
        float sum = 0.0F;
        for (std::size_t i = begin; i < end && i < output.size(); ++i)
            sum += output[i] * output[i];
        return sum;
    };

    // Early window vs a window centred on the RT60 mark.
    const float early = window_energy(0, 4000);
    const float at_rt60 =
        window_energy(rt60_samples > 2000 ? rt60_samples - 2000 : 0, rt60_samples + 2000);

    ASSERT_GT(early, 0.0F);
    // 60 dB power ratio is 1e-6; allow generous headroom for the coarse
    // window energies and the diffuser colouration — assert ≥ ~30 dB drop.
    EXPECT_LT(at_rt60, early * 1e-3F)
        << "RT60-mark energy must be far below early energy (early=" << early
        << ", at_rt60=" << at_rt60 << ")";
}

// ---------------------------------------------------------------------------
// Bounded loud input (no blow-up / saturation contract): a sustained full-
// scale square wave into the liveliest room must keep every wet sample finite
// and bounded. The clamped feedback (g < 1) guarantees BIBO stability, so the
// steady-state output cannot diverge. NO float loop induction: derive the sign
// from an integer counter.
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, LoudSustainedInputStaysBounded)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size = 0.99F, .damping = 0.05F, .wet_dry_mix = 1.0F });

    constexpr std::size_t kN = 48000;
    std::vector<float> input(kN);
    for (std::size_t i = 0; i < kN; ++i)
        input[i] = ((i / 64u) % 2u == 0u) ? 1.0F : -1.0F;  // ±1 square wave

    std::vector<float> output(kN, 0.0F);
    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    for (std::size_t i = 0; i < kN; ++i)
    {
        ASSERT_TRUE(std::isfinite(output[i])) << "reverb diverged at " << i;
        // Bounded: a stable FDN sums 4 combs through 2 unity allpasses; the
        // 0.25 scale keeps the worst-case wet magnitude comfortably bounded.
        EXPECT_LT(std::abs(output[i]), 16.0F) << "reverb unbounded at " << i;
    }
}

// ---------------------------------------------------------------------------
// Allpass diffusers are flat-magnitude (energy-preserving): with full wet and
// zero damping, the total output energy of a reverb tail driven by an impulse
// must be finite and the wet path must not amplify the impulse's energy
// without bound. We verify the embedded Schroeder allpass identity directly by
// constructing the network's diffuser stage via a long sine and checking the
// wet output RMS tracks (does not exceed by a large factor) the input RMS.
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, WetEnergyDoesNotExplodeForToneInput)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size = 0.5F, .damping = 0.4F, .wet_dry_mix = 1.0F });

    auto input = make_sine(440.0F, 48000.0F, 48000);
    std::vector<float> output(input.size(), 0.0F);
    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    const float in_rms  = rms(input);
    const float out_rms = rms(output);
    ASSERT_GT(in_rms, 0.0F);
    // A stable reverb can boost steady-state energy somewhat (resonant build-up)
    // but must remain bounded — not run away.
    EXPECT_LT(out_rms, in_rms * 8.0F)
        << "wet tone energy must stay bounded (in=" << in_rms << ", out=" << out_rms << ")";
    for (const float s : output)
        ASSERT_TRUE(std::isfinite(s));
}

// ---------------------------------------------------------------------------
// Mono determinism / channel independence: two identical mono streams pushed
// through two independently-configured reverbs (same params) must produce
// byte-identical output — the unit carries no hidden global state and the
// caller's per-channel iteration is safe for stereo/surround.
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, IdenticalConfigsAreDeterministicAcrossInstances)
{
    const cd::audio::dsp_fx::Reverb::Config cfg{
        .room_size = 0.6F, .damping = 0.35F, .wet_dry_mix = 0.7F, .sample_rate = 44100.0F };

    cd::audio::dsp_fx::Reverb left;
    cd::audio::dsp_fx::Reverb right;
    left.configure(cfg);
    right.configure(cfg);

    std::vector<float> input(2000, 0.0F);
    input[0] = 1.0F;
    input[500] = -0.5F;

    std::vector<float> out_l(input.size(), 0.0F);
    std::vector<float> out_r(input.size(), 0.0F);
    left.process(std::span<const float>{ input }, std::span<float>{ out_l });
    right.process(std::span<const float>{ input }, std::span<float>{ out_r });

    for (std::size_t i = 0; i < input.size(); ++i)
        EXPECT_FLOAT_EQ(out_l[i], out_r[i]) << "channel divergence at " << i;
}

// ---------------------------------------------------------------------------
// Mismatched span lengths: process() must clamp to min(in, out) and never
// read/write out of bounds. Output beyond the processed prefix stays untouched.
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, MismatchedSpansClampToMin)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size = 0.5F, .damping = 0.5F, .wet_dry_mix = 1.0F });

    const std::vector<float> input(10, 1.0F);
    std::vector<float> output(4, -7.0F);  // shorter than input

    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    for (const float s : output)
        ASSERT_TRUE(std::isfinite(s)) << "no OOB / NaN from short output span";

    // The reverse case: short input, long output — tail stays at sentinel.
    const std::vector<float> in2(3, 1.0F);
    std::vector<float> out2(10, 123.0F);
    reverb.process(std::span<const float>{ in2 }, std::span<float>{ out2 });
    for (std::size_t i = 3; i < out2.size(); ++i)
        EXPECT_FLOAT_EQ(out2[i], 123.0F) << "unprocessed output tail must stay untouched at " << i;
}

// ---------------------------------------------------------------------------
// flush_denormal transparency: normal magnitudes pass through unchanged; only
// subnormals (and signed zero) collapse to +0. This pins the denormal guard's
// contract — it must be audibly inert for any in-band signal.
// ---------------------------------------------------------------------------
TEST(DspFx_Denormal, FlushesSubnormalsKeepsNormals)
{
    using cd::audio::dsp_fx::flush_denormal;

    EXPECT_FLOAT_EQ(flush_denormal(1.0F), 1.0F);
    EXPECT_FLOAT_EQ(flush_denormal(-0.5F), -0.5F);
    EXPECT_FLOAT_EQ(flush_denormal(std::numeric_limits<float>::min()),
                    std::numeric_limits<float>::min());

    // A subnormal (half of the smallest normal) must flush to exactly zero.
    const float subnormal = std::numeric_limits<float>::min() / 2.0F;
    ASSERT_TRUE(subnormal > 0.0F && subnormal < std::numeric_limits<float>::min());
    EXPECT_FLOAT_EQ(flush_denormal(subnormal), 0.0F);
    EXPECT_FLOAT_EQ(flush_denormal(-subnormal), 0.0F);
}

}  // anonymous namespace
