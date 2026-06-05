// =============================================================================
// CHROMODYNAMIC — engine/asset/scene_streamer/tests/test_scene_streamer_async.cpp
// Phase 755 — cd::asset::scene_streamer Sprint-2 async unit tests
//
// Tests run fully headless — no real glTF I/O required.  AsyncScenePool uses
// a stub completion path (path token only; real decode deferred to future Sprint).
//
// Anti-flakiness: NO sleep_for anywhere.  All waiting uses condition_variable
// via join_all() or join_pending() which block on a predicate.
//
// Tests:
//   A1  AsyncScenePool: configure + submit_async + join_all + poll_completed
//       verifies completed_count matches submitted count.
//   A2  AsyncScenePool: completed_count accumulates across multiple poll calls.
//   A3  AsyncScenePool: poll_completed is non-blocking when no workers started.
//   A4  SceneStreamer async mode: 5 fake requests, tick() 3 times, verify
//       completed_count grows; join_pending() drains all remaining.
//   A5  SceneStreamer async mode: is_loaded() true for every submitted path
//       after join_pending().
//   A6  SceneStreamer async mode: pending_count reaches 0 after first tick().
// =============================================================================

#include <cd/asset/scene_streamer/SceneStreamer.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{

using cd::asset::scene_streamer::AsyncScenePool;
using cd::asset::scene_streamer::SceneStreamer;
using cd::asset::scene_streamer::SceneStreamerConfig;
using cd::asset::scene_streamer::StreamRequest;

// ---- A1: pool submit + join + completed_count matches submitted count --------
//
// Five requests are submitted, join_all() blocks until all workers finish, then
// poll_completed() returns all five paths.  completed_count() must equal 5.

TEST(SceneStreamerAsync, PoolCompletedCountMatchesSubmitted)
{
    AsyncScenePool pool;
    pool.configure(2U);

    const std::vector<std::string> paths = {
        "level/zone_a.glb",
        "level/zone_b.glb",
        "level/zone_c.glb",
        "level/zone_d.glb",
        "level/zone_e.glb",
    };

    for (const auto& p : paths)
    {
        pool.submit_async(StreamRequest{ p, 128U });
    }

    pool.join_all();  // blocks until all 5 are processed

    EXPECT_EQ(pool.completed_count(), paths.size());

    const auto done = pool.poll_completed();
    EXPECT_EQ(done.size(), paths.size());
}

// ---- A2: completed_count accumulates across multiple poll calls --------------
//
// After join_all() the pool's completed_count() must reflect ALL completed work
// regardless of how many times poll_completed() has been called (poll clears
// the local buffer but the atomic counter is never decremented).

TEST(SceneStreamerAsync, CompletedCountNeverDecrementsAcrossPolls)
{
    AsyncScenePool pool;
    pool.configure(2U);

    pool.submit_async(StreamRequest{ "s1.glb", 200U });
    pool.submit_async(StreamRequest{ "s2.glb", 100U });
    pool.submit_async(StreamRequest{ "s3.glb",  50U });

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

TEST(SceneStreamerAsync, PollCompletedNonBlockingOnEmptyPool)
{
    AsyncScenePool pool;
    // Intentionally NOT calling configure() — no workers started.

    const auto result = pool.poll_completed();
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(pool.completed_count(), 0U);
}

// ---- A4: SceneStreamer async mode: tick drives loading, count grows ----------
//
// 5 fake requests are enqueued.  tick() is called 3 times.  After the first
// tick, the pool has received all 5 requests (pending_map_ is cleared) and
// worker threads are running.  After join_pending(), all 5 must be completed.

TEST(SceneStreamerAsync, SceneStreamerAsyncTickGrowsCompletedCount)
{
    SceneStreamer s { SceneStreamerConfig{ .use_async = true, .worker_count = 2U } };

    s.enqueue(StreamRequest{ "level/city_block_00.glb",  200U });
    s.enqueue(StreamRequest{ "level/city_block_01.glb",  190U });
    s.enqueue(StreamRequest{ "level/city_block_02.glb",  180U });
    s.enqueue(StreamRequest{ "level/city_block_03.glb",  170U });
    s.enqueue(StreamRequest{ "level/city_block_04.glb",  160U });

    EXPECT_EQ(s.pending_count(), 5U);

    // Tick 1: submits all 5 to the pool; pending_map_ is now empty.
    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 0U);

    // Tick 2 & 3: drain completions from the pool.  Workers may already be
    // done at this point; we are NOT sleeping — just polling.
    s.tick(0.016F);
    s.tick(0.016F);

    // join_pending() blocks (no sleep_for) until all 5 are finished.
    s.join_pending();

    EXPECT_EQ(s.completed_count(), 5U);
}

// ---- A5: is_loaded() true for every path after join_pending() ---------------

TEST(SceneStreamerAsync, IsLoadedTrueForAllPathsAfterJoinPending)
{
    SceneStreamer s { SceneStreamerConfig{ .use_async = true, .worker_count = 2U } };

    const std::vector<std::string> paths = {
        "world/sponza.glb",
        "world/powerplant.glb",
        "world/san_miguel.glb",
        "world/bistro_exterior.glb",
        "world/bistro_interior.glb",
    };

    for (const auto& p : paths)
    {
        s.enqueue(StreamRequest{ p, 128U });
    }

    s.tick(0.016F);    // submits all to pool
    s.join_pending();  // waits for completion, drains into completed_paths_

    for (const auto& p : paths)
    {
        EXPECT_TRUE(s.is_loaded(p)) << "Expected loaded: " << p;
    }
    EXPECT_EQ(s.completed_count(), paths.size());
}

// ---- A6: pending_count reaches 0 after first tick() in async mode -----------
//
// In async mode tick() submits ALL pending requests to the pool in a single
// call, so pending_count must drop to 0 immediately after the first tick().

TEST(SceneStreamerAsync, PendingCountZeroAfterFirstAsyncTick)
{
    SceneStreamer s { SceneStreamerConfig{ .use_async = true, .worker_count = 2U } };

    s.enqueue(StreamRequest{ "chunk/north.glb", 255U });
    s.enqueue(StreamRequest{ "chunk/south.glb", 200U });
    s.enqueue(StreamRequest{ "chunk/east.glb",  150U });

    EXPECT_EQ(s.pending_count(), 3U);

    s.tick(0.016F);

    // All three requests dispatched to pool — local pending must be empty.
    EXPECT_EQ(s.pending_count(), 0U);

    // Drain to avoid dangling workers.
    s.join_pending();
}

}  // namespace
