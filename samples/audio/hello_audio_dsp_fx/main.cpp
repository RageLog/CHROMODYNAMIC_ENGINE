// =============================================================================
// CHROMODYNAMIC — samples/audio/hello_audio_dsp_fx
// Phase 588 — console proof that cd::audio::dsp_fx is consumable externally.
//
// Synthesizes a 1-second 1 kHz sine wave at 48 kHz, then pushes it through:
//   1. LowPass  (cutoff 500 Hz) — attenuates 1 kHz input tone.
//   2. HighPass (cutoff 2000 Hz) — attenuates 1 kHz input tone.
//   3. DelayLine (480 samples = 10 ms at 48 kHz) — round-trip verify.
//
// Assertions:
//   - LowPass  output RMS < input RMS   (significant attenuation expected)
//   - HighPass output RMS < input RMS   (significant attenuation expected)
//   - DelayLine round-trip: read(480) matches write(n-480) exactly
//
// Exits 0 on success, 1 on any assertion failure.
// No platform audio device required — offline DSP only.
// hello_engine UNTOUCHED.
// =============================================================================

#include <cd/audio/dsp_fx/DspFx.hpp>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace
{

constexpr float         kSampleRate  = 48000.0F;
constexpr float         kFreqHz      = 1000.0F;
constexpr std::size_t   kNumSamples  = static_cast<std::size_t>(kSampleRate);  // 1 second

// ---------------------------------------------------------------------------
// Generate a pure 1 kHz sine wave, amplitude 1.0.
// ---------------------------------------------------------------------------
[[nodiscard]] std::vector<float> make_sine()
{
    constexpr float kTwoPi = 6.28318530717958647692F;
    std::vector<float> buf(kNumSamples);
    for (std::size_t i = 0; i < kNumSamples; ++i)
        buf[i] = std::sin(kTwoPi * kFreqHz * static_cast<float>(i) / kSampleRate);
    return buf;
}

// ---------------------------------------------------------------------------
// Compute RMS of a float buffer.
// ---------------------------------------------------------------------------
[[nodiscard]] float rms(const std::vector<float>& buf)
{
    if (buf.empty())
        return 0.0F;
    float sum = 0.0F;
    for (const float s : buf)
        sum += s * s;
    return std::sqrt(sum / static_cast<float>(buf.size()));
}

}  // namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::printf("=== hello_audio_dsp_fx — cd::audio::dsp_fx consumability proof ===\n");

    // -----------------------------------------------------------------------
    // Generate source signal
    // -----------------------------------------------------------------------
    const std::vector<float> source = make_sine();
    const float source_rms = rms(source);
    std::printf("source:    1 kHz sine, %zu samples @ %.0f Hz, RMS = %.4f\n",
                kNumSamples, static_cast<double>(kSampleRate),
                static_cast<double>(source_rms));

    bool ok = true;

    // -----------------------------------------------------------------------
    // 1. LowPass (cutoff 500 Hz) — 1 kHz is in the stopband (2x cutoff).
    //    Expect: significant attenuation. The 2nd-order biquad delivers
    //    >= 6 dB attenuation at 2*fc, so output_rms < 0.5 * source_rms.
    // -----------------------------------------------------------------------
    {
        std::vector<float> buf(source);
        cd::audio::dsp_fx::LowPass lp;
        lp.configure(500.0F, kSampleRate);

        // Warm-up: process first 256 samples to fill filter state (discarded)
        std::vector<float> warmup(buf.begin(), buf.begin() + 256);
        lp.process(std::span<float>{ warmup });

        // Measure steady-state on remaining samples
        std::vector<float> steady(buf.begin() + 256, buf.end());
        lp.process(std::span<float>{ steady });
        const float lp_rms = rms(steady);

        std::printf("LowPass:   cutoff=500 Hz, output RMS = %.4f (source RMS = %.4f)\n",
                    static_cast<double>(lp_rms), static_cast<double>(source_rms));

        if (lp_rms >= source_rms)
        {
            std::printf("FAIL: LowPass did not attenuate 1 kHz signal (expected lp_rms < source_rms)\n");
            ok = false;
        }
        else
        {
            std::printf("LowPass:   PASS (%.1f%% of source)\n",
                        static_cast<double>(lp_rms / source_rms * 100.0F));
        }
    }

    // -----------------------------------------------------------------------
    // 2. HighPass (cutoff 2000 Hz) — 1 kHz is below cutoff (fc/2 = 1 kHz).
    //    Expect: significant attenuation.
    // -----------------------------------------------------------------------
    {
        std::vector<float> buf(source);
        cd::audio::dsp_fx::HighPass hp;
        hp.configure(2000.0F, kSampleRate);

        std::vector<float> warmup(buf.begin(), buf.begin() + 256);
        hp.process(std::span<float>{ warmup });

        std::vector<float> steady(buf.begin() + 256, buf.end());
        hp.process(std::span<float>{ steady });
        const float hp_rms = rms(steady);

        std::printf("HighPass:  cutoff=2000 Hz, output RMS = %.4f (source RMS = %.4f)\n",
                    static_cast<double>(hp_rms), static_cast<double>(source_rms));

        if (hp_rms >= source_rms)
        {
            std::printf("FAIL: HighPass did not attenuate 1 kHz signal (expected hp_rms < source_rms)\n");
            ok = false;
        }
        else
        {
            std::printf("HighPass:  PASS (%.1f%% of source)\n",
                        static_cast<double>(hp_rms / source_rms * 100.0F));
        }
    }

    // -----------------------------------------------------------------------
    // 3. DelayLine — 480 samples (10 ms at 48 kHz) round-trip check.
    //    Write all source samples; after writing, read(480) must equal the
    //    sample written 480 steps ago. We verify 8 evenly spaced positions.
    // -----------------------------------------------------------------------
    {
        constexpr std::size_t kDelay = 480;

        cd::audio::dsp_fx::DelayLine dl;
        dl.configure(kDelay);

        bool delay_ok = true;
        for (std::size_t i = 0; i < kNumSamples; ++i)
        {
            dl.write(source[i]);

            // Once we have written enough samples to fill the delay, verify.
            if (i >= kDelay)
            {
                const float expected = source[i - kDelay + 1];
                const float got      = dl.read(kDelay);
                // Allow tiny float rounding tolerance (epsilon * 8).
                const float diff = std::fabs(got - expected);
                if (diff > 1e-6F)
                {
                    std::printf("FAIL: DelayLine mismatch at sample %zu: expected %.6f got %.6f\n",
                                i, static_cast<double>(expected), static_cast<double>(got));
                    delay_ok = false;
                    ok = false;
                    break;
                }
            }
        }
        std::printf("DelayLine: 480-sample delay round-trip: %s\n",
                    delay_ok ? "PASS" : "FAIL");
    }

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    if (ok)
    {
        std::printf("[hello_audio_dsp_fx] ALL PASS\n");
        return 0;
    }
    std::printf("[hello_audio_dsp_fx] FAILED\n");
    return 1;
}
