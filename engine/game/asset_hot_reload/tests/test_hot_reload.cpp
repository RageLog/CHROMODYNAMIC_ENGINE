// =============================================================================
// CHROMODYNAMIC — tests/test_hot_reload.cpp
// Phase 502 — cd::game::asset_hot_reload::HotReloadBus unit tests.
//
// Each test runs against a unique scratch directory under the platform temp
// dir so parallel test execution does not collide. The tests inject the
// "now" timestamp into `tick(now)` to make throttle / coalesce behaviour
// deterministic (no `sleep_for`, per CLAUDE.md §5 anti-flakiness rule).
// =============================================================================
#include <cd/game/asset_hot_reload/HotReload.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

using cd::game::asset_hot_reload::AssetCategory;
using cd::game::asset_hot_reload::ChangeKind;
using cd::game::asset_hot_reload::HotReloadBus;
using cd::game::asset_hot_reload::ReloadEvent;
using cd::game::asset_hot_reload::SubscriptionId;

namespace fs = std::filesystem;

std::atomic<std::uint64_t> g_dir_seq { 0 };

fs::path make_unique_dir(std::string_view tag)
{
    const auto seq = g_dir_seq.fetch_add(1, std::memory_order_relaxed);
    const auto ts  = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = fs::temp_directory_path() /
               (std::string("cd_hot_reload_test_") + std::string(tag) +
                "_" + std::to_string(seq) + "_" + std::to_string(ts));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_text(const fs::path& p, std::string_view text)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    f.flush();
}

// Bump the file's mtime to a known-newer time so FileWatcher (which compares
// the std::filesystem mtime exactly) will observe an advance even when the
// host clock resolution is coarse. We add a healthy multi-second jump so the
// observation is unambiguous on every platform.
void bump_mtime(const fs::path& p, int seconds_forward)
{
    const auto now = fs::file_time_type::clock::now() +
                     std::chrono::seconds(seconds_forward);
    std::error_code ec;
    fs::last_write_time(p, now, ec);
    ASSERT_FALSE(ec) << "bump_mtime failed: " << ec.message();
}

}  // namespace

// =============================================================================
// 1) Watch file + modify -> callback fires
// =============================================================================
TEST(HotReloadBus, ModifyFiresCallback)
{
    auto dir  = make_unique_dir("modify");
    auto file = dir / "asset.txt";
    write_text(file, "initial");

    HotReloadBus bus(HotReloadBus::Duration { 0 });  // no throttle

    std::vector<ReloadEvent> events;
    auto id = bus.watch(file.string(), AssetCategory::kTexture,
        [&](const ReloadEvent& ev) {
            // The path_view backing belongs to bus internals — copy the
            // string for stable comparison after the tick returns.
            events.push_back(ReloadEvent {
                std::string_view {},  // placeholder; we keep ev.path's bytes
                ev.category, ev.kind
            });
            // We also separately verify the path text below.
            EXPECT_EQ(std::string(ev.path), file.string());
        });
    ASSERT_TRUE(id.is_valid());

    // First tick after a write that happened *before* watch() shouldn't fire
    // (FileWatcher establishes the baseline mtime in `watch()`).
    auto t0 = HotReloadBus::Clock::now();
    EXPECT_EQ(bus.tick(t0), 0U);
    EXPECT_TRUE(events.empty());

    // Now actually modify the file.
    bump_mtime(file, 10);

    auto t1 = t0 + std::chrono::milliseconds(1);
    const auto fired = bus.tick(t1);
    EXPECT_EQ(fired, 1U);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.front().category, AssetCategory::kTexture);
    EXPECT_EQ(events.front().kind, ChangeKind::kModified);
}

// =============================================================================
// 2) Unwatch stops callbacks
// =============================================================================
TEST(HotReloadBus, UnwatchStopsCallbacks)
{
    auto dir  = make_unique_dir("unwatch");
    auto file = dir / "asset.txt";
    write_text(file, "initial");

    HotReloadBus bus(HotReloadBus::Duration { 0 });

    int fired = 0;
    auto id = bus.watch(file.string(), AssetCategory::kMesh,
        [&](const ReloadEvent&) { ++fired; });
    ASSERT_TRUE(id.is_valid());

    // First mod fires.
    bump_mtime(file, 10);
    const auto t0 = HotReloadBus::Clock::now();
    EXPECT_EQ(bus.tick(t0), 1U);
    EXPECT_EQ(fired, 1);

    // Detach.
    const auto removed = bus.unwatch(file.string());
    EXPECT_EQ(removed, 1U);
    EXPECT_FALSE(bus.is_watching(file.string()));
    EXPECT_EQ(bus.subscriber_count(), 0U);

    // Subsequent mods do not fire.
    bump_mtime(file, 20);
    const auto t1 = t0 + std::chrono::milliseconds(10);
    EXPECT_EQ(bus.tick(t1), 0U);
    EXPECT_EQ(fired, 1);
}

// =============================================================================
// 3) Throttle: rapid writes coalesce to one callback per file per window
// =============================================================================
TEST(HotReloadBus, RapidWritesCoalesceWithinWindow)
{
    auto dir  = make_unique_dir("throttle");
    auto file = dir / "asset.txt";
    write_text(file, "initial");

    // Use a generous (200ms) window so we can express several ticks inside
    // and outside it deterministically.
    const auto window = HotReloadBus::Duration { 200 };
    HotReloadBus bus(window);

    int fired = 0;
    auto id = bus.watch(file.string(), AssetCategory::kShader,
        [&](const ReloadEvent&) { ++fired; });
    ASSERT_TRUE(id.is_valid());

    const auto t_base = HotReloadBus::Clock::now();

    // Three rapid mtime advances and three ticks, all inside the 200ms
    // window. We expect exactly ONE callback once the window elapses.
    bump_mtime(file, 10);
    bus.tick(t_base + std::chrono::milliseconds(10));   // not yet 200ms
    bump_mtime(file, 11);
    bus.tick(t_base + std::chrono::milliseconds(50));   // still inside
    bump_mtime(file, 12);
    bus.tick(t_base + std::chrono::milliseconds(120));  // still inside

    EXPECT_EQ(fired, 0)
        << "throttle window not elapsed yet — bus should still be holding";

    // Cross the window — single coalesced callback.
    bus.tick(t_base + std::chrono::milliseconds(250));
    EXPECT_EQ(fired, 1) << "burst of 3 writes coalesced to one callback";

    // A new write *after* the window starts a fresh pending. Tick once
    // immediately inside the next window — must NOT fire yet.
    bump_mtime(file, 20);
    bus.tick(t_base + std::chrono::milliseconds(260));
    EXPECT_EQ(fired, 1) << "fresh pending must wait the throttle window";

    // Elapse the next window — second coalesced callback.
    bus.tick(t_base + std::chrono::milliseconds(500));
    EXPECT_EQ(fired, 2);
}

// =============================================================================
// 4) Watching a non-existent file: callback fires on first create
// =============================================================================
TEST(HotReloadBus, NonExistentFileFiresOnFirstCreate)
{
    auto dir  = make_unique_dir("create");
    auto file = dir / "future.txt";   // not present yet

    HotReloadBus bus(HotReloadBus::Duration { 0 });

    int fired = 0;
    ChangeKind seen = ChangeKind::kDeleted;
    auto id = bus.watch(file.string(), AssetCategory::kAudio,
        [&](const ReloadEvent& ev) { ++fired; seen = ev.kind; });
    ASSERT_TRUE(id.is_valid());

    // No file -> no events.
    const auto t0 = HotReloadBus::Clock::now();
    EXPECT_EQ(bus.tick(t0), 0U);
    EXPECT_EQ(fired, 0);

    // Create the file.
    write_text(file, "now exists");

    // Next tick observes the creation transition.
    const auto t1 = t0 + std::chrono::milliseconds(5);
    const auto n  = bus.tick(t1);
    EXPECT_EQ(n, 1U);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(seen, ChangeKind::kModified)
        << "creation routes as kModified (subscribers should load the new file)";
}

// =============================================================================
// 5) Multiple watchers on same file fire all subscribers
// =============================================================================
TEST(HotReloadBus, MultipleSubscribersAllFire)
{
    auto dir  = make_unique_dir("multi");
    auto file = dir / "asset.txt";
    write_text(file, "initial");

    HotReloadBus bus(HotReloadBus::Duration { 0 });

    int a_count = 0;
    int b_count = 0;
    int c_count = 0;
    AssetCategory a_seen { AssetCategory::kMesh };
    AssetCategory b_seen { AssetCategory::kMesh };
    AssetCategory c_seen { AssetCategory::kMesh };

    auto ida = bus.watch(file.string(), AssetCategory::kTexture,
        [&](const ReloadEvent& ev) { ++a_count; a_seen = ev.category; });
    auto idb = bus.watch(file.string(), AssetCategory::kShader,
        [&](const ReloadEvent& ev) { ++b_count; b_seen = ev.category; });
    auto idc = bus.watch(file.string(), AssetCategory::kAudio,
        [&](const ReloadEvent& ev) { ++c_count; c_seen = ev.category; });

    ASSERT_TRUE(ida.is_valid());
    ASSERT_TRUE(idb.is_valid());
    ASSERT_TRUE(idc.is_valid());
    EXPECT_EQ(bus.watched_path_count(), 1U)
        << "three subscribers share one underlying FileWatcher entry";
    EXPECT_EQ(bus.subscriber_count(), 3U);

    bump_mtime(file, 10);
    const auto t0 = HotReloadBus::Clock::now();
    const auto fired = bus.tick(t0);

    EXPECT_EQ(fired, 3U) << "every subscriber on the path should be invoked";
    EXPECT_EQ(a_count, 1);
    EXPECT_EQ(b_count, 1);
    EXPECT_EQ(c_count, 1);
    EXPECT_EQ(a_seen, AssetCategory::kTexture);
    EXPECT_EQ(b_seen, AssetCategory::kShader);
    EXPECT_EQ(c_seen, AssetCategory::kAudio);

    // Detach just the middle one; the other two still fire on the next mod.
    EXPECT_TRUE(bus.unwatch_subscription(idb));
    EXPECT_EQ(bus.subscriber_count(), 2U);
    EXPECT_TRUE(bus.is_watching(file.string()));

    bump_mtime(file, 20);
    bus.tick(t0 + std::chrono::milliseconds(10));
    EXPECT_EQ(a_count, 2);
    EXPECT_EQ(b_count, 1) << "detached subscriber must not fire";
    EXPECT_EQ(c_count, 2);
}

// =============================================================================
// 6) Delete file: callback fires with deletion flag
// =============================================================================
TEST(HotReloadBus, DeleteFiresWithDeletionFlag)
{
    auto dir  = make_unique_dir("delete");
    auto file = dir / "asset.txt";
    write_text(file, "exists");

    HotReloadBus bus(HotReloadBus::Duration { 0 });

    std::vector<ChangeKind> seen_kinds;
    auto id = bus.watch(file.string(), AssetCategory::kLocale,
        [&](const ReloadEvent& ev) { seen_kinds.push_back(ev.kind); });
    ASSERT_TRUE(id.is_valid());

    const auto t0 = HotReloadBus::Clock::now();
    EXPECT_EQ(bus.tick(t0), 0U);
    EXPECT_TRUE(seen_kinds.empty());

    // Remove the file.
    std::error_code ec;
    fs::remove(file, ec);
    ASSERT_FALSE(ec) << "fs::remove failed: " << ec.message();

    // Next tick must surface kDeleted.
    const auto t1 = t0 + std::chrono::milliseconds(5);
    const auto fired = bus.tick(t1);
    EXPECT_EQ(fired, 1U);
    ASSERT_EQ(seen_kinds.size(), 1U);
    EXPECT_EQ(seen_kinds.front(), ChangeKind::kDeleted);

    // Re-creating the file fires kModified again.
    write_text(file, "back");
    const auto t2 = t1 + std::chrono::milliseconds(5);
    bus.tick(t2);
    ASSERT_EQ(seen_kinds.size(), 2U);
    EXPECT_EQ(seen_kinds.back(), ChangeKind::kModified);
}

// =============================================================================
// 7) Invalid inputs are rejected without crashing
// =============================================================================
TEST(HotReloadBus, InvalidInputsRejected)
{
    HotReloadBus bus;

    // Empty path -> invalid id, no watch registered.
    auto id_empty = bus.watch("", AssetCategory::kTexture,
        [](const ReloadEvent&) {});
    EXPECT_FALSE(id_empty.is_valid());
    EXPECT_EQ(bus.subscriber_count(), 0U);

    // Null callback -> invalid id.
    auto id_null = bus.watch("foo", AssetCategory::kTexture, {});
    EXPECT_FALSE(id_null.is_valid());
    EXPECT_EQ(bus.subscriber_count(), 0U);

    // Unwatching a never-registered path is a no-op (not a crash).
    EXPECT_EQ(bus.unwatch("never_seen"), 0U);
    EXPECT_FALSE(bus.unwatch_subscription(SubscriptionId { 12345 }));
}
