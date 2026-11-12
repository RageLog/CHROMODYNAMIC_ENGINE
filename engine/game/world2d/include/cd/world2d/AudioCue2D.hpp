// =============================================================================
// CHROMODYNAMIC — cd/world2d/AudioCue2D.hpp
// Milestone 3 — Procedural Retro Sound Waveform Synthesizer, 2D Equal-Power
// Stereo Panning, and Safe Null-Backend Dispatcher.
// =============================================================================
#pragma once

#include <cd/audio/IAudioBackend.hpp>
#include <cd/audio/PanLaw.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cd::world2d
{

enum class AudioCuePreset2D : std::uint8_t
{
    kJump       = 0,
    kCoin       = 1,
    kHit        = 2,
    kExplosion  = 3,
    kLaser      = 4,
    kPowerup    = 5,
    kBounce     = 6,

    // Aliases matching gameplay and explorer specifications
    kHeroJump        = kJump,
    kCollectibleGem  = kCoin,
    kHitImpact       = kHit,
    kLaserShoot      = kLaser,
    kJumpPadBounce   = kBounce,
    kEnemyStomp      = kExplosion,
};

using AudioCueKind2D = AudioCuePreset2D;

enum class WaveformType2D : std::uint8_t
{
    kSquare   = 0,
    kSine     = 1,
    kTriangle = 2,
    kSawtooth = 3,
    kNoise    = 4,
};

using WaveformKind2D = WaveformType2D;

struct RetroSynthParams2D
{
    WaveformType2D wave { WaveformType2D::kSquare };
    WaveformType2D waveform { WaveformType2D::kSquare };
    float          start_freq_hz { 440.0F };
    float          start_frequency_hz { 440.0F };
    float          end_freq_hz { 440.0F };
    float          end_frequency_hz { 440.0F };
    float          duration_sec { 0.20F };
    float          attack_time_sec { 0.01F };
    float          decay_time_sec { 0.19F };
    float          duty_cycle { 0.50F };
    float          noise_mix { 0.0F };
    float          vibrato_depth_hz { 0.0F };
    float          vibrato_freq_hz { 0.0F };
    std::uint32_t  sample_rate { 48000U };
};

using ProceduralWaveformConfig2D = RetroSynthParams2D;

struct AudioSpatialParams2D
{
    float min_distance { 1.0F };
    float max_distance { 25.0F };
    float pan_distance_scale { 12.0F };
    float rolloff_factor { 1.0F };
};

struct SpatialPanGains2D
{
    float gain_left { 1.0F };
    float gain_right { 1.0F };
};

// Procedural waveform synthesis functions
[[nodiscard]] std::vector<float> synthesize_waveform_samples(
    const RetroSynthParams2D& params,
    std::uint32_t             sample_rate = 0U);

[[nodiscard]] std::vector<float> synthesize_preset_samples(
    AudioCuePreset2D preset,
    std::uint32_t    sample_rate = 48000U);

// Spatial panning & attenuation math
[[nodiscard]] inline SpatialPanGains2D compute_spatial_panning_2d(
    cd::math::Vec2f listener_pos,
    float           stereo_width,
    cd::math::Vec2f emitter_pos) noexcept
{
    const float dx    = emitter_pos.x - listener_pos.x;
    const float width = std::max(1.0e-3F, stereo_width);
    const float p     = std::clamp(dx / width, -1.0F, 1.0F);

    float gl = 1.0F;
    float gr = 1.0F;
    if (p < 0.0F)
    {
        gl = 1.0F;
        gr = 1.0F + p;
    }
    else if (p > 0.0F)
    {
        gl = 1.0F - p;
        gr = 1.0F;
    }
    return SpatialPanGains2D { gl, gr };
}

[[nodiscard]] cd::audio::PanGains compute_2d_stereo_panning(
    cd::math::Vec2f             listener_pos,
    cd::math::Vec2f             emitter_pos,
    const AudioSpatialParams2D& params = {}) noexcept;

[[nodiscard]] float compute_2d_distance_attenuation(
    cd::math::Vec2f             listener_pos,
    cd::math::Vec2f             emitter_pos,
    const AudioSpatialParams2D& params = {}) noexcept;

class AudioCueSystem2D
{
public:
    explicit AudioCueSystem2D(cd::audio::IAudioBackend* backend = nullptr);
    ~AudioCueSystem2D();

    void set_backend(cd::audio::IAudioBackend* backend);
    [[nodiscard]] cd::audio::IAudioBackend* backend() const noexcept { return backend_; }

    void set_listener_position(cd::math::Vec2f pos) noexcept { listener_pos_ = pos; }
    [[nodiscard]] cd::math::Vec2f listener_position() const noexcept { return listener_pos_; }

    void set_spatial_params(const AudioSpatialParams2D& params) noexcept { spatial_params_ = params; }
    [[nodiscard]] const AudioSpatialParams2D& spatial_params() const noexcept { return spatial_params_; }

    // Preload preset clips into the audio backend
    void warm_up_presets();

    // Playback (always safe if backend == nullptr)
    cd::audio::VoiceHandle play_cue(
        AudioCuePreset2D preset,
        float            volume = 1.0F);

    cd::audio::VoiceHandle play_cue(
        AudioCuePreset2D preset,
        cd::math::Vec2f  emitter_pos,
        float            volume = 1.0F);

    cd::audio::VoiceHandle play_cue_at(
        AudioCuePreset2D preset,
        cd::math::Vec2f  emitter_pos,
        float            base_volume = 1.0F);

    void stop(cd::audio::VoiceHandle voice);
    void set_master_volume(float vol);

    // Telemetry & Test Assertions
    [[nodiscard]] std::uint32_t total_cues_dispatched() const noexcept { return total_cues_dispatched_; }
    [[nodiscard]] std::optional<AudioCuePreset2D> last_dispatched_cue() const noexcept { return last_dispatched_cue_; }
    void reset_telemetry() noexcept;

private:
    cd::audio::IAudioBackend* backend_ { nullptr };
    cd::math::Vec2f           listener_pos_ { 0.0F, 0.0F };
    AudioSpatialParams2D      spatial_params_ {};

    std::unordered_map<AudioCuePreset2D, cd::audio::ClipHandle> preset_clips_ {};
    std::uint32_t                   total_cues_dispatched_ { 0U };
    std::optional<AudioCuePreset2D> last_dispatched_cue_ {};
};

}  // namespace cd::world2d
