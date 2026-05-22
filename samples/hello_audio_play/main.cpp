// =============================================================================
// CHROMODYNAMIC — samples/hello_audio_play
//
// First end-to-end audio playback sample. Synthesises three sine clips
// (A4 = 440 Hz, C#5 = 554.37 Hz, E5 = 659.26 Hz — an A major chord),
// registers them with a cd::audio::FileSinkBackend, starts three voices
// with different volumes, renders 1 second of audio, and writes the
// mixed output to `hello_audio_play_out.wav`.
//
// Plays back in any system audio player. No platform-output backend
// (WASAPI / CoreAudio / ALSA) yet — see Phase 4 closure ADR S4.5.b.
// =============================================================================
#include <cd/audio/FileSinkBackend.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint32_t kChannels = 2;
constexpr float kDurationSec = 1.0F;

std::vector<float> synth_sine(float frequency_hz, std::uint32_t frames, float amplitude = 0.5F)
{
    constexpr float kPi = 3.14159265358979F;
    std::vector<float> out(static_cast<std::size_t>(frames));
    const float w = 2.0F * kPi * frequency_hz / static_cast<float>(kSampleRate);
    for (std::uint32_t i = 0; i < frames; ++i)
        out[i] = std::sin(static_cast<float>(i) * w) * amplitude;
    return out;
}

}  // namespace

int main()
{
    std::printf("=== hello_audio_play — A-major chord -> WAV via cd::audio ===\n");

    auto backend = cd::audio::make_file_sink_audio_backend(kSampleRate, kChannels);

    const auto frames = static_cast<std::uint32_t>(static_cast<float>(kSampleRate) * kDurationSec);

    // Three sine clips — mono so the mixer broadcasts each across L/R.
    cd::audio::ClipDesc desc {};
    desc.channels = 1;
    desc.sample_rate = kSampleRate;

    const auto a4  = synth_sine(440.00F, frames);
    desc.samples = std::span<const float> { a4.data(), a4.size() };
    auto a4_h = backend->create_clip(desc);
    if (!a4_h) { std::printf("create_clip(A4) failed\n"); return 1; }

    const auto cs5 = synth_sine(554.37F, frames);
    desc.samples = std::span<const float> { cs5.data(), cs5.size() };
    auto cs5_h = backend->create_clip(desc);
    if (!cs5_h) { std::printf("create_clip(C#5) failed\n"); return 2; }

    const auto e5  = synth_sine(659.26F, frames);
    desc.samples = std::span<const float> { e5.data(), e5.size() };
    auto e5_h = backend->create_clip(desc);
    if (!e5_h) { std::printf("create_clip(E5) failed\n"); return 3; }

    // Play with stacked volumes — root loudest.
    if (!backend->play(*a4_h,  0.6F, /*looping=*/false)) return 4;
    if (!backend->play(*cs5_h, 0.4F, /*looping=*/false)) return 5;
    if (!backend->play(*e5_h,  0.3F, /*looping=*/false)) return 6;
    std::printf("voices=%zu, clips=%zu\n", backend->voice_count(), backend->clip_count());

    // Render the chord.
    backend->render(frames);
    std::printf("rendered %u frames @ %u Hz = %.2fs\n",
                backend->rendered_frames(),
                kSampleRate,
                static_cast<double>(backend->rendered_frames()) / static_cast<double>(kSampleRate));

    // Write the mixed WAV to disk.
    constexpr const char* kOut = "hello_audio_play_out.wav";
    auto wr = backend->write_wav(kOut);
    if (!wr)
    {
        std::printf("write_wav failed: %.*s\n",
                    static_cast<int>(wr.error().message.size()), wr.error().message.data());
        return 7;
    }
    std::printf("wrote -> %s\n", kOut);
    std::printf("[hello_audio_play] done (audible in any WAV player)\n");
    return 0;
}
