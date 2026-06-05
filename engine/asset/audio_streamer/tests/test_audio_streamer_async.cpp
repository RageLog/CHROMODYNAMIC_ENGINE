// =============================================================================
// CHROMODYNAMIC — engine/asset/audio_streamer/tests/test_audio_streamer_async.cpp
// Phase 756 — cd::asset::audio_streamer Sprint-2 async unit tests
//
// Tests run fully headless — no audio device required.  AsyncAudioPool uses a
// stub I/O path (same AssetId hash strategy as Sprint-1).
//
// Anti-flakiness: NO sleep_for anywhere.  All waiting uses condition_variable
// via join_all() or join_pending() which block on a predicate.
//
// Tests:
//   A1  AsyncAudioPool: configure + submit_async + join_all + poll_completed
//       verifies completed_count matches submitted count.
//   A2  AsyncAudioPool: poll_completed accumulates across multiple polls after
//       join_all; atomic counter never decrements.
//   A3  AsyncAudioPool: poll_completed is non-blocking (returns empty immediately
//       when nothing has completed yet and workers haven't been started).
//   A4  AudioStreamer async mode: 5 fake requests, tick() 3 times, verify
//       completed_count grows; join_pending() drains all remaining.
//   A5  AudioStreamer async mode: is_loaded() reports true for every submitted
//       path after join_pending().
//   A6  AudioStreamer async mode: get_loaded() returns valid AssetId for every
//       submitted path after join_pending().
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{

using cd::asset::audio_streamer::AsyncAudioPool;
using cd::asset::audio_streamer::AudioStreamer;
using cd::asset::audio_streamer::AudioStreamerConfig;
using cd::asset::audio_streamer::StreamRequest;

// ---- A1: pool submit + join + completed_count matches submitted count --------
//
// Five requests are submitted, join_all() blocks until all workers finish, then
// poll_completed() returns all five paths.  completed_count() must equal 5.

TEST(AudioStreamerAsync, PoolCompletedCountMatchesSubmitted)
{
    AsyncAudioPool pool;
    pool.configure(2U);

    const std::vector<std::string> paths = {
        "audio/a.wav", "audio/b.wav", "audio/c.wav", "audio/d.wav", "audio/e.wav"
    };

    for (const auto& p : paths)
    {
        pool.submit_async(StreamRequest{ p, 0U, 128U });
    }

    pool.join_all();  // blocks until all 5 are processed

    EXPECT_EQ(pool.completed_count(), paths.size());

    const auto done = pool.poll_completed();
    EXPECT_EQ(done.size(), paths.size());
}

// ---- A2: completed_count accumulates across multiple polls after join_all ----
//
// After join_all() the pool's completed_count() must reflect ALL completed work
// regardless of how many times poll_completed() has been called (poll clears
// the local buffer but the atomic counter is never decremented).

TEST(AudioStreamerAsync, CompletedCountNeverDecrementsAcrossPolls)
{
    AsyncAudioPool pool;
    pool.configure(2U);

    pool.submit_async(StreamRequest{ "music/track1.wav",   0U, 200U });
    pool.submit_async(StreamRequest{ "music/track2.wav",   0U, 100U });
    pool.submit_async(StreamRequest{ "sfx/explosion.wav",  0U,  50U });

    pool.join_all();

    EXPECT_EQ(pool.completed_count(), 3U);

    // First poll drains the buffer.
    const auto first = pool.poll_completed();
    EXPECT_EQ(first.size(), 3U);

    // Second poll on an already-drained buffer returns empty — count unchanged.
    const auto second = pool.poll_completed();
    EXPECT_TRUE(second.empty());

    // Atomic counter is cumulative — it never decrements.
    EXPECT_EQ(pool.completed_count(), 3U);
}

// ---- A3: poll_completed is non-blocking when pool has no workers started ----
//
// A pool that has never had configure() called and has no workers should return
// an empty vector immediately from poll_completed().

TEST(AudioStreamerAsync, PollCompletedNonBlockingOnEmptyPool)
{
    AsyncAudioPool pool;
    // Intentionally NOT calling configure() — no workers started.

    const auto result = pool.poll_completed();
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(pool.completed_count(), 0U);
}

// ---- A4: AudioStreamer async mode: tick drives loading, count grows ----------
//
// 5 fake requests are enqueued.  tick() is called 3 times.  After the first
// tick, the pool has received all 5 requests (pending_map_ is cleared) and
// worker threads are running.  After join_pending(), all 5 must be completed.

TEST(AudioStreamerAsync, AudioStreamerAsyncTickGrowsCompletedCount)
{
    AudioStreamer as { AudioStreamerConfig{ .use_async = true, .worker_count = 2U } };

    as.enqueue(StreamRequest{ "music/level1_ambient.wav",  0U, 200U });
    as.enqueue(StreamRequest{ "music/level1_action.wav",   0U, 190U });
    as.enqueue(StreamRequest{ "sfx/footstep_concrete.wav", 0U, 180U });
    as.enqueue(StreamRequest{ "sfx/footstep_metal.wav",    0U, 170U });
    as.enqueue(StreamRequest{ "sfx/door_open.wav",         0U, 160U });

    EXPECT_EQ(as.pending_count(), 5U);

    // Tick 1: submits all 5 to the pool, pending_map_ is now empty.
    as.tick(0.016F);
    EXPECT_EQ(as.pending_count(), 0U);

    // Tick 2 & 3: drain completions from the pool.  Workers may already be
    // done at this point; we are NOT sleeping — just polling.
    as.tick(0.016F);
    as.tick(0.016F);

    // join_pending() blocks (no sleep) until all 5 are finished.
    as.join_pending();

    EXPECT_EQ(as.completed_count(), 5U);
}

// ---- A5: is_loaded() true for every path after join_pending() ---------------

TEST(AudioStreamerAsync, IsLoadedTrueForAllPathsAfterJoinPending)
{
    AudioStreamer as { AudioStreamerConfig{ .use_async = true, .worker_count = 2U } };

    const std::vector<std::string> paths = {
        "music/intro.wav",
        "music/loop.wav",
        "sfx/jump.wav",
        "sfx/land.wav",
        "voice/player_hurt.wav",
    };

    for (const auto& p : paths)
    {
        as.enqueue(StreamRequest{ p, 0U, 128U });
    }

    as.tick(0.016F);   // submits all to pool
    as.join_pending(); // waits for completion, drains into completed_paths_

    for (const auto& p : paths)
    {
        EXPECT_TRUE(as.is_loaded(p)) << "Expected loaded: " << p;
    }
    EXPECT_EQ(as.completed_count(), paths.size());
}

// ---- A6: get_loaded() returns valid AssetId for every path after join_pending

TEST(AudioStreamerAsync, GetLoadedReturnsValidAssetIdAfterJoinPending)
{
    AudioStreamer as { AudioStreamerConfig{ .use_async = true, .worker_count = 2U } };

    const std::vector<std::string> paths = {
        "music/credits.wav",
        "sfx/pickup.wav",
        "sfx/reload.wav",
    };

    for (const auto& p : paths)
    {
        as.enqueue(StreamRequest{ p, 0U, 200U });
    }

    as.tick(0.016F);
    as.join_pending();

    for (const auto& p : paths)
    {
        const auto id = as.get_loaded(p);
        ASSERT_TRUE(id.has_value()) << "Expected AssetId for: " << p;
        EXPECT_TRUE(id->is_valid()) << "AssetId should be valid for: " << p;
    }
}

}  // namespace
