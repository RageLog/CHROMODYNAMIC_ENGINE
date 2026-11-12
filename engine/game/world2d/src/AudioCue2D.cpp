// =============================================================================
// CHROMODYNAMIC — cd/world2d/AudioCue2D.cpp
// Milestone 3 — Procedural Retro Sound Waveform Synthesizer Implementation.
// =============================================================================
#include <cd/world2d/AudioCue2D.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace cd::world2d
{

namespace
{

constexpr float kTwoPi = 6.283185307179586F;

[[nodiscard]] float pseudo_random_noise(std::uint32_t seed) noexcept
{
    // Fast 32-bit integer hash to float in [-1.0, 1.0]
    seed = (seed ^ 61U) ^ (seed >> 16U);
    seed *= 9U;
    seed = seed ^ (seed >> 4U);
    seed *= 0x27d4eb2dU;
    seed = seed ^ (seed >> 15U);
    const auto u = static_cast<float>(seed & 0x00FFFFFFU) / static_cast<float>(0x00FFFFFFU);
    return u * 2.0F - 1.0F;
}

}  // namespace

std::vector<float> synthesize_waveform_samples(
    const RetroSynthParams2D& params,
    std::uint32_t             sample_rate)
{
    const std::uint32_t srate = (sample_rate > 0U) ? sample_rate : params.sample_rate;
    const std::uint32_t actual_sr = (srate > 0U) ? srate : 48000U;

    const auto num_samples = static_cast<std::size_t>(
        std::max(1.0F, std::ceil(std::max(0.01F, params.duration_sec) * static_cast<float>(actual_sr))));

    float f0 = (params.start_freq_hz != 440.0F) ? params.start_freq_hz : params.start_frequency_hz;
    float f1 = (params.end_freq_hz != 440.0F) ? params.end_freq_hz : params.end_frequency_hz;
    if (!std::isfinite(f0) || f0 <= 0.0F)
    {
        f0 = 440.0F;
    }
    if (!std::isfinite(f1) || f1 <= 0.0F)
    {
        f1 = 440.0F;
    }
    const WaveformType2D wave = (params.wave != WaveformType2D::kSquare) ? params.wave : params.waveform;

    const float duration = std::max(1.0e-4F, params.duration_sec);
    const float att = std::max(1.0e-4F, params.attack_time_sec);
    const float dec = std::max(1.0e-4F, params.decay_time_sec);
    const float duty = std::clamp(params.duty_cycle, 0.05F, 0.95F);
    const float noise_mix = std::clamp(params.noise_mix, 0.0F, 1.0F);

    std::vector<float> samples(num_samples, 0.0F);

    for (std::size_t n = 0U; n < num_samples; ++n)
    {
        const float t = static_cast<float>(n) / static_cast<float>(actual_sr);

        // Linear frequency sweep phase
        float phase = kTwoPi * (f0 * t + 0.5F * ((f1 - f0) / duration) * t * t);
        if (params.vibrato_depth_hz > 0.0F && params.vibrato_freq_hz > 0.0F)
        {
            phase += (params.vibrato_depth_hz / params.vibrato_freq_hz) * std::sin(kTwoPi * params.vibrato_freq_hz * t);
        }

        float theta = std::fmod(phase / kTwoPi, 1.0F);
        if (theta < 0.0F)
        {
            theta += 1.0F;
        }

        float wave_val = 0.0F;
        switch (wave)
        {
            case WaveformType2D::kSquare:
                wave_val = (theta < duty) ? 1.0F : -1.0F;
                break;
            case WaveformType2D::kSine:
                wave_val = std::sin(kTwoPi * theta);
                break;
            case WaveformType2D::kTriangle:
                wave_val = 1.0F - 4.0F * std::abs(theta - 0.5F);
                break;
            case WaveformType2D::kSawtooth:
                wave_val = 2.0F * theta - 1.0F;
                break;
            case WaveformType2D::kNoise:
                wave_val = pseudo_random_noise(static_cast<std::uint32_t>(n) * 1103515245U);
                break;
        }

        if (noise_mix > 0.0F)
        {
            const float noise = pseudo_random_noise(static_cast<std::uint32_t>(n) * 1664525U + 1013904223U);
            wave_val = wave_val * (1.0F - noise_mix) + noise * noise_mix;
        }

        // Linear Attack / Decay envelope
        float env = 1.0F;
        if (t < att)
        {
            env = t / att;
        }
        else
        {
            env = std::max(0.0F, 1.0F - (t - att) / dec);
        }

        const float val = std::clamp(wave_val * env, -1.0F, 1.0F);
        samples[n] = std::isnan(val) ? 0.0F : val;
    }

    return samples;
}

std::vector<float> synthesize_preset_samples(
    AudioCuePreset2D preset,
    std::uint32_t    sample_rate)
{
    RetroSynthParams2D p {};
    p.sample_rate = sample_rate;

    switch (preset)
    {
        case AudioCuePreset2D::kJump:
            p.wave            = WaveformType2D::kSquare;
            p.start_freq_hz   = 160.0F;
            p.end_freq_hz     = 640.0F;
            p.duration_sec    = 0.16F;
            p.attack_time_sec = 0.01F;
            p.decay_time_sec  = 0.15F;
            p.duty_cycle      = 0.50F;
            return synthesize_waveform_samples(p, sample_rate);

        case AudioCuePreset2D::kCoin:
        {
            // Two-tone arpeggio: 987 Hz -> 1318 Hz
            p.wave            = WaveformType2D::kSine;
            p.start_freq_hz   = 987.0F;
            p.end_freq_hz     = 987.0F;
            p.duration_sec    = 0.07F;
            p.attack_time_sec = 0.005F;
            p.decay_time_sec  = 0.065F;
            auto part1 = synthesize_waveform_samples(p, sample_rate);

            p.start_freq_hz   = 1318.0F;
            p.end_freq_hz     = 1318.0F;
            p.duration_sec    = 0.20F;
            p.attack_time_sec = 0.005F;
            p.decay_time_sec  = 0.195F;
            auto part2 = synthesize_waveform_samples(p, sample_rate);

            part1.insert(part1.end(), part2.begin(), part2.end());
            return part1;
        }

        case AudioCuePreset2D::kHit:
            p.wave            = WaveformType2D::kSawtooth;
            p.start_freq_hz   = 320.0F;
            p.end_freq_hz     = 70.0F;
            p.duration_sec    = 0.12F;
            p.attack_time_sec = 0.005F;
            p.decay_time_sec  = 0.115F;
            p.noise_mix       = 0.35F;
            return synthesize_waveform_samples(p, sample_rate);

        case AudioCuePreset2D::kExplosion:
            p.wave            = WaveformType2D::kNoise;
            p.start_freq_hz   = 120.0F;
            p.end_freq_hz     = 30.0F;
            p.duration_sec    = 0.45F;
            p.attack_time_sec = 0.01F;
            p.decay_time_sec  = 0.44F;
            p.noise_mix       = 0.95F;
            return synthesize_waveform_samples(p, sample_rate);

        case AudioCuePreset2D::kLaser:
            p.wave            = WaveformType2D::kSawtooth;
            p.start_freq_hz   = 1100.0F;
            p.end_freq_hz     = 120.0F;
            p.duration_sec    = 0.18F;
            p.attack_time_sec = 0.005F;
            p.decay_time_sec  = 0.175F;
            return synthesize_waveform_samples(p, sample_rate);

        case AudioCuePreset2D::kPowerup:
        {
            // 4-tone ascending fanfare (C5, E5, G5, C6)
            std::vector<float> full;
            constexpr std::array<float, 4> notes = { 523.25F, 659.25F, 783.99F, 1046.50F };
            for (float note : notes)
            {
                p.wave            = WaveformType2D::kSine;
                p.start_freq_hz   = note;
                p.end_freq_hz     = note;
                p.duration_sec    = 0.065F;
                p.attack_time_sec = 0.005F;
                p.decay_time_sec  = 0.060F;
                auto tone = synthesize_waveform_samples(p, sample_rate);
                full.insert(full.end(), tone.begin(), tone.end());
            }
            return full;
        }

        case AudioCuePreset2D::kBounce:
            p.wave            = WaveformType2D::kSine;
            p.start_freq_hz   = 200.0F;
            p.end_freq_hz     = 460.0F;
            p.duration_sec    = 0.18F;
            p.attack_time_sec = 0.01F;
            p.decay_time_sec  = 0.17F;
            return synthesize_waveform_samples(p, sample_rate);
    }

    return synthesize_waveform_samples(p, sample_rate);
}

cd::audio::PanGains compute_2d_stereo_panning(
    cd::math::Vec2f             listener_pos,
    cd::math::Vec2f             emitter_pos,
    const AudioSpatialParams2D& params) noexcept
{
    const float dx = emitter_pos.x - listener_pos.x;
    const float p  = std::clamp(dx / std::max(1.0e-3F, params.pan_distance_scale), -1.0F, 1.0F);
    return cd::audio::constant_power_pan(p);
}

float compute_2d_distance_attenuation(
    cd::math::Vec2f             listener_pos,
    cd::math::Vec2f             emitter_pos,
    const AudioSpatialParams2D& params) noexcept
{
    const float dx   = emitter_pos.x - listener_pos.x;
    const float dy   = emitter_pos.y - listener_pos.y;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist <= params.min_distance)
    {
        return 1.0F;
    }
    if (dist >= params.max_distance)
    {
        return 0.0F;
    }
    const float norm = (dist - params.min_distance) / (params.max_distance - params.min_distance);
    return std::pow(std::max(0.0F, 1.0F - norm), std::max(0.1F, params.rolloff_factor));
}

AudioCueSystem2D::AudioCueSystem2D(cd::audio::IAudioBackend* backend)
    : backend_(backend)
{
    if (backend_ != nullptr)
    {
        warm_up_presets();
    }
}

AudioCueSystem2D::~AudioCueSystem2D()
{
    if (backend_ != nullptr)
    {
        for (const auto& [preset, clip] : preset_clips_)
        {
            backend_->destroy_clip(clip);
        }
    }
    preset_clips_.clear();
}

void AudioCueSystem2D::set_backend(cd::audio::IAudioBackend* backend)
{
    if (backend_ != backend)
    {
        if (backend_ != nullptr)
        {
            for (const auto& [preset, clip] : preset_clips_)
            {
                backend_->destroy_clip(clip);
            }
            preset_clips_.clear();
        }
        backend_ = backend;
        if (backend_ != nullptr)
        {
            warm_up_presets();
        }
    }
}

void AudioCueSystem2D::warm_up_presets()
{
    if (backend_ == nullptr)
    {
        return;
    }

    constexpr std::array<AudioCuePreset2D, 7> presets = {
        AudioCuePreset2D::kJump,
        AudioCuePreset2D::kCoin,
        AudioCuePreset2D::kHit,
        AudioCuePreset2D::kExplosion,
        AudioCuePreset2D::kLaser,
        AudioCuePreset2D::kPowerup,
        AudioCuePreset2D::kBounce,
    };

    for (const auto p : presets)
    {
        if (!preset_clips_.contains(p))
        {
            const auto samples = synthesize_preset_samples(p);
            const cd::audio::ClipDesc desc {
                .samples     = samples,
                .channels    = 1U,
                .sample_rate = 48000U,
            };
            const auto res = backend_->create_clip(desc);
            if (res.has_value())
            {
                preset_clips_[p] = res.value();
            }
        }
    }
}

cd::audio::VoiceHandle AudioCueSystem2D::play_cue(AudioCuePreset2D preset, float volume)
{
    ++total_cues_dispatched_;
    last_dispatched_cue_ = preset;

    if (backend_ == nullptr)
    {
        return cd::audio::VoiceHandle {};
    }

    auto it = preset_clips_.find(preset);
    if (it == preset_clips_.end())
    {
        warm_up_presets();
        it = preset_clips_.find(preset);
        if (it == preset_clips_.end())
        {
            return cd::audio::VoiceHandle {};
        }
    }

    const auto v_res = backend_->play(it->second, volume, false);
    return v_res.value_or(cd::audio::VoiceHandle {});
}

cd::audio::VoiceHandle AudioCueSystem2D::play_cue(
    AudioCuePreset2D preset,
    cd::math::Vec2f  emitter_pos,
    float            volume)
{
    return play_cue_at(preset, emitter_pos, volume);
}

cd::audio::VoiceHandle AudioCueSystem2D::play_cue_at(
    AudioCuePreset2D preset,
    cd::math::Vec2f  emitter_pos,
    float            base_volume)
{
    const float atten = compute_2d_distance_attenuation(listener_pos_, emitter_pos, spatial_params_);
    const float vol   = base_volume * atten;
    return play_cue(preset, vol);
}

void AudioCueSystem2D::stop(cd::audio::VoiceHandle voice)
{
    if (backend_ != nullptr && !voice.is_null())
    {
        backend_->stop(voice);
    }
}

void AudioCueSystem2D::set_master_volume(float vol)
{
    if (backend_ != nullptr)
    {
        backend_->set_master_volume(vol);
    }
}

void AudioCueSystem2D::reset_telemetry() noexcept
{
    total_cues_dispatched_ = 0U;
    last_dispatched_cue_   = std::nullopt;
}

}  // namespace cd::world2d
