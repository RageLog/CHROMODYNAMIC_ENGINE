// =============================================================================
// CHROMODYNAMIC — samples/hello_audio_wasapi
//
// First real-time audio output sample. Tries to bring up a WASAPI
// shared-mode backend; if WASAPI isn't available (non-Windows or no
// audio device) falls back to the FileSink backend so the sample
// still proves the mixer code path end-to-end.
//
// Plays an A-major chord (A4 + C#5 + E5) for 1.5 s on the default
// audio device. Headless: exits when playback finishes — smoke
// harness compatible.
// =============================================================================
#include <cd/audio/FileSinkBackend.hpp>
#include <cd/audio/WasapiBackend.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{

constexpr std::uint32_t kClipSampleRate = 48000;
constexpr float kDurationSec = 1.5F;

std::vector<float> synth_sine(float freq_hz, std::uint32_t frames, float amp = 0.4F)
{
    constexpr float kPi = 3.14159265358979F;
    std::vector<float> out(frames);
    const float w = 2.0F * kPi * freq_hz / static_cast<float>(kClipSampleRate);
    for (std::uint32_t i = 0; i < frames; ++i)
        out[i] = std::sin(static_cast<float>(i) * w) * amp;
    return out;
}

}  // namespace

int main()
{
    std::printf("=== hello_audio_wasapi — real-time A-major chord ===\n");

    std::unique_ptr<cd::audio::IAudioBackend> backend = cd::audio::make_wasapi_audio_backend();
    bool real_audio = backend != nullptr;
    if (!real_audio)
    {
        std::printf("WASAPI unavailable, falling back to FileSink (no audible output)\n");
        backend = cd::audio::make_file_sink_audio_backend(kClipSampleRate, 2);
    }
    else
    {
        std::printf("WASAPI shared-mode render thread started\n");
    }

    const auto frames = static_cast<std::uint32_t>(static_cast<float>(kClipSampleRate) * kDurationSec);
    const auto a4 = synth_sine(440.00F, frames, 0.50F);
    const auto cs5 = synth_sine(554.37F, frames, 0.35F);
    const auto e5 = synth_sine(659.26F, frames, 0.30F);

    cd::audio::ClipDesc desc {};
    desc.channels = 1;
    desc.sample_rate = kClipSampleRate;

    desc.samples = std::span<const float> { a4.data(), a4.size() };
    auto h_a4 = backend->create_clip(desc);
    desc.samples = std::span<const float> { cs5.data(), cs5.size() };
    auto h_cs5 = backend->create_clip(desc);
    desc.samples = std::span<const float> { e5.data(), e5.size() };
    auto h_e5 = backend->create_clip(desc);
    if (!h_a4 || !h_cs5 || !h_e5)
        return 1;

    (void)backend->play(*h_a4, 0.6F, false);
    (void)backend->play(*h_cs5, 0.4F, false);
    (void)backend->play(*h_e5, 0.3F, false);

    if (real_audio)
    {
        // WASAPI mixes continuously on its render thread. We just sleep
        // the wall-clock duration of the clip and let the OS push the
        // samples to the audio device. Add a small tail for the last
        // buffer to drain.
        std::this_thread::sleep_for(std::chrono::milliseconds {
            static_cast<int>(kDurationSec * 1000.0F + 200.0F) });
        std::printf("[hello_audio_wasapi] playback complete (audible on the default device)\n");
    }
    else
    {
        // FileSink: render explicitly and dump WAV so the sample still
        // produces an auditioning artifact in the no-WASAPI scenario.
        if (auto* fs = dynamic_cast<cd::audio::IFileSinkBackend*>(backend.get()))
        {
            fs->render(frames);
            (void)fs->write_wav("hello_audio_wasapi_fallback.wav");
            std::printf("[hello_audio_wasapi] wrote hello_audio_wasapi_fallback.wav\n");
        }
    }
    return 0;
}
