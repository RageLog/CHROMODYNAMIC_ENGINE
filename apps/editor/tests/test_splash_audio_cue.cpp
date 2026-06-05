// =============================================================================
// CHROMODYNAMIC — apps/editor/tests/test_splash_audio_cue.cpp
// Phase 775 / FINALE-8 W1B H2 — Synthetic tests for the SplashAudioCue.
//
// All tests use the null audio backend (make_null_audio_backend) so they
// run correctly in headless CI without a real audio device.
//
// Test suite: SplashAudioCue
//
//   Synthesize_NonZeroAtExpectedTiming
//     The synthesized PCM vector has kTotalSamples entries and the samples
//     in the sustain region (expected peak amplitude) are non-zero.
//
//   Synthesize_AttackSamplesRampUp
//     The envelope during the attack region ([0, kAttackEnd)) rises
//     monotonically from near-zero toward kPeakAmplitude.
//
//   Synthesize_ReleaseSamplesDecayToZero
//     The last sample in the clip is near zero (release has decayed away).
//
//   TriggerBeforeThresholdDoesNotFire
//     trigger_if_needed() returns false and fired() stays false when
//     elapsed_ms < kChimeTriggerMs.
//
//   TriggerAtThresholdFires
//     trigger_if_needed() returns true on the first call at or after
//     kChimeTriggerMs and creates a clip + voice on the null backend.
//
//   TriggerIsIdempotentAfterFirstFire
//     Subsequent calls to trigger_if_needed() return false even when
//     elapsed_ms > kChimeTriggerMs.
//
//   TriggerWithNullBackendSetsFireButNotChimed
//     When backend == nullptr the cue sets fired() == true but
//     chimed() == false (graceful skip).
// =============================================================================

#include "../SplashAudioCue.hpp"
#include <cd/audio/IAudioBackend.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Synthesize_NonZeroAtExpectedTiming
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, Synthesize_NonZeroAtExpectedTiming)
{
    using Cue = cd::editor::SplashAudioCue;

    const std::vector<float> pcm = Cue::synthesize();

    ASSERT_EQ(pcm.size(), Cue::kTotalSamples);

    // Sustain region is [kDecayEnd, kSustainEnd).  The sine oscillates so
    // some individual samples may pass through zero; check that at least
    // one sample in the sustain band is non-zero with meaningful amplitude.
    float sustain_peak = 0.0F;
    for (std::size_t i = Cue::kDecayEnd; i < Cue::kSustainEnd; ++i)
        sustain_peak = std::max(sustain_peak, std::fabs(pcm[i]));

    EXPECT_GT(sustain_peak, 0.0F)
        << "sustain region should contain non-zero amplitude samples";

    // The sustain amplitude should be close to kPeakAmplitude * kSustainLevel.
    const float expected_peak = Cue::kPeakAmplitude * Cue::kSustainLevel;
    EXPECT_NEAR(sustain_peak, expected_peak, expected_peak * 0.05F)
        << "sustain peak should be within 5% of kPeakAmplitude * kSustainLevel";
}

// ---------------------------------------------------------------------------
// Synthesize_AttackSamplesRampUp
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, Synthesize_AttackSamplesRampUp)
{
    using Cue = cd::editor::SplashAudioCue;
    const std::vector<float> pcm = Cue::synthesize();

    // Compare absolute amplitudes at the very start vs. near the end of attack.
    // The sine oscillation means we cannot compare consecutive samples; compare
    // the max absolute value in the first tenth of the attack against the last
    // tenth — it should be larger at the end.
    const std::size_t tenth = Cue::kAttackEnd / 10U;

    float early_max = 0.0F;
    for (std::size_t i = 0; i < tenth; ++i)
        early_max = std::max(early_max, std::fabs(pcm[i]));

    float late_max = 0.0F;
    for (std::size_t i = Cue::kAttackEnd - tenth; i < Cue::kAttackEnd; ++i)
        late_max = std::max(late_max, std::fabs(pcm[i]));

    EXPECT_GT(late_max, early_max)
        << "amplitude envelope should be larger near end of attack than at start";
}

// ---------------------------------------------------------------------------
// Synthesize_ReleaseSamplesDecayToZero
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, Synthesize_ReleaseSamplesDecayToZero)
{
    using Cue = cd::editor::SplashAudioCue;
    const std::vector<float> pcm = Cue::synthesize();

    // The very last sample should be near zero (release fully decayed).
    const float last = std::fabs(pcm.back());
    // The release factor at the final sample index (kTotalSamples - 1):
    //   t = (kTotalSamples - 1 - kSustainEnd) / (kTotalSamples - kSustainEnd)
    // which approaches 1 as i -> kTotalSamples. The amplitude is
    //   kPeakAmplitude * kSustainLevel * (1 - t), approaching 0.
    // Allow a tolerance of one full peak level (sine may land at any phase).
    EXPECT_LT(last, Cue::kPeakAmplitude * Cue::kSustainLevel * 0.1F + 1e-6F)
        << "last sample should be near zero (release fully decayed)";
}

// ---------------------------------------------------------------------------
// TriggerBeforeThresholdDoesNotFire
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, TriggerBeforeThresholdDoesNotFire)
{
    using Cue = cd::editor::SplashAudioCue;

    auto backend = cd::audio::make_null_audio_backend();
    Cue cue {};

    const bool fired = cue.trigger_if_needed(
        Cue::kChimeTriggerMs - 1.0, backend.get());

    EXPECT_FALSE(fired)    << "should not fire before kChimeTriggerMs";
    EXPECT_FALSE(cue.fired()) << "fired() should stay false";
    EXPECT_FALSE(cue.chimed());
    EXPECT_EQ(backend->clip_count(), 0U)
        << "no clip should be created before threshold";
}

// ---------------------------------------------------------------------------
// TriggerAtThresholdFires
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, TriggerAtThresholdFires)
{
    using Cue = cd::editor::SplashAudioCue;

    auto backend = cd::audio::make_null_audio_backend();
    Cue cue {};

    const bool fired = cue.trigger_if_needed(
        Cue::kChimeTriggerMs, backend.get());

    EXPECT_TRUE(fired)     << "should return true on first trigger at threshold";
    EXPECT_TRUE(cue.fired())  << "fired() should be true";
    EXPECT_TRUE(cue.chimed()) << "chimed() should be true (null backend accepts clip+play)";

    // Null backend stores the clip and voice.
    EXPECT_EQ(backend->clip_count(), 1U)
        << "one clip should be created on trigger";
    EXPECT_EQ(backend->voice_count(), 1U)
        << "one voice should be playing";
}

// ---------------------------------------------------------------------------
// TriggerIsIdempotentAfterFirstFire
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, TriggerIsIdempotentAfterFirstFire)
{
    using Cue = cd::editor::SplashAudioCue;

    auto backend = cd::audio::make_null_audio_backend();
    Cue cue {};

    // First call fires.
    cue.trigger_if_needed(Cue::kChimeTriggerMs, backend.get());
    ASSERT_TRUE(cue.fired());

    // Subsequent calls should be no-ops.
    const bool fired2 = cue.trigger_if_needed(
        Cue::kChimeTriggerMs + 100.0, backend.get());
    const bool fired3 = cue.trigger_if_needed(
        Cue::kChimeTriggerMs + 200.0, backend.get());

    EXPECT_FALSE(fired2) << "second call should return false (idempotent)";
    EXPECT_FALSE(fired3) << "third call should return false (idempotent)";

    // No additional clips or voices should be created.
    EXPECT_EQ(backend->clip_count(), 1U)
        << "only one clip should exist after repeated triggers";
    EXPECT_EQ(backend->voice_count(), 1U)
        << "only one voice should exist after repeated triggers";
}

// ---------------------------------------------------------------------------
// TriggerWithNullBackendSetsFireButNotChimed
// ---------------------------------------------------------------------------
TEST(SplashAudioCue, TriggerWithNullBackendSetsFireButNotChimed)
{
    using Cue = cd::editor::SplashAudioCue;

    Cue cue {};

    // Pass nullptr — device unavailable path.
    const bool fired = cue.trigger_if_needed(Cue::kChimeTriggerMs, nullptr);

    EXPECT_FALSE(fired)    << "returns false when backend is null";
    EXPECT_TRUE(cue.fired())  << "fired() should be true (attempted)";
    EXPECT_FALSE(cue.chimed()) << "chimed() should stay false (no backend)";
}

}  // namespace
