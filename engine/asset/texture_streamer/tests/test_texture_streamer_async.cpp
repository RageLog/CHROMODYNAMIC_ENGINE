// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_streamer/tests/test_texture_streamer_async.cpp
// Phase 714 — cd::asset::texture_streamer Sprint-2 async unit tests
//
// Tests run fully headless — no Vulkan ICD required.  AsyncTexturePool uses a
// stub I/O path (same placeholder strategy as Sprint-1 NullDevice).
//
// Anti-flakiness: NO sleep_for anywhere.  All waiting uses condition_variable
// via join_all() or join_pending() which block on a predicate.
//
// Tests:
//   A1  AsyncTexturePool: configure + submit_async + join_all + poll_completed
//       verifies completed_count matches submitted count.
//   A2  AsyncTexturePool: poll_completed grows over multiple polls after join_all
//   A3  AsyncTexturePool: poll_completed is non-blocking (returns empty immediately
//       when nothing has completed yet and workers haven't been started).
//   A4  TextureStreamer async mode: 5 fake requests, tick() 3 times, verify
//       completed_count grows; join_pending() drains all remaining.
//   A5  TextureStreamer async mode: is_loaded() reports true for every submitted
//       path after join_pending().
// =============================================================================

#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#if __has_include(<cd/rhi/NullDevice.hpp>)
#  include <cd/rhi/NullDevice.hpp>
#endif

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{

using cd::asset::texture_streamer::AsyncTexturePool;
using cd::asset::texture_streamer::StreamRequest;
using cd::asset::texture_streamer::TextureStreamer;
using cd::asset::texture_streamer::TextureStreamerConfig;

// ---- A1: pool submit + join + completed_count matches submitted count --------
//
// Five requests are submitted, join_all() blocks until all workers finish, then
// poll_completed() returns all five paths.  completed_count() must equal 5.

TEST(TextureStreamerAsync, PoolCompletedCountMatchesSubmitted)
{
    AsyncTexturePool pool;
    pool.configure(2U);

    const std::vector<std::string> paths = {
        "tex/a.cdtex", "tex/b.cdtex", "tex/c.cdtex", "tex/d.cdtex", "tex/e.cdtex"
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

// ---- A2: completed_count accumulates across multiple poll_completed calls ----
//
// After join_all() the pool's completed_count() must reflect ALL completed work
// regardless of how many times poll_completed() has been called (poll clears
// the local buffer but the atomic counter is never decremented).

TEST(TextureStreamerAsync, CompletedCountNeverDecrementsAcrossPolls)
{
    AsyncTexturePool pool;
    pool.configure(2U);

    pool.submit_async(StreamRequest{ "t1.cdtex", 0U, 200U });
    pool.submit_async(StreamRequest{ "t2.cdtex", 0U, 100U });
    pool.submit_async(StreamRequest{ "t3.cdtex", 0U,  50U });

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

TEST(TextureStreamerAsync, PollCompletedNonBlockingOnEmptyPool)
{
    AsyncTexturePool pool;
    // Intentionally NOT calling configure() — no workers started.

    const auto result = pool.poll_completed();
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(pool.completed_count(), 0U);
}

// ---- A4: TextureStreamer async mode: tick drives loading, count grows --------
//
// 5 fake requests are enqueued.  tick() is called 3 times.  After the first
// tick, the pool has received all 5 requests (pending_map_ is cleared) and
// worker threads are running.  After join_pending(), all 5 must be completed.

TEST(TextureStreamerAsync, TextureStreamerAsyncTickGrowsCompletedCount)
{
    // NullDevice is unused by the async path; we need to pass something.
    // The tick() signature requires IDevice& but async mode never calls it.
    // Use a mock-free approach: construct a NullDevice locally.
    // (If NullDevice is not available in this TU, gate the test.)

    TextureStreamer ts { TextureStreamerConfig{ .use_async = true, .worker_count = 2U } };

    ts.enqueue(StreamRequest{ "level/albedo_0.cdtex",   0U, 200U });
    ts.enqueue(StreamRequest{ "level/albedo_1.cdtex",   0U, 190U });
    ts.enqueue(StreamRequest{ "level/normal_0.cdtex",   0U, 180U });
    ts.enqueue(StreamRequest{ "level/roughness_0.cdtex",0U, 170U });
    ts.enqueue(StreamRequest{ "level/emissive_0.cdtex", 0U, 160U });

    EXPECT_EQ(ts.pending_count(), 5U);

    // We need a device reference for the tick() signature even though async
    // mode doesn't call it.  Build a minimal NullDevice on the stack.
    // This avoids a Vulkan ICD dependency.
#if __has_include(<cd/rhi/NullDevice.hpp>)
    cd::rhi::NullDevice device;
    // Tick 1: submits all 5 to the pool, pending_map_ is now empty.
    ts.tick(0.016F, device);
    EXPECT_EQ(ts.pending_count(), 0U);

    // Tick 2 & 3: drain completions from the pool.  Workers may already be
    // done at this point; we are NOT sleeping — just polling.
    ts.tick(0.016F, device);
    ts.tick(0.016F, device);

    // join_pending() blocks (no sleep) until all 5 are finished.
    ts.join_pending();

    EXPECT_EQ(ts.completed_count(), 5U);
#else
    GTEST_SKIP() << "NullDevice not available — async tick test skipped";
#endif
}

// ---- A5: is_loaded() true for every path after join_pending() ---------------

TEST(TextureStreamerAsync, IsLoadedTrueForAllPathsAfterJoinPending)
{
#if __has_include(<cd/rhi/NullDevice.hpp>)
    TextureStreamer ts { TextureStreamerConfig{ .use_async = true, .worker_count = 2U } };

    const std::vector<std::string> paths = {
        "t/rock_d.cdtex",
        "t/rock_n.cdtex",
        "t/rock_r.cdtex",
        "t/concrete_d.cdtex",
        "t/concrete_n.cdtex",
    };

    for (const auto& p : paths)
    {
        ts.enqueue(StreamRequest{ p, 0U, 128U });
    }

    cd::rhi::NullDevice device;
    ts.tick(0.016F, device);  // submits all to pool
    ts.join_pending();         // waits for completion, drains into completed_paths_

    for (const auto& p : paths)
    {
        EXPECT_TRUE(ts.is_loaded(p)) << "Expected loaded: " << p;
    }
    EXPECT_EQ(ts.completed_count(), paths.size());
#else
    GTEST_SKIP() << "NullDevice not available — async is_loaded test skipped";
#endif
}

}  // namespace
