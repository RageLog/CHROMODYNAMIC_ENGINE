// =============================================================================
// CHROMODYNAMIC — cd::asset tests (Sprint S3.2)
// =============================================================================
#include <cd/asset/AssetId.hpp>
#include <cd/asset/AssetRegistry.hpp>
#include <cd/asset/AssetTag.hpp>
#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset/SchemaRegistry.hpp>
#include <cd/vfs/MemorySource.hpp>
#include <cd/vfs/VirtualFileSystem.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace
{

// --- AssetId --------------------------------------------------------------
TEST(AssetId, EmptyIsNull)
{
    cd::asset::AssetId id;
    EXPECT_FALSE(id.is_valid());
    EXPECT_FALSE(static_cast<bool>(id));
}

TEST(AssetId, HashIsStableAcrossCalls)
{
    auto a = cd::asset::AssetId::from_path("shaders/standard.vert");
    auto b = cd::asset::AssetId::from_path("shaders/standard.vert");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, cd::asset::AssetId::from_path("shaders/standard.frag"));
}

TEST(AssetId, ConstexprUsable)
{
    constexpr auto compile_time = cd::asset::AssetId::from_path("foo/bar.txt");
    static_assert(compile_time.value() != 0);
    auto run_time = cd::asset::AssetId::from_path("foo/bar.txt");
    EXPECT_EQ(compile_time, run_time);
}

// --- AssetRegistry --------------------------------------------------------
class AssetRegistryTest : public ::testing::Test
{
protected:
    cd::vfs::VirtualFileSystem vfs;
    std::shared_ptr<cd::vfs::MemorySource> mem = std::make_shared<cd::vfs::MemorySource>("mem");

    void SetUp() override
    {
        vfs.mount_back(mem);
    }
};

TEST_F(AssetRegistryTest, LoadTextSucceeds)
{
    mem->put_text("foo.txt", "hello");
    cd::asset::AssetRegistry reg { vfs };
    reg.register_loader(std::make_unique<cd::asset::TextAssetLoader>());
    EXPECT_EQ(reg.loader_count(), 1u);

    auto id = reg.load("text", "foo.txt");
    ASSERT_TRUE(id.has_value());
    auto* asset = reg.find(*id);
    ASSERT_NE(asset, nullptr);
    auto* txt = dynamic_cast<cd::asset::TextAsset*>(asset);
    ASSERT_NE(txt, nullptr);
    EXPECT_EQ(txt->text(), "hello");
    EXPECT_EQ(reg.cached_count(), 1u);
}

TEST_F(AssetRegistryTest, RepeatLoadIsCached)
{
    mem->put_text("a.txt", "abc");
    cd::asset::AssetRegistry reg { vfs };
    reg.register_loader(std::make_unique<cd::asset::TextAssetLoader>());

    auto id1 = reg.load("text", "a.txt");
    auto id2 = reg.load("text", "a.txt");
    ASSERT_TRUE(id1.has_value() && id2.has_value());
    EXPECT_EQ(*id1, *id2);
    EXPECT_EQ(reg.cached_count(), 1u);
}

TEST_F(AssetRegistryTest, UnknownTagRejected)
{
    mem->put_text("a.txt", "x");
    cd::asset::AssetRegistry reg { vfs };
    auto r = reg.load("nonexistent", "a.txt");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::asset_errors::Code::kNoLoaderForTag));
}

TEST_F(AssetRegistryTest, VfsMissingProducesError)
{
    cd::asset::AssetRegistry reg { vfs };
    reg.register_loader(std::make_unique<cd::asset::BytesAssetLoader>());
    auto r = reg.load("bytes", "missing.bin");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::asset_errors::Code::kVfsReadFailed));
}

TEST_F(AssetRegistryTest, ReleaseAndClear)
{
    mem->put_text("a.txt", "x");
    mem->put_text("b.txt", "y");
    cd::asset::AssetRegistry reg { vfs };
    reg.register_loader(std::make_unique<cd::asset::TextAssetLoader>());
    auto a = *reg.load("text", "a.txt");
    (void)reg.load("text", "b.txt");
    EXPECT_EQ(reg.cached_count(), 2u);
    reg.release(a);
    EXPECT_EQ(reg.cached_count(), 1u);
    reg.clear();
    EXPECT_EQ(reg.cached_count(), 0u);
}

TEST_F(AssetRegistryTest, InstallByPasses)
{
    cd::asset::AssetRegistry reg { vfs };
    auto id = cd::asset::AssetId::from_path("runtime.tag");
    reg.install(id, std::make_unique<cd::asset::TextAsset>(std::string { "injected" }));
    auto* txt = dynamic_cast<cd::asset::TextAsset*>(reg.find(id));
    ASSERT_NE(txt, nullptr);
    EXPECT_EQ(txt->text(), "injected");
}

// --- SchemaRegistry -------------------------------------------------------
TEST(SchemaRegistry, RegisterAndFind)
{
    cd::asset::SchemaRegistry reg;
    cd::asset::Schema s;
    s.name = "material.standard";
    s.version = 1;
    s.fields = {
        { "albedo",     cd::asset::FieldType::kVec4f,         false },
        { "roughness",  cd::asset::FieldType::kFloat,         false },
        { "normal_map", cd::asset::FieldType::kHandleTexture, true  },
    };
    reg.register_schema(std::move(s));
    EXPECT_EQ(reg.size(), 1u);
    auto* found = reg.find("material.standard");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->fields.size(), 3u);
    EXPECT_NE(found->find_field("albedo"), nullptr);
    EXPECT_EQ(found->find_field("missing"), nullptr);
}

TEST(SchemaRegistry, ValidatePayload)
{
    cd::asset::SchemaRegistry reg;
    cd::asset::Schema s;
    s.name = "event.tick";
    s.version = 1;
    s.fields = {
        { "dt",       cd::asset::FieldType::kFloat,  false },
        { "frame_id", cd::asset::FieldType::kUint64, false },
        { "debug",    cd::asset::FieldType::kString, true  },
    };
    reg.register_schema(std::move(s));

    std::vector<std::string> present_ok { "dt", "frame_id" };
    EXPECT_TRUE(reg.validate_payload("event.tick", present_ok).has_value());

    std::vector<std::string> present_with_optional { "dt", "frame_id", "debug" };
    EXPECT_TRUE(reg.validate_payload("event.tick", present_with_optional).has_value());

    std::vector<std::string> missing_required { "dt" };
    auto bad = reg.validate_payload("event.tick", missing_required);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, static_cast<std::uint32_t>(cd::asset::schema_errors::Code::kMissingRequiredField));
}

TEST(SchemaRegistry, UnknownSchemaRejected)
{
    cd::asset::SchemaRegistry reg;
    auto r = reg.validate_payload("nope", {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::schema_errors::Code::kUnknownSchema));
}

}  // namespace

// --- AssetRegistry::tag_from_extension (backlog) ---------------------------

TEST(AssetRegistry, TagFromExtensionRecognisesKnownFormats)
{
    using cd::asset::AssetRegistry;
    EXPECT_EQ(AssetRegistry::tag_from_extension("data/scene.json"), "json");
    EXPECT_EQ(AssetRegistry::tag_from_extension("textures/wall.PNG"), "image");
    EXPECT_EQ(AssetRegistry::tag_from_extension("env.hdr"), "image");
    EXPECT_EQ(AssetRegistry::tag_from_extension("model.GLB"), "gltf");
    EXPECT_EQ(AssetRegistry::tag_from_extension("rocks.gltf"), "gltf");
    EXPECT_EQ(AssetRegistry::tag_from_extension("cube.obj"), "obj");
    EXPECT_EQ(AssetRegistry::tag_from_extension("tex.ktx2"), "ktx2");
    EXPECT_EQ(AssetRegistry::tag_from_extension("mesh.cdmesh"), "cdmesh");
    EXPECT_EQ(AssetRegistry::tag_from_extension("tex.cdtex"), "cdtex");
    EXPECT_EQ(AssetRegistry::tag_from_extension("sfx/clip.wav"), "wav");
    EXPECT_EQ(AssetRegistry::tag_from_extension("README"), std::string_view {});
    EXPECT_EQ(AssetRegistry::tag_from_extension("blob.unknown"), std::string_view {});
}

// ---------------------------------------------------------------------------
// Phase 16.C — FileWatcher tests (Wave 173)
// ---------------------------------------------------------------------------
#include <cd/asset/FileWatcher.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
std::filesystem::path fw_tmp_path(const char* suffix)
{
    return std::filesystem::temp_directory_path() /
           (std::string { "cd_filewatcher_" } + suffix + "_" +
            std::to_string(static_cast<unsigned long long>(
                std::chrono::steady_clock::now().time_since_epoch().count())));
}
}

TEST(FileWatcher, FreshFileDoesNotFireOnFirstPoll)
{
    const auto p = fw_tmp_path("a");
    { std::ofstream f(p); f << "hello"; }

    int fired = 0;
    cd::asset::FileWatcher fw;
    fw.watch(p.string(), [&](std::string_view) { ++fired; });

    EXPECT_EQ(fw.poll(), 0u);
    EXPECT_EQ(fired, 0);

    std::filesystem::remove(p);
}

TEST(FileWatcher, FiresOnMtimeAdvance)
{
    const auto p = fw_tmp_path("b");
    { std::ofstream f(p); f << "v1"; }

    int fired = 0;
    cd::asset::FileWatcher fw;
    fw.watch(p.string(), [&](std::string_view) { ++fired; });

    std::filesystem::last_write_time(p,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds { 1 });

    EXPECT_EQ(fw.poll(), 1u);
    EXPECT_EQ(fired, 1);

    std::filesystem::remove(p);
}

TEST(FileWatcher, UnwatchStopsCallbacks)
{
    const auto p = fw_tmp_path("c");
    { std::ofstream f(p); f << "x"; }

    int fired = 0;
    cd::asset::FileWatcher fw;
    fw.watch(p.string(), [&](std::string_view) { ++fired; });
    fw.unwatch(p.string());

    std::filesystem::last_write_time(p,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds { 1 });
    EXPECT_EQ(fw.poll(), 0u);
    EXPECT_EQ(fired, 0);

    std::filesystem::remove(p);
}

TEST(AssetTag, MakeTagHashesString)
{
    constexpr auto t = cd::asset::make_tag("character");
    EXPECT_NE(t.hash, 0u);
    // FNV-1a is deterministic.
    EXPECT_EQ(cd::asset::make_tag("character"), cd::asset::make_tag("character"));
    EXPECT_NE(cd::asset::make_tag("character"), cd::asset::make_tag("enemy"));
}

TEST(AssetTagSet, AddAndContainsRoundTrip)
{
    cd::asset::AssetTagSet s;
    s.add(cd::asset::make_tag("hero"));
    s.add(cd::asset::make_tag("tier3"));
    EXPECT_TRUE(s.contains(cd::asset::make_tag("hero")));
    EXPECT_TRUE(s.contains(cd::asset::make_tag("tier3")));
    EXPECT_FALSE(s.contains(cd::asset::make_tag("missing")));
    EXPECT_EQ(s.size(), 2u);
}

TEST(AssetTagSet, DuplicateAddIsNoOp)
{
    cd::asset::AssetTagSet s;
    s.add(cd::asset::make_tag("dup"));
    s.add(cd::asset::make_tag("dup"));
    EXPECT_EQ(s.size(), 1u);
}

TEST(AssetTagSet, RemoveDropsEntry)
{
    cd::asset::AssetTagSet s;
    s.add(cd::asset::make_tag("a"));
    s.add(cd::asset::make_tag("b"));
    s.remove(cd::asset::make_tag("a"));
    EXPECT_FALSE(s.contains(cd::asset::make_tag("a")));
    EXPECT_TRUE(s.contains(cd::asset::make_tag("b")));
    EXPECT_EQ(s.size(), 1u);
}

#include <cd/asset/MemoryCache.hpp>

TEST(MemoryCache, EmptyCacheIsZero)
{
    cd::asset::MemoryCache c;
    EXPECT_EQ(c.total_bytes(), 0u);
    EXPECT_EQ(c.size(), 0u);
}

TEST(MemoryCache, TouchAccumulatesBytes)
{
    cd::asset::MemoryCache c;
    c.touch(cd::asset::AssetId::from_path("a"), 100);
    c.touch(cd::asset::AssetId::from_path("b"), 200);
    EXPECT_EQ(c.total_bytes(), 300u);
    EXPECT_EQ(c.size(), 2u);
}

TEST(MemoryCache, ForgetReleasesBytes)
{
    cd::asset::MemoryCache c;
    auto id_a = cd::asset::AssetId::from_path("a");
    c.touch(id_a, 100);
    c.forget(id_a);
    EXPECT_EQ(c.total_bytes(), 0u);
    EXPECT_EQ(c.size(), 0u);
}

TEST(MemoryCache, VictimsReturnsOldestFirst)
{
    cd::asset::MemoryCache c;
    auto id_a = cd::asset::AssetId::from_path("a");
    auto id_b = cd::asset::AssetId::from_path("b");
    auto id_c = cd::asset::AssetId::from_path("c");
    c.touch(id_a, 50);
    c.touch(id_b, 50);
    c.touch(id_c, 50);
    // Re-touch a — it becomes newest.
    c.touch(id_a, 50);
    auto v = c.victims(60);
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(v[0], id_b);   // oldest after re-touch
}

TEST(AssetRegistryEvict, DropsLruVictimsAndClearsMemoryCache)
{
    // Phase 113 — `AssetRegistry::evict()` consults a MemoryCache for
    // LRU victim selection, then drops them from both the registry
    // cache and the memory cache.
    cd::vfs::VirtualFileSystem vfs;
    cd::asset::AssetRegistry registry { vfs };
    cd::asset::MemoryCache mem_cache;

    const auto id_old = cd::asset::AssetId::from_path("oldest");
    const auto id_mid = cd::asset::AssetId::from_path("mid");
    const auto id_new = cd::asset::AssetId::from_path("newest");

    registry.install(id_old, std::make_unique<cd::asset::TextAsset>("oldest"));
    mem_cache.touch(id_old, 1024);
    registry.install(id_mid, std::make_unique<cd::asset::TextAsset>("mid"));
    mem_cache.touch(id_mid, 1024);
    registry.install(id_new, std::make_unique<cd::asset::TextAsset>("newest"));
    mem_cache.touch(id_new, 1024);

    EXPECT_EQ(registry.cached_count(), 3u);
    EXPECT_EQ(mem_cache.total_bytes(), 3072u);

    // Request 1500 bytes — should evict the two oldest (oldest + mid).
    const std::size_t n = registry.evict(mem_cache, /*release_bytes=*/1500);
    EXPECT_EQ(n, 2u);
    EXPECT_EQ(registry.cached_count(), 1u);
    EXPECT_EQ(mem_cache.total_bytes(), 1024u);
    EXPECT_TRUE(registry.find(id_new) != nullptr);   // newest survives
    EXPECT_TRUE(registry.find(id_old) == nullptr);
    EXPECT_TRUE(registry.find(id_mid) == nullptr);
}

TEST(AssetRegistryEvict, ZeroBytesIsNoOp)
{
    cd::vfs::VirtualFileSystem vfs;
    cd::asset::AssetRegistry registry { vfs };
    cd::asset::MemoryCache mem_cache;
    const auto id = cd::asset::AssetId::from_path("only");
    registry.install(id, std::make_unique<cd::asset::TextAsset>("only"));
    mem_cache.touch(id, 100);
    const std::size_t n = registry.evict(mem_cache, 0);
    EXPECT_EQ(n, 0u);
    EXPECT_EQ(registry.cached_count(), 1u);
    EXPECT_EQ(mem_cache.total_bytes(), 100u);
}

#include <cd/asset/AsyncStreamer.hpp>

#include <atomic>
#include <chrono>
#include <thread>

TEST(AsyncStreamer, EnqueueAndComplete)
{
    std::atomic<int> load_calls { 0 };
    cd::asset::AsyncStreamer streamer {
        [&load_calls](cd::asset::AssetId) {
            ++load_calls;
            return true;  // success
        }
    };
    streamer.start();
    const auto id_a = cd::asset::AssetId::from_path("a");
    const auto id_b = cd::asset::AssetId::from_path("b");
    streamer.enqueue(cd::asset::StreamRequest { id_a, 1, 0 });
    streamer.enqueue(cd::asset::StreamRequest { id_b, 5, 0 });

    // Wait up to 1 second for both to finish.
    for (int i = 0; i < 100; ++i)
    {
        if (load_calls.load() >= 2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    streamer.stop();

    EXPECT_GE(load_calls.load(), 2);
    EXPECT_EQ(streamer.state_of(id_a), cd::asset::StreamState::kComplete);
    EXPECT_EQ(streamer.state_of(id_b), cd::asset::StreamState::kComplete);
}

TEST(AsyncStreamer, FailureMarksFailed)
{
    cd::asset::AsyncStreamer streamer {
        [](cd::asset::AssetId) { return false; }
    };
    streamer.start();
    const auto id = cd::asset::AssetId::from_path("doomed");
    streamer.enqueue(cd::asset::StreamRequest { id, 0, 0 });
    for (int i = 0; i < 100; ++i)
    {
        if (streamer.state_of(id) == cd::asset::StreamState::kFailed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    streamer.stop();
    EXPECT_EQ(streamer.state_of(id), cd::asset::StreamState::kFailed);
}

TEST(AsyncStreamer, StopWithoutStartIsNoop)
{
    cd::asset::AsyncStreamer streamer { [](cd::asset::AssetId) { return true; } };
    streamer.stop();  // should not crash, no thread started
    EXPECT_FALSE(streamer.is_running());
}

#include <cd/asset/StreamRequest.hpp>

TEST(StreamQueue, EmptyOnConstruction)
{
    cd::asset::StreamQueue q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(StreamQueue, PopReturnsHighestPriorityFirst)
{
    cd::asset::StreamQueue q;
    q.push(cd::asset::StreamRequest { cd::asset::AssetId::from_path("a"), 1, 0 });
    q.push(cd::asset::StreamRequest { cd::asset::AssetId::from_path("b"), 5, 0 });
    q.push(cd::asset::StreamRequest { cd::asset::AssetId::from_path("c"), 2, 0 });
    auto r = q.pop_top();
    EXPECT_EQ(r.priority, 5);
    EXPECT_EQ(r.id, cd::asset::AssetId::from_path("b"));
}

TEST(StreamQueue, PopMarksInflight)
{
    cd::asset::StreamQueue q;
    auto id = cd::asset::AssetId::from_path("x");
    q.push(cd::asset::StreamRequest { id, 0, 0 });
    EXPECT_EQ(q.state_of(id), cd::asset::StreamState::kPending);
    (void)q.pop_top();
    EXPECT_EQ(q.state_of(id), cd::asset::StreamState::kInflight);
}

TEST(StreamQueue, MarkCompleteUpdatesState)
{
    cd::asset::StreamQueue q;
    auto id = cd::asset::AssetId::from_path("y");
    q.push(cd::asset::StreamRequest { id, 0, 0 });
    (void)q.pop_top();
    q.mark_complete(id);
    EXPECT_EQ(q.state_of(id), cd::asset::StreamState::kComplete);
}

TEST(StreamQueue, MarkFailedUpdatesState)
{
    cd::asset::StreamQueue q;
    auto id = cd::asset::AssetId::from_path("z");
    q.push(cd::asset::StreamRequest { id, 0, 0 });
    (void)q.pop_top();
    q.mark_failed(id);
    EXPECT_EQ(q.state_of(id), cd::asset::StreamState::kFailed);
}

#include <cd/asset/LoadProfile.hpp>

TEST(LoadProfile, RecordsSamples)
{
    cd::asset::LoadProfile p;
    auto id = cd::asset::AssetId::from_path("model.gltf");
    p.record(id, 1000);
    p.record(id, 3000);
    EXPECT_EQ(p.count(id), 2u);
    EXPECT_EQ(p.total_us(id), 4000u);
    EXPECT_EQ(p.mean_us(id), 2000u);
}

TEST(LoadProfile, SlowestNReturnsTopByDuration)
{
    cd::asset::LoadProfile p;
    p.record(cd::asset::AssetId::from_path("a"), 100);
    p.record(cd::asset::AssetId::from_path("b"), 500);
    p.record(cd::asset::AssetId::from_path("c"), 200);
    auto top = p.slowest(2);
    ASSERT_EQ(top.size(), 2u);
    EXPECT_EQ(top[0].duration_us, 500u);
    EXPECT_EQ(top[1].duration_us, 200u);
}

TEST(LoadProfile, UnseenIdReturnsZero)
{
    cd::asset::LoadProfile p;
    auto unknown = cd::asset::AssetId::from_path("nope");
    EXPECT_EQ(p.count(unknown), 0u);
    EXPECT_EQ(p.total_us(unknown), 0u);
    EXPECT_EQ(p.mean_us(unknown), 0u);
}

TEST(LoadProfile, ClearEmptiesEverything)
{
    cd::asset::LoadProfile p;
    p.record(cd::asset::AssetId::from_path("a"), 100);
    p.clear();
    EXPECT_EQ(p.sample_count(), 0u);
}

#include <cd/asset/HotReloadQueue.hpp>

TEST(HotReloadQueue, DuplicateNotifyCoalesces)
{
    cd::asset::HotReloadQueue q;
    auto id = cd::asset::AssetId::from_path("a.png");
    q.notify(id);
    q.notify(id);
    q.notify(id);
    EXPECT_EQ(q.pending_count(), 1u);
    EXPECT_TRUE(q.is_pending(id));
}

TEST(HotReloadQueue, DrainEmptiesQueueAndFiresEachOnce)
{
    cd::asset::HotReloadQueue q;
    q.notify(cd::asset::AssetId::from_path("a"));
    q.notify(cd::asset::AssetId::from_path("b"));
    q.notify(cd::asset::AssetId::from_path("c"));
    int fired = 0;
    q.drain([&](cd::asset::AssetId) { ++fired; });
    EXPECT_EQ(fired, 3);
    EXPECT_EQ(q.pending_count(), 0u);
}

TEST(HotReloadQueue, InvalidAssetIdIgnored)
{
    cd::asset::HotReloadQueue q;
    q.notify(cd::asset::AssetId {});
    EXPECT_EQ(q.pending_count(), 0u);
}

TEST(HotReloadQueue, ClearWithoutDrain)
{
    cd::asset::HotReloadQueue q;
    q.notify(cd::asset::AssetId::from_path("x"));
    q.clear();
    EXPECT_EQ(q.pending_count(), 0u);
}

#include <cd/asset/PathResolver.hpp>

TEST(PathResolver, NoVariablesPassesThrough)
{
    cd::asset::PathResolver r;
    EXPECT_EQ(r.expand("plain/path/file.png"), "plain/path/file.png");
}

TEST(PathResolver, SubstitutesKnownVariables)
{
    cd::asset::PathResolver r;
    r.set("LEVEL", "level01");
    r.set("REGION", "tundra");
    EXPECT_EQ(r.expand("{LEVEL}/textures/{REGION}.cdtex"),
              "level01/textures/tundra.cdtex");
}

TEST(PathResolver, UnknownVariableLeftLiteral)
{
    cd::asset::PathResolver r;
    EXPECT_EQ(r.expand("{UNKNOWN}/file"), "{UNKNOWN}/file");
}

TEST(PathResolver, UnclosedBraceLeftAsIs)
{
    cd::asset::PathResolver r;
    EXPECT_EQ(r.expand("a/b/{NOEND"), "a/b/{NOEND");
}

#include <cd/asset/BundleMeta.hpp>

TEST(BundleMeta, HeaderSizeIs32Bytes)
{
    EXPECT_EQ(sizeof(cd::asset::BundleHeader), 32u);
}

TEST(BundleMeta, ValidHeaderAccepted)
{
    cd::asset::BundleHeader h;
    h.magic = cd::asset::kBundleMagicMesh;
    h.payload_size = 1024;
    h.asset_count = 1;
    EXPECT_TRUE(cd::asset::is_valid_header(h, cd::asset::kBundleMagicMesh));
}

TEST(BundleMeta, MagicMismatchRejected)
{
    cd::asset::BundleHeader h;
    h.magic = cd::asset::kBundleMagicMesh;
    h.payload_size = 1024;
    h.asset_count = 1;
    EXPECT_FALSE(cd::asset::is_valid_header(h, cd::asset::kBundleMagicTexture));
}

TEST(BundleMeta, ZeroAssetCountRejected)
{
    cd::asset::BundleHeader h;
    h.magic = cd::asset::kBundleMagicScene;
    h.payload_size = 100;
    h.asset_count = 0;
    EXPECT_FALSE(cd::asset::is_valid_header(h, cd::asset::kBundleMagicScene));
}

TEST(BundleMeta, OversizedPayloadRejected)
{
    cd::asset::BundleHeader h;
    h.magic = cd::asset::kBundleMagicMesh;
    h.payload_size = 1ULL << 40;
    h.asset_count = 1;
    EXPECT_FALSE(cd::asset::is_valid_header(h, cd::asset::kBundleMagicMesh));
}

#include <cd/asset/DependencyGraph.hpp>

TEST(DependencyGraph, OneHopDependents)
{
    cd::asset::DependencyGraph g;
    auto mat = cd::asset::AssetId::from_path("mat.json");
    auto tex = cd::asset::AssetId::from_path("tex.png");
    g.depend(mat, tex);   // mat depends on tex
    auto deps = g.dependents_of(tex);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], mat);
}

TEST(DependencyGraph, NoEdgesEmptyDependents)
{
    cd::asset::DependencyGraph g;
    auto x = cd::asset::AssetId::from_path("x");
    EXPECT_TRUE(g.dependents_of(x).empty());
}

TEST(DependencyGraph, TransitiveDependents)
{
    cd::asset::DependencyGraph g;
    auto scene = cd::asset::AssetId::from_path("scene.json");
    auto mat   = cd::asset::AssetId::from_path("mat.json");
    auto tex   = cd::asset::AssetId::from_path("tex.png");
    // scene depends on mat, mat depends on tex
    g.depend(scene, mat);
    g.depend(mat, tex);
    auto t = g.transitive_dependents(tex);
    // Both mat and scene should be in the transitive closure.
    EXPECT_EQ(t.size(), 2u);
}

TEST(DependencyGraph, InvalidIdRejected)
{
    cd::asset::DependencyGraph g;
    g.depend(cd::asset::AssetId {}, cd::asset::AssetId::from_path("x"));
    g.depend(cd::asset::AssetId::from_path("y"), cd::asset::AssetId {});
    EXPECT_EQ(g.edge_node_count(), 0u);
}

#include <cd/asset/AssetRefCount.hpp>

TEST(AssetRefCount, AcquireIncrements)
{
    cd::asset::AssetRefCount rc;
    auto id = cd::asset::AssetId::from_path("tex.png");
    EXPECT_EQ(rc.acquire(id), 1);
    EXPECT_EQ(rc.acquire(id), 2);
    EXPECT_EQ(rc.count_of(id), 2);
}

TEST(AssetRefCount, ReleaseDecrements)
{
    cd::asset::AssetRefCount rc;
    auto id = cd::asset::AssetId::from_path("mesh.bin");
    rc.acquire(id);
    rc.acquire(id);
    EXPECT_EQ(rc.release(id), 1);
    EXPECT_EQ(rc.release(id), 0);
    EXPECT_FALSE(rc.is_held(id));
}

TEST(AssetRefCount, OverReleaseSafe)
{
    cd::asset::AssetRefCount rc;
    auto id = cd::asset::AssetId::from_path("a");
    EXPECT_EQ(rc.release(id), 0);   // never acquired
    rc.acquire(id);
    EXPECT_EQ(rc.release(id), 0);   // back to zero
    EXPECT_EQ(rc.release(id), 0);   // extra release no-op
}

TEST(AssetRefCount, InvalidIdRejected)
{
    cd::asset::AssetRefCount rc;
    EXPECT_EQ(rc.acquire(cd::asset::AssetId {}), 0);
    EXPECT_EQ(rc.tracked_count(), 0u);
}
