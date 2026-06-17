// =============================================================================
// CHROMODYNAMIC — engine/asset/scene_streamer/tests/test_scene_streamer_async.cpp
// Phase 755 — cd::asset::scene_streamer async unit tests
// Band 6   — workers run the REAL glTF parse; completions carry LoadedScene.
//
// Tests write a real triangle .gltf fixture to a temp dir (same in-memory
// triangle pattern as test_scene_loader.cpp) so the worker parse produces a
// genuine node/mesh tree, proving real data flows end-to-end through the pool.
//
// Anti-flakiness: NO sleep_for anywhere. All waiting uses condition_variable
// via join_all() or join_pending() which block on a predicate.
//
// Tests:
//   A1  AsyncScenePool: parse 5 real fixtures; completed_count + poll match.
//   A2  AsyncScenePool: completed_count accumulates; second poll empty.
//   A3  AsyncScenePool: poll_completed non-blocking on un-configured pool.
//   A4  SceneStreamer async: tick + join_pending → completed_count == N.
//   A5  SceneStreamer async: is_loaded true + REAL node/mesh tree for each path.
//   A6  AsyncScenePool: a path that fails to parse is NOT completed.
//   A7  SceneStreamer async: pending_count reaches 0 after first tick().
// =============================================================================

#include <cd/asset/scene_streamer/SceneStreamer.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

using cd::asset::scene_streamer::AsyncScenePool;
using cd::asset::scene_streamer::SceneStreamer;
using cd::asset::scene_streamer::SceneStreamerConfig;
using cd::asset::scene_streamer::StreamRequest;

// ---- Triangle .gltf fixture (reuses the test_scene_loader.cpp pattern) ------

[[nodiscard]] std::vector<std::uint8_t> make_triangle_bin()
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

[[nodiscard]] std::string base64_encode(std::span<const std::uint8_t> in)
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size())
    {
        const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) |
                                (static_cast<std::uint32_t>(in[i + 1]) << 8) | in[i + 2];
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
        i += 3;
    }
    if (i < in.size())
    {
        std::uint32_t v = static_cast<std::uint32_t>(in[i]) << 16;
        if (i + 1 < in.size())
            v |= static_cast<std::uint32_t>(in[i + 1]) << 8;
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back((i + 1 < in.size()) ? kAlphabet[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

[[nodiscard]] std::string make_triangle_gltf_json()
{
    const auto bin = make_triangle_bin();
    const auto b64 = base64_encode(bin);
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

[[nodiscard]] fs::path tmp_gltf_path()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("cd_ss_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
            std::to_string(seq.fetch_add(1)) + ".gltf");
}

struct PathGuard
{
    fs::path path;

    explicit PathGuard(fs::path p)
        : path { std::move(p) }
    {
    }

    ~PathGuard()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }

    PathGuard(const PathGuard&)            = delete;
    PathGuard& operator=(const PathGuard&) = delete;
    PathGuard(PathGuard&&)                 = delete;
    PathGuard& operator=(PathGuard&&)      = delete;
};

void write_triangle_gltf(const fs::path& p)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    const std::string json = make_triangle_gltf_json();
    f.write(json.data(), static_cast<std::streamsize>(json.size()));
}

}  // namespace

// ---- A1: pool parses 5 real fixtures; completed_count + poll match ----------

TEST(SceneStreamerAsync, PoolParsesAllSubmittedFixtures)
{
    PathGuard g0 { tmp_gltf_path() };
    PathGuard g1 { tmp_gltf_path() };
    PathGuard g2 { tmp_gltf_path() };
    PathGuard g3 { tmp_gltf_path() };
    PathGuard g4 { tmp_gltf_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string(), g3.path.string(), g4.path.string()
    };
    for (const auto& p : paths)
    {
        write_triangle_gltf(fs::path { p });
    }

    AsyncScenePool pool;
    pool.configure(2U);
    for (const auto& p : paths)
    {
        pool.submit_async(StreamRequest{ p, 128U });
    }
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), paths.size());

    const auto done = pool.poll_completed();
    EXPECT_EQ(done.size(), paths.size());
    for (const auto& item : done)
    {
        // Real parse: one node, one mesh, one primitive, one material.
        EXPECT_EQ(item.scene.nodes.size(),     1U);
        EXPECT_EQ(item.scene.meshes.size(),    1U);
        EXPECT_EQ(item.scene.materials.size(), 1U);
        ASSERT_EQ(item.scene.meshes.size(),    1U);
        EXPECT_EQ(item.scene.meshes.front().primitives.size(), 1U);
    }
}

// ---- A2: completed_count accumulates; second poll empty ----------------------

TEST(SceneStreamerAsync, CompletedCountNeverDecrementsAcrossPolls)
{
    PathGuard g0 { tmp_gltf_path() };
    PathGuard g1 { tmp_gltf_path() };
    PathGuard g2 { tmp_gltf_path() };
    write_triangle_gltf(g0.path);
    write_triangle_gltf(g1.path);
    write_triangle_gltf(g2.path);

    AsyncScenePool pool;
    pool.configure(2U);
    pool.submit_async(StreamRequest{ g0.path.string(), 200U });
    pool.submit_async(StreamRequest{ g1.path.string(), 100U });
    pool.submit_async(StreamRequest{ g2.path.string(),  50U });
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), 3U);

    const auto first = pool.poll_completed();
    EXPECT_EQ(first.size(), 3U);

    const auto second = pool.poll_completed();
    EXPECT_TRUE(second.empty());

    EXPECT_EQ(pool.completed_count(), 3U);
}

// ---- A3: poll_completed non-blocking on un-configured pool ------------------

TEST(SceneStreamerAsync, PollCompletedNonBlockingOnEmptyPool)
{
    AsyncScenePool pool;
    const auto result = pool.poll_completed();
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(pool.completed_count(), 0U);
}

// ---- A4: SceneStreamer async tick + join_pending grows completed_count ------

TEST(SceneStreamerAsync, SceneStreamerAsyncTickGrowsCompletedCount)
{
    PathGuard g0 { tmp_gltf_path() };
    PathGuard g1 { tmp_gltf_path() };
    PathGuard g2 { tmp_gltf_path() };
    PathGuard g3 { tmp_gltf_path() };
    PathGuard g4 { tmp_gltf_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string(), g3.path.string(), g4.path.string()
    };
    for (const auto& p : paths)
    {
        write_triangle_gltf(fs::path { p });
    }

    SceneStreamer s { SceneStreamerConfig{ .use_async = true, .worker_count = 2U } };
    for (const auto& p : paths)
    {
        s.enqueue(StreamRequest{ p, 180U });
    }
    EXPECT_EQ(s.pending_count(), 5U);

    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 0U);

    s.tick(0.016F);
    s.tick(0.016F);
    s.join_pending();

    EXPECT_EQ(s.completed_count(), 5U);
}

// ---- A5: is_loaded true + REAL node/mesh tree for each path after join ------

TEST(SceneStreamerAsync, IsLoadedRealSceneTreeAfterJoinPending)
{
    PathGuard g0 { tmp_gltf_path() };
    PathGuard g1 { tmp_gltf_path() };
    PathGuard g2 { tmp_gltf_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string()
    };
    for (const auto& p : paths)
    {
        write_triangle_gltf(fs::path { p });
    }

    SceneStreamer s { SceneStreamerConfig{ .use_async = true, .worker_count = 2U } };
    for (const auto& p : paths)
    {
        s.enqueue(StreamRequest{ p, 128U });
    }

    s.tick(0.016F);
    s.join_pending();

    for (const auto& p : paths)
    {
        EXPECT_TRUE(s.is_loaded(p)) << "Expected loaded: " << p;
        const auto* scene = s.get_scene(p);
        ASSERT_NE(scene, nullptr) << "Expected parsed scene for: " << p;
        EXPECT_EQ(scene->nodes.size(),     1U);   // REAL parsed node tree
        EXPECT_EQ(scene->meshes.size(),    1U);
        EXPECT_EQ(scene->materials.size(), 1U);
        EXPECT_FALSE(scene->root_nodes.empty());
    }
    EXPECT_EQ(s.completed_count(), paths.size());
}

// ---- A6: a path that fails to parse is NOT reported completed ----------------

TEST(SceneStreamerAsync, FailedParseNotCompleted)
{
    AsyncScenePool pool;
    pool.configure(2U);
    pool.submit_async(StreamRequest{ "__missing_async__.glb", 200U });
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), 0U);
    EXPECT_TRUE(pool.poll_completed().empty());
}

// ---- A7: pending_count reaches 0 after first async tick() -------------------

TEST(SceneStreamerAsync, PendingCountZeroAfterFirstAsyncTick)
{
    PathGuard g0 { tmp_gltf_path() };
    PathGuard g1 { tmp_gltf_path() };
    PathGuard g2 { tmp_gltf_path() };
    write_triangle_gltf(g0.path);
    write_triangle_gltf(g1.path);
    write_triangle_gltf(g2.path);

    SceneStreamer s { SceneStreamerConfig{ .use_async = true, .worker_count = 2U } };
    s.enqueue(StreamRequest{ g0.path.string(), 255U });
    s.enqueue(StreamRequest{ g1.path.string(), 200U });
    s.enqueue(StreamRequest{ g2.path.string(), 150U });
    EXPECT_EQ(s.pending_count(), 3U);

    s.tick(0.016F);
    EXPECT_EQ(s.pending_count(), 0U);

    s.join_pending();
}
