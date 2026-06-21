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
//   T8  cancel unknown path is a no-op (does not crash)
//   T9  cancel after load is a no-op (loaded entry stays in completed_)
//   T10 enqueue already-loaded path is a no-op (completed count unchanged)
//   T11 SceneId uniqueness: two distinct paths → distinct SceneIds
//   T12 SceneId stability: id returned from get_loaded equals the stored one
//   T13 get_scene returns nullptr for unknown/failed path
//   T14 sync tick drains exactly one item per call
//   T15 bulk enqueue + N ticks drains all items
//   T16 join_pending is a no-op in sync mode (does not deadlock/crash)
//   T17 completed_count never decrements across enqueue/cancel cycles
//   T18 priority preserved on duplicate enqueue (first priority wins)
//   T19 real sync load: get_scene returns genuine node/mesh tree (SEALED path)
// =============================================================================

#include <cd/asset/scene_streamer/SceneStreamer.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

using cd::asset::scene_streamer::SceneId;
using cd::asset::scene_streamer::SceneStreamer;
using cd::asset::scene_streamer::StreamRequest;

// ---- Triangle .gltf fixture helpers (shared with async suite) ---------------

[[nodiscard]] std::vector<std::uint8_t> make_triangle_bin_s()
{
    std::vector<std::uint8_t> bin(42);
    auto* p = reinterpret_cast<float*>(bin.data());
    p[0] = -1.0F; p[1] = 0.0F; p[2] = 0.0F;
    p[3] =  1.0F; p[4] = 0.0F; p[5] = 0.0F;
    p[6] =  0.0F; p[7] = 1.0F; p[8] = 0.0F;
    auto* idx = reinterpret_cast<std::uint16_t*>(bin.data() + 36);
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    return bin;
}

[[nodiscard]] std::string base64_encode_s(std::span<const std::uint8_t> in)
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2U) / 3U) * 4U);
    std::size_t i = 0U;
    while (i + 3U <= in.size())
    {
        const auto v = (static_cast<std::uint32_t>(in[i]) << 16U) |
                       (static_cast<std::uint32_t>(in[i + 1U]) << 8U) |
                        static_cast<std::uint32_t>(in[i + 2U]);
        out.push_back(kAlphabet[(v >> 18U) & 0x3FU]);
        out.push_back(kAlphabet[(v >> 12U) & 0x3FU]);
        out.push_back(kAlphabet[(v >>  6U) & 0x3FU]);
        out.push_back(kAlphabet[ v         & 0x3FU]);
        i += 3U;
    }
    if (i < in.size())
    {
        auto v = static_cast<std::uint32_t>(in[i]) << 16U;
        if (i + 1U < in.size())
        {
            v |= static_cast<std::uint32_t>(in[i + 1U]) << 8U;
        }
        out.push_back(kAlphabet[(v >> 18U) & 0x3FU]);
        out.push_back(kAlphabet[(v >> 12U) & 0x3FU]);
        out.push_back((i + 1U < in.size()) ? kAlphabet[(v >> 6U) & 0x3FU] : '=');
        out.push_back('=');
    }
    return out;
}

[[nodiscard]] std::string make_triangle_gltf_json_s()
{
    const auto bin = make_triangle_bin_s();
    const auto b64 = base64_encode_s(bin);
    std::string j;
    j += R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,)";
    j += b64;
    j += R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)";
    j += R"({"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],)";
    j += R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)";
    j += R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)";
    j += R"("meshes":[{"name":"tri","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)";
    j += R"("materials":[{"name":"red","pbrMetallicRoughness":{"baseColorFactor":[1.0,0.25,0.125,1.0],"metallicFactor":0.5,"roughnessFactor":0.75},"doubleSided":true,"alphaMode":"MASK","alphaCutoff":0.3}],)";
    j += R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    return j;
}

[[nodiscard]] fs::path tmp_gltf_path_s()
{
    static std::atomic<std::uint64_t> seq { 0U };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("cd_ss_sync_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
            std::to_string(seq.fetch_add(1U)) + ".gltf");
}

struct PathGuardS
{
    fs::path path;

    explicit PathGuardS(fs::path p)
        : path { std::move(p) }
    {
    }

    ~PathGuardS()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }

    PathGuardS(const PathGuardS&)            = delete;
    PathGuardS& operator=(const PathGuardS&) = delete;
    PathGuardS(PathGuardS&&)                 = delete;
    PathGuardS& operator=(PathGuardS&&)      = delete;
};

void write_triangle_gltf_s(const fs::path& p)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    const auto json = make_triangle_gltf_json_s();
    f.write(json.data(), static_cast<std::streamsize>(json.size()));
}

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

// ---- T8: cancel unknown path is a no-op (must not crash) --------------------

TEST(SceneStreamer, CancelUnknownPathIsNoOp)
{
    SceneStreamer s;
    s.cancel("never_enqueued.glb");  // Should not crash.
    EXPECT_EQ(s.pending_count(),   0U);
    EXPECT_EQ(s.completed_count(), 0U);
}

// ---- T9: cancel after successful load is a no-op ----------------------------
//   load → is_loaded true → cancel → still is_loaded true (completed_ immutable)

TEST(SceneStreamer, CancelAfterLoadIsNoOp)
{
    PathGuardS g { tmp_gltf_path_s() };
    write_triangle_gltf_s(g.path);

    SceneStreamer s;
    s.enqueue(StreamRequest{ g.path.string(), 200U });
    s.tick(0.016F);  // Synchronously loads.

    ASSERT_TRUE(s.is_loaded(g.path.string()));
    ASSERT_EQ(s.completed_count(), 1U);

    s.cancel(g.path.string());  // No-op for a loaded path.

    // Loaded entry must still be accessible.
    EXPECT_TRUE(s.is_loaded(g.path.string()));
    EXPECT_EQ(s.completed_count(), 1U);
    EXPECT_NE(s.get_loaded(g.path.string()), std::nullopt);
}

// ---- T10: enqueue already-loaded path is a no-op ---------------------------
//   load → is_loaded true → enqueue again → pending stays 0

TEST(SceneStreamer, EnqueueAlreadyLoadedIsNoOp)
{
    PathGuardS g { tmp_gltf_path_s() };
    write_triangle_gltf_s(g.path);

    SceneStreamer s;
    s.enqueue(StreamRequest{ g.path.string(), 200U });
    s.tick(0.016F);  // Load succeeds.

    ASSERT_TRUE(s.is_loaded(g.path.string()));

    // Re-enqueue same path: must be a no-op — already in completed_.
    s.enqueue(StreamRequest{ g.path.string(), 255U });
    EXPECT_EQ(s.pending_count(),   0U);
    EXPECT_EQ(s.completed_count(), 1U);
}

// ---- T11: SceneId uniqueness — two distinct paths yield distinct SceneIds ---

TEST(SceneStreamer, SceneIdUniquePerPath)
{
    PathGuardS g0 { tmp_gltf_path_s() };
    PathGuardS g1 { tmp_gltf_path_s() };
    write_triangle_gltf_s(g0.path);
    write_triangle_gltf_s(g1.path);

    SceneStreamer s;
    s.enqueue(StreamRequest{ g0.path.string(), 200U });
    s.enqueue(StreamRequest{ g1.path.string(), 100U });
    s.tick(0.016F);  // Loads g0 (higher priority).
    s.tick(0.016F);  // Loads g1.

    ASSERT_EQ(s.completed_count(), 2U);

    const auto id0 = s.get_loaded(g0.path.string());
    const auto id1 = s.get_loaded(g1.path.string());
    ASSERT_TRUE(id0.has_value());
    ASSERT_TRUE(id1.has_value());

    // IDs must be distinct.
    EXPECT_NE(*id0, *id1);
}

// ---- T12: SceneId stability — repeated calls return the same id -------------

TEST(SceneStreamer, SceneIdStableAcrossRepeatedQueries)
{
    PathGuardS g { tmp_gltf_path_s() };
    write_triangle_gltf_s(g.path);

    SceneStreamer s;
    s.enqueue(StreamRequest{ g.path.string(), 128U });
    s.tick(0.016F);

    ASSERT_TRUE(s.is_loaded(g.path.string()));

    const auto id_a = s.get_loaded(g.path.string());
    const auto id_b = s.get_loaded(g.path.string());
    ASSERT_TRUE(id_a.has_value());
    ASSERT_TRUE(id_b.has_value());
    EXPECT_EQ(*id_a, *id_b);
}

// ---- T13: get_scene returns nullptr for unknown / failed path ---------------

TEST(SceneStreamer, GetSceneNullptrForUnknownOrFailed)
{
    SceneStreamer s;

    // Unknown path — never enqueued.
    EXPECT_EQ(s.get_scene("unknown.glb"), nullptr);

    // Failed load (missing file) — attempted but not completed.
    s.enqueue(StreamRequest{ "__bad__.glb", 255U });
    s.tick(0.016F);
    EXPECT_EQ(s.get_scene("__bad__.glb"), nullptr);
}

// ---- T14: sync tick drains exactly ONE item per call ------------------------

TEST(SceneStreamer, SyncTickDrainsOneItemPerCall)
{
    SceneStreamer s;
    // Three missing-file requests; each tick consumes one.
    s.enqueue(StreamRequest{ "a.glb", 10U });
    s.enqueue(StreamRequest{ "b.glb", 20U });
    s.enqueue(StreamRequest{ "c.glb", 30U });
    EXPECT_EQ(s.pending_count(), 3U);

    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 2U);

    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 1U);

    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 0U);
}

// ---- T15: bulk enqueue + N ticks drains all items --------------------------

TEST(SceneStreamer, BulkEnqueueNTicksDrainsAll)
{
    SceneStreamer s;
    constexpr std::size_t kCount = 8U;
    for (std::size_t i = 0U; i < kCount; ++i)
    {
        s.enqueue(StreamRequest{ "missing_" + std::to_string(i) + ".glb",
                                 static_cast<std::uint8_t>(i) });
    }
    EXPECT_EQ(s.pending_count(), kCount);

    for (std::size_t i = 0U; i < kCount; ++i)
    {
        s.tick(0.016F);
    }

    EXPECT_EQ(s.pending_count(), 0U);
    // All were missing files → no successes.
    EXPECT_EQ(s.completed_count(), 0U);
}

// ---- T16: join_pending is a no-op in sync mode (must not deadlock/crash) ---

TEST(SceneStreamer, JoinPendingNoOpInSyncMode)
{
    SceneStreamer s;  // default = sync
    s.enqueue(StreamRequest{ "missing.glb", 128U });
    s.tick(0.016F);
    s.join_pending();  // Must not deadlock or crash.
    EXPECT_EQ(s.pending_count(), 0U);
}

// ---- T17: completed_count never decrements across enqueue/cancel cycles -----

TEST(SceneStreamer, CompletedCountNeverDecrements)
{
    PathGuardS g0 { tmp_gltf_path_s() };
    PathGuardS g1 { tmp_gltf_path_s() };
    write_triangle_gltf_s(g0.path);
    write_triangle_gltf_s(g1.path);

    SceneStreamer s;
    s.enqueue(StreamRequest{ g0.path.string(), 200U });
    s.tick(0.016F);
    ASSERT_EQ(s.completed_count(), 1U);

    // Cancel a different path, enqueue a missing file, tick — count unchanged.
    s.cancel("phantom.glb");
    s.enqueue(StreamRequest{ "also_missing.glb", 10U });
    s.tick(0.016F);
    EXPECT_EQ(s.completed_count(), 1U);  // missing file didn't add.

    // Load second real fixture.
    s.enqueue(StreamRequest{ g1.path.string(), 100U });
    s.tick(0.016F);
    EXPECT_EQ(s.completed_count(), 2U);  // grew, never shrank.
}

// ---- T18: priority preserved on duplicate enqueue (first priority wins) -----

TEST(SceneStreamer, FirstPriorityWinsOnDuplicate)
{
    SceneStreamer s;
    // First enqueue at priority 50.
    s.enqueue(StreamRequest{ "prio_test.glb", 50U });
    // Duplicate: should be no-op; original priority 50 stays.
    s.enqueue(StreamRequest{ "prio_test.glb", 255U });

    EXPECT_EQ(s.pending_count(), 1U);

    // Enqueue another at priority 100. If the duplicate had wrongly updated
    // the priority to 255, it would beat "other.glb" (100) and be consumed
    // first; instead with priority 50 it should lose to "other.glb" (100).
    s.enqueue(StreamRequest{ "other.glb", 100U });
    EXPECT_EQ(s.pending_count(), 2U);

    // One tick: highest-priority item consumed. Both files are missing, but
    // we can confirm which one was attempted by checking which remains.
    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 1U);

    // "other.glb" (prio 100) should have been consumed first.
    // "prio_test.glb" (prio 50) should still be pending.
    s.cancel("prio_test.glb");  // Cancels only if still pending.
    EXPECT_EQ(s.pending_count(), 0U);
}

// ---- T19: real sync load — get_scene returns genuine node/mesh tree ---------
//   NOTE: actual glTF file decode is real (cd::asset::gltf::load_scene).
//   GPU/ECS ingest is the render-side consumer's job and is NOT part of this
//   library. This test SEALS that the sync path produces real parsed data.

TEST(SceneStreamer, RealSyncLoadGetSceneReturnsGenuineTree)
{
    PathGuardS g { tmp_gltf_path_s() };
    write_triangle_gltf_s(g.path);

    SceneStreamer s;
    s.enqueue(StreamRequest{ g.path.string(), 255U });
    EXPECT_EQ(s.pending_count(), 1U);

    s.tick(0.016F);  // Synchronous real load.

    ASSERT_TRUE(s.is_loaded(g.path.string()));
    EXPECT_EQ(s.pending_count(),   0U);
    EXPECT_EQ(s.completed_count(), 1U);

    const auto* scene = s.get_scene(g.path.string());
    ASSERT_NE(scene, nullptr);

    // Real parse: the triangle fixture has 1 node, 1 mesh, 1 material.
    EXPECT_EQ(scene->nodes.size(),     1U);
    EXPECT_EQ(scene->meshes.size(),    1U);
    EXPECT_EQ(scene->materials.size(), 1U);
    EXPECT_FALSE(scene->root_nodes.empty());
    ASSERT_EQ(scene->meshes.size(), 1U);
    EXPECT_EQ(scene->meshes.front().primitives.size(), 1U);

    // get_loaded and get_scene are consistent.
    const auto id = s.get_loaded(g.path.string());
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->index, 0U);  // First and only completed entry.
}

}  // namespace
