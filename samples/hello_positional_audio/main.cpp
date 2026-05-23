// =============================================================================
// CHROMODYNAMIC — samples/hello_positional_audio
//
// Demos the cd::audio::PositionalSource pipeline:
//   * Synthesizes 0.5 s of mono 440 Hz sine wave at 48 kHz.
//   * Orbits a source around the listener (full 360° in the sample
//     duration; source 1 m out, listener at origin facing -Z).
//   * Runs the sine through PositionalSource block-by-block, applying
//     ITD + ILD + stereo gains per block.
//   * Reports left/right RMS at 5 key azimuths (0°, 90°, 180°, 270°,
//     full sweep average) so the panning effect is visible without
//     audio output.
//
// Headless / CI smoke-safe — no audio playback, no file I/O. The
// audio backends (WASAPI/CoreAudio/ALSA) consume the same
// PositionalSource output; this sample shows the DSP layer alone.
// =============================================================================
#include <cd/audio/Positional.hpp>
#include <cd/audio/PositionalSource.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

constexpr std::uint32_t kSampleRate = 48000;
constexpr float kDuration = 0.5F;
constexpr std::uint32_t kTotalFrames = static_cast<std::uint32_t>(
    kSampleRate * kDuration);
constexpr std::uint32_t kBlockFrames = 256;
constexpr float kPi = 3.14159265F;
constexpr float kTwoPi = 2.0F * kPi;

void rms_of(std::span<const float> buf, float& left_rms, float& right_rms)
{
    double l_sum = 0.0;
    double r_sum = 0.0;
    std::size_t pairs = buf.size() / 2;
    for (std::size_t i = 0; i < pairs; ++i)
    {
        const double l = static_cast<double>(buf[i * 2 + 0]);
        const double r = static_cast<double>(buf[i * 2 + 1]);
        l_sum += l * l;
        r_sum += r * r;
    }
    if (pairs == 0)
    {
        left_rms = right_rms = 0.0F;
        return;
    }
    left_rms = static_cast<float>(std::sqrt(l_sum / static_cast<double>(pairs)));
    right_rms = static_cast<float>(std::sqrt(r_sum / static_cast<double>(pairs)));
}

}  // namespace

int main()
{
    std::printf("=== hello_positional_audio — orbiting 440 Hz sine ===\n");
    std::printf("  sample_rate = %u Hz, duration = %.2f s, block = %u\n",
                kSampleRate, static_cast<double>(kDuration), kBlockFrames);

    // 1. Synthesize the mono sine wave.
    std::vector<float> mono(kTotalFrames);
    for (std::uint32_t i = 0; i < kTotalFrames; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        mono[i] = static_cast<float>(0.5 * std::sin(
            static_cast<double>(kTwoPi) * 440.0 * static_cast<double>(t)));
    }

    // 2. Set up positional source + listener.
    cd::audio::PositionalSource src;
    src.prime(kSampleRate, kBlockFrames);
    const cd::audio::ListenerPose listener {};  // origin, facing -Z

    // 3. Orbit + process block-by-block. Stereo output accumulates.
    std::vector<float> stereo(static_cast<std::size_t>(kTotalFrames) * 2, 0.0F);
    std::vector<float> block_in(kBlockFrames);
    std::vector<float> block_out(static_cast<std::size_t>(kBlockFrames) * 2);

    std::uint32_t pos = 0;
    while (pos < kTotalFrames)
    {
        const auto frames = std::min(kBlockFrames, kTotalFrames - pos);
        // Per-block source position: orbit at radius 1 m, full 360° over
        // the whole sample duration. theta=0 is in front (-Z).
        const float t_frac = static_cast<float>(pos) / static_cast<float>(kTotalFrames);
        const float theta = kTwoPi * t_frac;
        const cd::math::Vec3f source_pos {
            static_cast<float>(std::sin(static_cast<double>(theta))),
            0.0F,
            static_cast<float>(-std::cos(static_cast<double>(theta)))
        };

        // Copy input slice into block_in (and zero the tail if last block
        // is short).
        std::fill(block_in.begin(), block_in.end(), 0.0F);
        for (std::uint32_t i = 0; i < frames; ++i)
            block_in[i] = mono[pos + i];

        std::span<const float> in_view { block_in.data(), kBlockFrames };
        std::span<float> out_view { block_out.data(), block_out.size() };
        src.process(in_view, listener, source_pos, out_view);

        // Copy the first `frames` stereo samples into the global buffer.
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            stereo[(pos + i) * 2 + 0] = block_out[i * 2 + 0];
            stereo[(pos + i) * 2 + 1] = block_out[i * 2 + 1];
        }
        pos += frames;
    }

    // 4. Report L/R RMS in 4 quarter slices of the orbit.
    const std::size_t quarter = stereo.size() / 8;  // 1/4 of stereo (=1/4 of orbit), in float pairs
    auto slice = [&](std::size_t start_pair, std::size_t pair_count) {
        return std::span<const float>(stereo.data() + start_pair * 2, pair_count * 2);
    };

    float l_rms = 0.0F, r_rms = 0.0F;
    auto report = [&](const char* label) {
        std::printf("  %s : L=%.3f  R=%.3f\n", label,
                    static_cast<double>(l_rms), static_cast<double>(r_rms));
    };
    std::printf("\n=== L/R RMS by orbit quadrant ===\n");
    rms_of(slice(0 * quarter, quarter), l_rms, r_rms);
    report("0° → 90°   (front to right)  ");
    rms_of(slice(1 * quarter, quarter), l_rms, r_rms);
    report("90° → 180°  (right to behind) ");
    rms_of(slice(2 * quarter, quarter), l_rms, r_rms);
    report("180° → 270° (behind to left)  ");
    rms_of(slice(3 * quarter, quarter), l_rms, r_rms);
    report("270° → 360° (left to front)   ");

    // 5. Full sweep mean — should be near-equal L/R since the orbit is
    // symmetric.
    float l_total = 0.0F, r_total = 0.0F;
    rms_of(stereo, l_total, r_total);
    std::printf("\n  full orbit RMS                  : L=%.3f  R=%.3f (diff=%.4f)\n",
                static_cast<double>(l_total),
                static_cast<double>(r_total),
                static_cast<double>(std::abs(l_total - r_total)));

    std::printf("[hello_positional_audio] done\n");
    return 0;
}
