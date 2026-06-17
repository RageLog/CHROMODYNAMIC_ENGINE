// =============================================================================
// CHROMODYNAMIC — engine/asset/streamer_pool/tests/test_streamer_pool.cpp
// Phase 705 — cd::asset::streamer_pool unit tests (Sprint-1)
//
// All tests use cd::rhi::NullDevice for the texture path so they run without a
// real Vulkan ICD.  SceneStreamer and AudioStreamer use their own Sprint-1
// in-process stubs (no real file I/O).
//
// Tests:
//   T1  stats returns zeros when no streamers are attached
//   T2  audio-only: tick drains pending → completed
//   T3  priority weighting: scene (10) vs audio (4) — scene gets more tokens
//   T4  configure() resets deficit accumulators (no stale bias after reconfig)
//   T5  texture streamer drains via pool tick (NullDevice path)
//   T6  shader cache stats reflected in shader_completed
//   T7  tick on all-zero pending is a no-op (no crash, no spurious counts)
//   T8  detach by passing nullptr stops tick dispatch
//   T9  zero-weight streamer: a priority-0 streamer is intentionally never
//       scheduled (weight 0 == "do not dispatch"); a positive-weight streamer
//       sharing the pool still drains fully and the deficit math does not
//       divide by zero when the only pending streamer carries weight 0.
//   T10 single-streamer: the sole active positive-weight streamer receives every
//       token (no weight is wasted when only one streamer competes).
//   T11 fairness / no-starvation: across many ticks a much-lower-priority
//       streamer is still serviced (Bresenham deficit prevents permanent
//       starvation) and both streamers fully drain.
// =============================================================================

#include <cd/asset/streamer_pool/StreamerPool.hpp>

#include <cd/asset/audio_streamer/AudioStreamer.hpp>
#include <cd/asset/scene_streamer/SceneStreamer.hpp>
#include <cd/asset/shader_cache/ShaderCache.hpp>
#include <cd/asset/texture_streamer/TextureStreamer.hpp>
#include <cd/rhi/NullDevice.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace
{

using cd::asset::streamer_pool::PoolConfig;
using cd::asset::streamer_pool::PoolStats;
using cd::asset::streamer_pool::StreamerPool;

// ---- helpers -----------------------------------------------------------------

/// Drain a pool for up to `max_ticks` frames.  Returns the actual count.
static std::uint32_t drain(StreamerPool& pool,
                            std::uint32_t max_ticks = 64U,
                            float         dt         = 0.016F)
{
    std::uint32_t ticks = 0U;
    for (; ticks < max_ticks; ++ticks)
    {
        const PoolStats s = pool.stats();
        const bool any_pending =
            (s.scene_pending + s.texture_pending + s.audio_pending) > 0U;
        if (!any_pending) { break; }
        pool.tick(dt);
    }
    return ticks;
}

// ---- T1: stats returns zeros when no streamers are attached ------------------

TEST(StreamerPool, StatsAllZeroWhenNoStreamersAttached)
{
    StreamerPool pool;

    const PoolStats s = pool.stats();

    EXPECT_EQ(s.scene_pending,    0U);
    EXPECT_EQ(s.scene_completed,  0U);
    EXPECT_EQ(s.texture_pending,  0U);
    EXPECT_EQ(s.texture_completed,0U);
    EXPECT_EQ(s.audio_pending,    0U);
    EXPECT_EQ(s.audio_completed,  0U);
    EXPECT_EQ(s.shader_pending,   0U);
    EXPECT_EQ(s.shader_completed, 0U);
}

// ---- T2: audio-only: tick drains pending → completed ------------------------

TEST(StreamerPool, AudioOnlyTickDrainsPending)
{
    cd::asset::audio_streamer::AudioStreamer audio;

    audio.enqueue({ "audio/clip_a.wav", 0U, 100U });
    audio.enqueue({ "audio/clip_b.wav", 0U, 100U });

    StreamerPool pool;
    pool.attach_audio(&audio);

    EXPECT_EQ(pool.stats().audio_pending, 2U);
    EXPECT_EQ(pool.stats().audio_completed, 0U);

    drain(pool);

    EXPECT_EQ(pool.stats().audio_pending,   0U);
    EXPECT_EQ(pool.stats().audio_completed, 2U);
}

// ---- T3: priority weighting — scene(10) vs audio(4) gets more tokens --------
//
// Enqueue 10 items in each streamer.  SceneStreamer Sprint-1 does a real file
// load that silently fails for non-existent paths — items are dequeued but not
// completed.  AudioStreamer Sprint-1 always succeeds (path hash).  We therefore
// measure the dispatch token allocation via *pending reduction* (items consumed
// from the queue), which is observable regardless of load success.
//
// With scene_priority=10, audio_priority=4 and max_concurrent_loads=4:
//   total weight = 14.
//   Per tick scene gets floor(10/14 * 4) = 2+ tokens; audio gets ~1 token.
//   After 3 ticks (12 total tokens), scene should have consumed >= audio.

TEST(StreamerPool, ScenePriorityHigherThanAudio)
{
    cd::asset::scene_streamer::SceneStreamer scene;
    cd::asset::audio_streamer::AudioStreamer audio;

    constexpr int kItems = 10;
    for (int i = 0; i < kItems; ++i)
    {
        scene.enqueue({ "scene/level" + std::to_string(i) + ".glb", 100U });
        audio.enqueue({ "audio/sfx"   + std::to_string(i) + ".wav", 0U, 100U });
    }

    StreamerPool pool;
    pool.configure(PoolConfig{
        .max_concurrent_loads = 4U,
        .scene_priority       = 10U,
        .texture_priority     = 8U,
        .audio_priority       = 4U,
        .shader_priority      = 4U,
    });
    pool.attach_scene(&scene);
    pool.attach_audio(&audio);

    // Run just 3 ticks (not enough to drain either fully).
    for (int t = 0; t < 3; ++t) { pool.tick(0.016F); }

    // Measure items *dequeued* (pending reduction) rather than completed, since
    // SceneStreamer Sprint-1 drops failed loads silently without incrementing
    // completed_count().  Items removed = kItems - pending_remaining.
    const PoolStats s = pool.stats();
    const auto scene_dequeued = static_cast<std::uint32_t>(kItems) - s.scene_pending;
    const auto audio_dequeued = static_cast<std::uint32_t>(kItems) - s.audio_pending;

    // Higher-priority streamer (scene, weight 10) should receive >= tokens.
    EXPECT_GE(scene_dequeued, audio_dequeued);
    // Sanity: some items must have been dispatched.
    EXPECT_GT(scene_dequeued + audio_dequeued, 0U);
}

// ---- T4: configure() resets deficit accumulators ----------------------------
//
// If we run ticks that build up deficit bias and then reconfigure, the new
// allocation must restart cleanly without inherited bias.

TEST(StreamerPool, ReconfigureResetsDeficit)
{
    cd::asset::audio_streamer::AudioStreamer audio;

    for (int i = 0; i < 8; ++i)
    {
        audio.enqueue({ "audio/clip" + std::to_string(i) + ".wav", 0U, 100U });
    }

    StreamerPool pool;
    pool.attach_audio(&audio);

    // Run a few ticks to accumulate deficit.
    pool.tick(0.016F);
    pool.tick(0.016F);

    // Reconfigure — must not crash and must reset deficit cleanly.
    pool.configure(PoolConfig{
        .max_concurrent_loads = 2U,
        .audio_priority       = 6U,
    });

    // After reconfig, pool should still drain remaining audio.
    drain(pool, 16U);

    EXPECT_EQ(pool.stats().audio_pending, 0U);
}

// ---- T5: texture streamer drains via pool tick (NullDevice) -----------------

TEST(StreamerPool, TextureStreamerDrainsViaTick)
{
    cd::rhi::NullDevice                          device;
    cd::asset::texture_streamer::TextureStreamer texture;

    texture.enqueue({ "textures/albedo.cdtex",  0U, 200U });
    texture.enqueue({ "textures/normal.cdtex",  0U, 150U });
    texture.enqueue({ "textures/roughness.cdtex", 0U, 100U });

    StreamerPool pool;
    pool.configure(PoolConfig{ .max_concurrent_loads = 4U });
    pool.attach_texture(&texture, &device);

    EXPECT_EQ(pool.stats().texture_pending, 3U);

    drain(pool);

    EXPECT_EQ(pool.stats().texture_pending,   0U);
    EXPECT_EQ(pool.stats().texture_completed, 3U);
}

// ---- T6: shader cache stats reflected in shader_completed -------------------

TEST(StreamerPool, ShaderCacheEntryCountInStats)
{
    cd::asset::shader_cache::ShaderCache cache;

    cache.put({ "abc123", "main", 0U, 0U }, { 0x07230203U }, "main", 0U);
    cache.put({ "def456", "main", 1U, 0U }, { 0x07230203U }, "main", 1U);

    StreamerPool pool;
    pool.attach_shader(&cache);

    const PoolStats s = pool.stats();

    EXPECT_EQ(s.shader_pending,   0U);
    EXPECT_EQ(s.shader_completed, 2U);  // Matches ShaderCache::entry_count().
}

// ---- T7: tick on all-zero pending is a no-op (no crash) --------------------

TEST(StreamerPool, TickOnEmptyQueueIsNoop)
{
    cd::asset::audio_streamer::AudioStreamer audio;
    StreamerPool pool;
    pool.attach_audio(&audio);

    // No enqueues — pending is 0 from the start.
    EXPECT_EQ(pool.stats().audio_pending, 0U);

    // Must not crash, must not increment completed.
    pool.tick(0.016F);
    pool.tick(0.016F);

    EXPECT_EQ(pool.stats().audio_pending,   0U);
    EXPECT_EQ(pool.stats().audio_completed, 0U);
}

// ---- T8: detach by passing nullptr stops tick dispatch ----------------------

TEST(StreamerPool, DetachNullptrStopsDispatch)
{
    cd::asset::audio_streamer::AudioStreamer audio;
    audio.enqueue({ "audio/once.wav", 0U, 100U });

    StreamerPool pool;
    pool.attach_audio(&audio);

    EXPECT_EQ(pool.stats().audio_pending, 1U);

    // Detach — tick should not dispatch to audio streamer.
    pool.attach_audio(nullptr);

    pool.tick(0.016F);
    pool.tick(0.016F);

    // The streamer still has its pending item (pool did not tick it).
    EXPECT_EQ(audio.pending_count(), 1U);

    // Pool stats see nothing attached.
    EXPECT_EQ(pool.stats().audio_pending,   0U);
    EXPECT_EQ(pool.stats().audio_completed, 0U);
}

// ---- T9: zero-weight streamer is not scheduled, positive one still drains ----
//
// audio_priority = 0  -> audio must never receive a token while it is the only
// active streamer (weight 0 means "do not dispatch"), and the deficit math must
// not divide by zero. A scene streamer with a positive weight, enqueued in the
// same pool, still drains its queue normally.

TEST(StreamerPool, ZeroWeightStreamerNeverScheduled)
{
    cd::asset::audio_streamer::AudioStreamer audio;
    cd::asset::scene_streamer::SceneStreamer scene;

    for (int i = 0; i < 4; ++i)
    {
        audio.enqueue({ "audio/zw" + std::to_string(i) + ".wav", 0U, 100U });
        scene.enqueue({ "scene/zw" + std::to_string(i) + ".glb", 100U });
    }

    StreamerPool pool;
    pool.configure(PoolConfig{
        .max_concurrent_loads = 4U,
        .scene_priority       = 10U,
        .texture_priority     = 8U,
        .audio_priority       = 0U,   // zero weight -> never dispatched
        .shader_priority      = 4U,
    });
    pool.attach_scene(&scene);
    pool.attach_audio(&audio);

    // Tick enough to drain everything that *can* be drained.
    for (int t = 0; t < 32; ++t) { pool.tick(0.016F); }

    // Scene (positive weight) fully dequeued; audio (zero weight) untouched.
    EXPECT_EQ(pool.stats().scene_pending, 0U)
        << "positive-weight scene streamer must drain";
    EXPECT_EQ(pool.stats().audio_pending, 4U)
        << "zero-weight audio streamer must never be scheduled";
    EXPECT_EQ(pool.stats().audio_completed, 0U);
}

// ---- T10: single active streamer receives every token -----------------------

TEST(StreamerPool, SingleStreamerGetsAllTokens)
{
    cd::asset::audio_streamer::AudioStreamer audio;

    // Enqueue exactly max_concurrent_loads items so a single tick should drain
    // them all if the sole streamer truly receives every token.
    for (int i = 0; i < 4; ++i)
    {
        audio.enqueue({ "audio/single" + std::to_string(i) + ".wav", 0U, 100U });
    }

    StreamerPool pool;
    pool.configure(PoolConfig{
        .max_concurrent_loads = 4U,
        .scene_priority       = 10U,
        .texture_priority     = 8U,
        .audio_priority       = 6U,
        .shader_priority      = 4U,
    });
    pool.attach_audio(&audio);  // audio is the ONLY attached streamer

    EXPECT_EQ(pool.stats().audio_pending, 4U);

    // One tick: total_weight == audio_priority, so audio gets all 4 tokens.
    pool.tick(0.016F);

    EXPECT_EQ(pool.stats().audio_pending,   0U)
        << "sole active streamer must receive every token in one tick";
    EXPECT_EQ(pool.stats().audio_completed, 4U);
}

// ---- T11: fairness — a much-lower-priority streamer is never starved --------
//
// scene_priority = 12, audio_priority = 1. Even though audio's weight is 12x
// smaller, the Bresenham deficit accumulator must eventually grant it tokens so
// that, given enough ticks, audio fully drains rather than being starved forever.

TEST(StreamerPool, LowPriorityStreamerNotStarved)
{
    cd::asset::scene_streamer::SceneStreamer scene;
    cd::asset::audio_streamer::AudioStreamer audio;

    constexpr int kSceneItems = 30;
    constexpr int kAudioItems = 5;
    for (int i = 0; i < kSceneItems; ++i)
    {
        scene.enqueue({ "scene/fair" + std::to_string(i) + ".glb", 100U });
    }
    for (int i = 0; i < kAudioItems; ++i)
    {
        audio.enqueue({ "audio/fair" + std::to_string(i) + ".wav", 0U, 100U });
    }

    StreamerPool pool;
    pool.configure(PoolConfig{
        .max_concurrent_loads = 4U,
        .scene_priority       = 12U,
        .texture_priority     = 8U,
        .audio_priority       = 1U,   // 12x lower than scene
        .shader_priority      = 4U,
    });
    pool.attach_scene(&scene);
    pool.attach_audio(&audio);

    // Bounded number of ticks — must be enough to drain both queues if the
    // low-priority streamer is genuinely never starved.
    drain(pool, /*max_ticks=*/256U);

    EXPECT_EQ(pool.stats().scene_pending, 0U) << "scene queue must drain";
    EXPECT_EQ(pool.stats().audio_pending, 0U)
        << "low-priority audio must not be starved — it must fully drain";
    EXPECT_EQ(pool.stats().audio_completed, static_cast<std::uint32_t>(kAudioItems));
}

}  // namespace
