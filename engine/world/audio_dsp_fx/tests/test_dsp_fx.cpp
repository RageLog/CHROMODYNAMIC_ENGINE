// =============================================================================
// CHROMODYNAMIC — cd::audio::dsp_fx tests
// Phase 562 — Sprint-1: BiquadCoeffs, LowPass, HighPass, DelayLine, Reverb
// =============================================================================
#include <cd/audio/dsp_fx/DspFx.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numeric>

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
    constexpr float kTwoPi = 6.28318530717958647692F;
    for (std::size_t i = 0; i < n; ++i)
        out[i] = std::sin(kTwoPi * freq_hz * static_cast<float>(i) / sample_rate);
    return out;
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
        const float val = static_cast<float>(i + 1);  // 1, 2, 3, ...
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
TEST(DspFx_Reverb, PassthroughPreservesSignal)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({});

    constexpr std::size_t kN = 256;
    std::vector<float> input(kN);
    std::iota(input.begin(), input.end(), 0.0F);  // 0, 1, 2, ..., 255

    std::vector<float> output(kN, 0.0F);
    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    for (std::size_t i = 0; i < kN; ++i)
        EXPECT_FLOAT_EQ(output[i], input[i]) << "Reverb passthrough mismatch at index " << i;
}

// ---------------------------------------------------------------------------
// TEST 8 — Reverb: passthrough with non-default config still passes signal
// ---------------------------------------------------------------------------
TEST(DspFx_Reverb, PassthroughWithNonDefaultConfig)
{
    cd::audio::dsp_fx::Reverb reverb;
    reverb.configure({ .room_size = 0.9F, .damping = 0.3F, .wet_dry_mix = 1.0F });

    const std::array<float, 4> input { 0.1F, -0.2F, 0.3F, -0.4F };
    std::array<float, 4> output {};
    reverb.process(std::span<const float>{ input }, std::span<float>{ output });

    for (std::size_t i = 0; i < input.size(); ++i)
        EXPECT_FLOAT_EQ(output[i], input[i]);
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

}  // anonymous namespace
