// =============================================================================
// CHROMODYNAMIC — apps/editor/SplashAudioCue.hpp
// Phase 775 / FINALE-8 W1B H2 — 500 ms boot chime for the editor splash.
//
// Generates a short synthesized audio sting (oscillator-based, no sample
// asset required) and plays it once via cd::audio at t >= kChimeTriggerMs
// during the boot splash.
//
// Design
// ------
//   * Synthesis: 440 Hz sine wave ("A4") with a simple ADSR amplitude
//     envelope:
//       Attack  [0,   20ms)  — amplitude 0 -> 1
//       Decay   [20,  80ms)  — amplitude 1 -> 0.6
//       Sustain [80, 400ms)  — amplitude 0.6 (flat)
//       Release [400,500ms)  — amplitude 0.6 -> 0
//     The 500 ms clip is generated entirely in software at 48 kHz mono;
//     no file I/O and no platform thread are required.
//
//   * Playback: create_clip() + play() via the provided IAudioBackend*.
//     If the pointer is null (device unavailable) or if create_clip() /
//     play() return an error the cue is silently skipped — the splash
//     continues without audio.
//
//   * One-shot: trigger_if_needed() is idempotent after the first fire.
//     Call it every frame from the splash draw path.
//
// Thread safety: none — call from the main thread only.
//
// MOMENT: editor.exe boots, the CHROMODYNAMIC title fades in, a soft
// chime rings at 300 ms — the engine has a voice the moment it starts.
// =============================================================================
#pragma once

#include <cd/audio/IAudioBackend.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace cd::editor
{

// =============================================================================
// SplashAudioCue — one-shot 500 ms boot chime
// =============================================================================
class SplashAudioCue
{
public:
    // Timing constants (milliseconds).
    /// Splash elapsed time at which the chime fires.
    static constexpr double kChimeTriggerMs = 300.0;
    /// Chime duration in milliseconds.
    static constexpr double kChimeDurationMs = 500.0;

    // Synthesis parameters.
    static constexpr float         kFrequencyHz  = 440.0F;  ///< A4
    static constexpr std::uint32_t kSampleRate   = 48000U;
    static constexpr float         kPeakAmplitude= 0.25F;   ///< quiet — won't startle

    // ADSR phase boundaries (in samples at kSampleRate).
    static constexpr std::size_t kTotalSamples   = 24000U;  // 500 ms * 48 kHz
    static constexpr std::size_t kAttackEnd      =   960U;  //  20 ms
    static constexpr std::size_t kDecayEnd       =  3840U;  //  80 ms
    static constexpr std::size_t kSustainEnd     = 19200U;  // 400 ms
    // Release: [kSustainEnd, kTotalSamples)

    static constexpr float kSustainLevel = 0.6F;  // sustain amplitude relative to peak

    // -------------------------------------------------------------------------
    // synthesize() — generate the 500 ms PCM clip as a vector of mono floats.
    // Pure function: no side effects, easily unit-testable.
    // -------------------------------------------------------------------------
    [[nodiscard]] static std::vector<float> synthesize()
    {
        std::vector<float> samples(kTotalSamples, 0.0F);

        constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
        const float     phase_inc = kTwoPi * kFrequencyHz
                                    / static_cast<float>(kSampleRate);
        float phase = 0.0F;

        for (std::size_t i = 0; i < kTotalSamples; ++i)
        {
            // ADSR envelope
            float envelope = 0.0F;
            if (i < kAttackEnd)
            {
                // Attack: linear 0 -> 1
                envelope = static_cast<float>(i)
                           / static_cast<float>(kAttackEnd);
            }
            else if (i < kDecayEnd)
            {
                // Decay: linear 1 -> kSustainLevel
                const float t = static_cast<float>(i - kAttackEnd)
                                / static_cast<float>(kDecayEnd - kAttackEnd);
                envelope = 1.0F - t * (1.0F - kSustainLevel);
            }
            else if (i < kSustainEnd)
            {
                // Sustain: flat
                envelope = kSustainLevel;
            }
            else
            {
                // Release: linear kSustainLevel -> 0
                const float t = static_cast<float>(i - kSustainEnd)
                                / static_cast<float>(kTotalSamples - kSustainEnd);
                envelope = kSustainLevel * (1.0F - t);
            }

            samples[i] = kPeakAmplitude * envelope * std::sin(phase);
            phase += phase_inc;
            if (phase >= kTwoPi) { phase -= kTwoPi; }
        }

        return samples;
    }

    // -------------------------------------------------------------------------
    // trigger_if_needed() — call from the frame loop while the splash is active.
    //
    // @param elapsed_ms  Splash wall-clock elapsed time in milliseconds.
    // @param backend     Pointer to the audio backend; may be null (graceful skip).
    //
    // Returns true if the chime was fired this call, false otherwise.
    // -------------------------------------------------------------------------
    bool trigger_if_needed(double elapsed_ms, cd::audio::IAudioBackend* backend) noexcept
    {
        if (fired_) { return false; }
        if (elapsed_ms < kChimeTriggerMs) { return false; }

        fired_ = true;  // mark unconditionally — don't retry on failure

        if (backend == nullptr) { return false; }

        // Synthesize PCM data on first trigger.
        const std::vector<float> pcm = synthesize();

        cd::audio::ClipDesc desc {};
        desc.samples     = pcm;
        desc.channels    = 1U;
        desc.sample_rate = kSampleRate;

        auto clip_result = backend->create_clip(desc);
        if (!clip_result.has_value()) { return false; }

        clip_handle_ = *clip_result;
        auto voice_result = backend->play(clip_handle_, 1.0F, false);
        if (!voice_result.has_value())
        {
            backend->destroy_clip(clip_handle_);
            clip_handle_ = {};
            return false;
        }

        voice_handle_ = *voice_result;
        chimed_ = true;
        return true;
    }

    /// True if the chime has been successfully submitted for playback.
    [[nodiscard]] bool chimed() const noexcept { return chimed_; }

    /// True once trigger_if_needed() has been called with elapsed_ms >= kChimeTriggerMs
    /// (regardless of success).
    [[nodiscard]] bool fired() const noexcept { return fired_; }

private:
    bool fired_   { false };
    bool chimed_  { false };
    cd::audio::ClipHandle  clip_handle_  {};
    cd::audio::VoiceHandle voice_handle_ {};
};

}  // namespace cd::editor
