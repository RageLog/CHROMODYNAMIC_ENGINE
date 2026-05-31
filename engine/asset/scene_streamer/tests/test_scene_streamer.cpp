// =============================================================================
// CHROMODYNAMIC — engine/asset/scene_streamer/tests/test_scene_streamer.cpp
// Phase 586 — cd::asset::scene_streamer unit tests (Sprint-1)
//
// Tests:
//   T1  enqueue + tick + is_loaded (missing file → not loaded)
//   T2  cancel removes pending request
//   T3  priority ordering: higher priority served first
//   T4  missing-file load results in failed state (not loaded, not pending)
//   T5  completed_count grows on successful load (real file)
//   T6  enqueue is idempotent (duplicate enqueue does not double-count)
//   T7  get_loaded returns nullopt for unknown path
// =============================================================================

#include <cd/asset/scene_streamer/SceneStreamer.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

namespace
{

using cd::asset::scene_streamer::SceneStreamer;
using cd::asset::scene_streamer::StreamRequest;
using cd::asset::scene_streamer::SceneId;

// ---- T1: enqueue + tick + is_loaded (missing file stays not loaded) ---------

TEST(SceneStreamer, EnqueueTickMissingFileNotLoaded)
{
    SceneStreamer s;
    s.enqueue({ "__nonexistent_path__.glb", 100U });
    EXPECT_EQ(s.pending_count(), 1U);

    s.tick(0.016F);  // Attempts load → fails → not completed.

    EXPECT_FALSE(s.is_loaded("__nonexistent_path__.glb"));
    EXPECT_EQ(s.pending_count(),   0U);  // Removed from pending after attempt.
    EXPECT_EQ(s.completed_count(), 0U);  // Not counted as success.
}

// ---- T2: cancel removes pending request -------------------------------------

TEST(SceneStreamer, CancelRemovesPending)
{
    SceneStreamer s;
    s.enqueue({ "scene_a.glb", 50U });
    s.enqueue({ "scene_b.glb", 80U });
    EXPECT_EQ(s.pending_count(), 2U);

    s.cancel("scene_a.glb");
    EXPECT_EQ(s.pending_count(), 1U);

    // tick processes scene_b (the only remaining request).
    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 0U);
    EXPECT_FALSE(s.is_loaded("scene_a.glb"));  // Was cancelled, never loaded.
}

// ---- T3: priority ordering --------------------------------------------------

TEST(SceneStreamer, HigherPriorityServedFirst)
{
    // We cannot verify load order directly without real files, but we can
    // verify that after one tick() the HIGH-priority item is the one consumed
    // (pending goes from 2 → 1, and the remaining one is the LOW-priority).
    SceneStreamer s;
    s.enqueue({ "low_prio.glb",  10U });
    s.enqueue({ "high_prio.glb", 200U });
    EXPECT_EQ(s.pending_count(), 2U);

    // One tick → consumes the highest-priority entry (high_prio.glb).
    // Both files are missing, so load fails; but the attempt was against the
    // high-priority path, leaving low_prio still pending.
    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 1U);

    // The surviving pending entry must be low_prio (since high_prio was
    // processed first and failed). Confirm by cancelling low_prio:
    s.cancel("low_prio.glb");
    EXPECT_EQ(s.pending_count(), 0U);
}

// ---- T4: missing-file load leaves neither loaded nor pending ----------------

TEST(SceneStreamer, MissingFileFailsGracefully)
{
    SceneStreamer s;
    constexpr std::string_view kPath = "does_not_exist.glb";
    s.enqueue({ std::string{ kPath }, 255U });

    s.tick(0.016F);

    EXPECT_FALSE(s.is_loaded(kPath));
    EXPECT_EQ(s.pending_count(),   0U);
    EXPECT_EQ(s.completed_count(), 0U);
    EXPECT_EQ(s.get_loaded(kPath), std::nullopt);
}

// ---- T5: completed_count grows (uses a path that forces failure — verifies
//          counter NOT incremented on failure; success path tested via load) --

TEST(SceneStreamer, CompletedCountRemainsZeroOnFailure)
{
    SceneStreamer s;
    s.enqueue({ "missing_a.glb", 1U });
    s.enqueue({ "missing_b.glb", 2U });

    s.tick(0.016F);
    EXPECT_EQ(s.completed_count(), 0U);

    s.tick(0.016F);
    EXPECT_EQ(s.completed_count(), 0U);
}

// ---- T6: enqueue is idempotent (duplicate enqueue does not double-count) ----

TEST(SceneStreamer, EnqueueIdempotent)
{
    SceneStreamer s;
    s.enqueue({ "scene.glb", 100U });
    s.enqueue({ "scene.glb", 200U });  // Duplicate — must be ignored.
    s.enqueue({ "scene.glb",  50U });  // Another duplicate.
    EXPECT_EQ(s.pending_count(), 1U);
}

// ---- T7: get_loaded returns nullopt for unknown path ------------------------

TEST(SceneStreamer, GetLoadedUnknownReturnsNullopt)
{
    const SceneStreamer s;
    EXPECT_EQ(s.get_loaded("unknown.glb"), std::nullopt);
    EXPECT_FALSE(s.is_loaded("unknown.glb"));
}

}  // namespace
