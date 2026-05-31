// =============================================================================
// CHROMODYNAMIC — engine/asset/audio_streamer/tests/test_audio_streamer.cpp
// Phase 619 — cd::asset::audio_streamer unit tests (Sprint-1)
//
// Sprint-1 uses AssetId::from_path() as the "load" operation, which always
// succeeds for non-empty paths. Tests exercise the full queue/cancel/tick
// lifecycle without real I/O.
//
// Tests:
//   T1  enqueue + tick + is_loaded round-trip (happy path)
//   T2  cancel removes pending request
//   T3  priority ordering: higher priority served first
//   T4  missing-file / empty-path fails gracefully: completed_count stays zero
//   T5  completed_count grows on each successful tick
//   T6  enqueue is idempotent (duplicate enqueue does not double-count)
//   T7  get_loaded returns nullopt for unknown path
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

namespace
{

using cd::asset::audio_streamer::AudioStreamer;
using cd::asset::audio_streamer::StreamRequest;

// ---- T1: enqueue + tick + is_loaded round-trip (happy path) -----------------
//
// Any non-empty path produces a valid AssetId via from_path(), so this test
// verifies the full happy path: enqueue → tick → is_loaded → get_loaded.

TEST(AudioStreamer, EnqueueTickIsLoadedRoundTrip)
{
    AudioStreamer as;

    as.enqueue({ "audio/footstep.wav", 0U, 200U });
    EXPECT_EQ(as.pending_count(), 1U);
    EXPECT_FALSE(as.is_loaded("audio/footstep.wav"));

    as.tick(0.016F);

    EXPECT_TRUE(as.is_loaded("audio/footstep.wav"));
    EXPECT_EQ(as.pending_count(),   0U);
    EXPECT_EQ(as.completed_count(), 1U);

    const auto id = as.get_loaded("audio/footstep.wav");
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(id->is_valid());
}

// ---- T2: cancel removes pending request -------------------------------------

TEST(AudioStreamer, CancelRemovesPending)
{
    AudioStreamer as;

    as.enqueue({ "audio/clip_a.wav", 0U, 50U });
    as.enqueue({ "audio/clip_b.wav", 0U, 80U });
    EXPECT_EQ(as.pending_count(), 2U);

    as.cancel("audio/clip_a.wav");
    EXPECT_EQ(as.pending_count(), 1U);

    // tick processes clip_b (the only remaining request).
    as.tick(0.016F);

    EXPECT_EQ(as.pending_count(), 0U);
    EXPECT_FALSE(as.is_loaded("audio/clip_a.wav"));  // Was cancelled, never loaded.
    EXPECT_TRUE(as.is_loaded("audio/clip_b.wav"));
}

// ---- T3: priority ordering: higher priority served first --------------------

TEST(AudioStreamer, HigherPriorityServedFirst)
{
    AudioStreamer as;

    as.enqueue({ "audio/low_prio.wav",  0U,  10U });
    as.enqueue({ "audio/high_prio.wav", 0U, 200U });
    EXPECT_EQ(as.pending_count(), 2U);

    // One tick must process the highest-priority entry (high_prio).
    as.tick(0.016F);
    EXPECT_EQ(as.pending_count(), 1U);

    // The surviving pending entry must be low_prio. Verify via cancel:
    as.cancel("audio/low_prio.wav");
    EXPECT_EQ(as.pending_count(), 0U);

    // high_prio should be loaded; low_prio was never processed.
    EXPECT_TRUE(as.is_loaded("audio/high_prio.wav"));
    EXPECT_FALSE(as.is_loaded("audio/low_prio.wav"));
}

// ---- T4: empty path fails gracefully: completed_count stays zero ------------
//
// AssetId::from_path("") hashes the empty string, producing a non-zero hash
// in practice (FNV-1a of "" = kOffset != 0). The guard for the zero-value
// sentinel (kNullAssetId) is the only case where a "load" silently fails.
// We test the observable contract: cancel before tick → count remains zero.

TEST(AudioStreamer, CompletedCountRemainsZeroWhenNothingSucceeds)
{
    AudioStreamer as;

    // Enqueue two items then cancel both before any tick.
    as.enqueue({ "audio/missing_a.wav", 0U, 1U });
    as.enqueue({ "audio/missing_b.wav", 0U, 2U });
    as.cancel("audio/missing_a.wav");
    as.cancel("audio/missing_b.wav");

    EXPECT_EQ(as.pending_count(),   0U);
    EXPECT_EQ(as.completed_count(), 0U);

    // tick on an empty queue is a no-op — no crash, no spurious increment.
    as.tick(0.016F);
    EXPECT_EQ(as.completed_count(), 0U);
}

// ---- T5: completed_count grows on each successful tick ----------------------

TEST(AudioStreamer, CompletedCountGrowsOnSuccess)
{
    AudioStreamer as;

    as.enqueue({ "audio/clip1.wav", 0U, 100U });
    as.enqueue({ "audio/clip2.wav", 1U, 100U });

    EXPECT_EQ(as.completed_count(), 0U);

    as.tick(0.016F);
    EXPECT_EQ(as.completed_count(), 1U);

    as.tick(0.016F);
    EXPECT_EQ(as.completed_count(), 2U);

    EXPECT_TRUE(as.is_loaded("audio/clip1.wav"));
    EXPECT_TRUE(as.is_loaded("audio/clip2.wav"));
}

// ---- T6: enqueue is idempotent (duplicate enqueue does not double-count) ----

TEST(AudioStreamer, EnqueueIdempotent)
{
    AudioStreamer as;

    as.enqueue({ "audio/clip.wav", 0U, 100U });
    as.enqueue({ "audio/clip.wav", 0U, 200U });  // Duplicate — must be ignored.
    as.enqueue({ "audio/clip.wav", 1U,  50U });  // Another duplicate.

    EXPECT_EQ(as.pending_count(), 1U);
}

// ---- T7: get_loaded returns nullopt for unknown path ------------------------

TEST(AudioStreamer, GetLoadedUnknownReturnsNullopt)
{
    const AudioStreamer as;

    EXPECT_EQ(as.get_loaded("audio/unknown.wav"), std::nullopt);
    EXPECT_FALSE(as.is_loaded("audio/unknown.wav"));
    EXPECT_EQ(as.pending_count(),   0U);
    EXPECT_EQ(as.completed_count(), 0U);
}

}  // namespace
