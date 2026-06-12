// =============================================================================
// HelloAudio.hpp
// -----------------------------------------------------------------------------
// hello_engine-local audio chain aggregate + boot init. Lifted out of
// main() in Marathon Run 11 phase N11.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/audio/Compressor.hpp>
#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/Limiter.hpp>
#include <cd/audio/LowPass.hpp>
#include <cd/audio/Mixer.hpp>
#include <cd/audio/SimpleReverb.hpp>
#include <cd/audio/WasapiBackend.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <vector>

namespace cd_sample {

inline constexpr std::uint32_t kAudioSampleRate = 48000u;
inline constexpr std::size_t kAudioRingFrames =
    static_cast<std::size_t>(kAudioSampleRate) * 5u;

struct AudioState
{
    cd::audio::Mixer<2>       bus;
    cd::audio::Compressor     comp;
    cd::audio::SimpleReverb   reverb;
    cd::audio::LowPass        lowpass;
    cd::audio::Limiter        limiter;
    std::uint64_t             sample_t        { 0 };
    bool                      muted           { true };
    float                     peak_window     { 0.0F };
    float                     comp_db_window  { 0.0F };
    float                     limiter_gain_min{ 1.0F };
    std::deque<float>         meter_history;
    std::vector<std::int16_t> ring;
    std::size_t               ring_write      { 0 };
    std::uint64_t             total_written   { 0 };
    std::unique_ptr<cd::audio::IAudioBackend> backend;
    cd::audio::ClipHandle     live_clip       {};
    cd::audio::VoiceHandle    live_voice      {};
    bool                      live_ok         { false };
};

template <typename SquareFn, typename NoiseFn>
inline void
init_audio(AudioState& a, SquareFn square_wave, NoiseFn burst_noise)
{
    a.bus.set_gain(0, 0.6F);
    a.bus.set_gain(1, 0.7F);
    a.comp.prepare(
        static_cast<float>(kAudioSampleRate),
        /*threshold=*/0.40F,
        /*ratio=*/6.0F,
        /*attack_sec=*/0.004F,
        /*release_sec=*/0.080F);
    a.reverb.prepare(kAudioSampleRate / 8u);
    a.reverb.set_feedback(0.35F);
    a.lowpass.prepare(static_cast<float>(kAudioSampleRate),
                      /*cutoff_hz=*/6500.0F);
    a.limiter.prepare(
        static_cast<float>(kAudioSampleRate),
        /*threshold=*/0.92F,
        /*attack_sec=*/0.0002F,
        /*release_sec=*/0.040F);

    a.ring.assign(kAudioRingFrames, 0);

    a.backend = cd::audio::make_wasapi_audio_backend();
    a.live_ok = (a.backend != nullptr);
    if (!a.live_ok)
        return;

    constexpr std::size_t kPreRenderFrames =
        static_cast<std::size_t>(kAudioSampleRate) * 2u;
    std::vector<float> live_buf(kPreRenderFrames, 0.0F);
    cd::audio::Mixer<2> m2;
    m2.set_gain(0, 0.6F);
    m2.set_gain(1, 0.7F);
    cd::audio::Compressor c2;
    c2.prepare(static_cast<float>(kAudioSampleRate),
               0.40F, 6.0F, 0.004F, 0.080F);
    cd::audio::SimpleReverb r2;
    r2.prepare(kAudioSampleRate / 8u);
    r2.set_feedback(0.35F);
    cd::audio::LowPass l2;
    l2.prepare(static_cast<float>(kAudioSampleRate), 6500.0F);
    cd::audio::Limiter L2;
    L2.prepare(static_cast<float>(kAudioSampleRate),
               0.92F, 0.0002F, 0.040F);
    for (std::size_t i = 0; i < kPreRenderFrames; ++i)
    {
        m2.mix(0, square_wave(i, 440.0F));
        m2.mix(1, burst_noise(i));
        float x = m2.pull();
        x = c2.process(x);
        const float wet = r2.process(x);
        x = 0.75F * x + 0.20F * wet;
        x = l2.process(x);
        x = L2.process(x);
        x = std::min(x, 1.0F);
        x = std::max(x, -1.0F);
        live_buf[i] = x * 0.7F;
    }
    cd::audio::ClipDesc cd_desc {};
    cd_desc.samples = std::span<const float>(live_buf);
    cd_desc.channels = 1;
    cd_desc.sample_rate = kAudioSampleRate;
    auto clip_r = a.backend->create_clip(cd_desc);
    if (!clip_r.has_value())
        return;
    a.live_clip = *clip_r;
    auto voice_r = a.backend->play(a.live_clip, /*volume=*/0.0F, /*looping=*/true);
    if (voice_r.has_value())
        a.live_voice = *voice_r;
}

}  // namespace cd_sample
