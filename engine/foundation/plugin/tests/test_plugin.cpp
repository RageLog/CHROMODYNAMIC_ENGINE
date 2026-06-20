// =============================================================================
// CHROMODYNAMIC — cd::plugin tests (Sprint S2.4)
//
// Note: full end-to-end loader tests (compile a tiny test DLL, dlopen it) live
// in Sprint S2.4+. v1 here exercises the error paths and the IPlugin interface
// surface that callers will implement.
// =============================================================================
#include <cd/plugin/FileWatcher.hpp>
#include <cd/plugin/HotReload.hpp>
#include <cd/plugin/IPlugin.hpp>
#include <cd/plugin/Loader.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace
{

TEST(PluginIPlugin, AbiVersionStable)
{
    EXPECT_GE(cd::plugin::kAbiVersion, 1u);
}

TEST(PluginIPlugin, EntrySymbolNonEmpty)
{
    EXPECT_FALSE(cd::plugin::kEntrySymbol.empty());
}

TEST(PluginLoader, LoadNonexistentFails)
{
    cd::plugin::Loader loader;
    auto r = loader.load("nonexistent_plugin_zxyq.dll");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::plugin::loader_errors::Code::kLoadFailed));
}

TEST(PluginLoader, LoadedPathsTracksSuccesses)
{
    cd::plugin::Loader loader;
    EXPECT_TRUE(loader.loaded_paths().empty());
    auto r = loader.load("definitely-not-here.so");
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(loader.loaded_paths().empty());
}

// -----------------------------------------------------------------------------
// HotReload — Wave 35
// Tests use injected mock load + version functions so we don't need a
// real on-disk DLL. The mocks fabricate a LoadedPlugin around a
// trivial IPlugin implementation.
// -----------------------------------------------------------------------------

class FakePlugin final : public cd::plugin::IPlugin
{
public:
    explicit FakePlugin(std::uint32_t id) noexcept : id_ { id } {}
    [[nodiscard]] cd::plugin::PluginInfo info() const noexcept override
    {
        return { "fake", "1.0", "test", cd::plugin::kAbiVersion };
    }
    [[nodiscard]] bool initialize() noexcept override { return true; }
    void shutdown() noexcept override {}
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

private:
    std::uint32_t id_;
};

namespace
{

cd::plugin::LoadedPlugin make_fake_loaded(std::uint32_t id, std::string path)
{
    cd::plugin::LoadedPlugin lp;
    lp.instance = std::make_unique<FakePlugin>(id);
    lp.native_handle = nullptr;
    lp.path = std::move(path);
    return lp;
}

}  // namespace

TEST(HotReload, MountThenPollNoChangeReturnsFalse)
{
    std::uint64_t fake_version = 42;
    std::uint32_t next_id = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    ASSERT_TRUE(hr.mount("dummy/path").has_value());
    EXPECT_TRUE(hr.is_mounted());
    EXPECT_EQ(hr.current_version(), 42U);
    EXPECT_EQ(hr.reload_count(), 0U);
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r);
    EXPECT_EQ(hr.reload_count(), 0U);
}

TEST(HotReload, VersionChangeTriggersReload)
{
    std::uint64_t fake_version = 1;
    std::uint32_t next_id = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    ASSERT_TRUE(hr.mount("plugin.dll").has_value());
    auto* first = dynamic_cast<FakePlugin*>(hr.current());
    ASSERT_NE(first, nullptr);
    const auto first_id = first->id();

    fake_version = 2;  // simulate disk update
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ(hr.reload_count(), 1U);
    auto* second = dynamic_cast<FakePlugin*>(hr.current());
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second->id(), first_id);  // fresh instance
    EXPECT_EQ(hr.current_version(), 2U);
}

TEST(HotReload, BeforeUnloadAndAfterLoadFireInOrder)
{
    std::uint64_t fake_version = 100;
    std::uint32_t next_id = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    int unload_calls = 0;
    int load_calls = 0;
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unload_calls; });
    hr.set_after_load([&](cd::plugin::IPlugin&) { ++load_calls; });

    ASSERT_TRUE(hr.mount("p.dll").has_value());
    EXPECT_EQ(load_calls, 1);  // initial mount → after_load
    EXPECT_EQ(unload_calls, 0);

    fake_version = 200;
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ(unload_calls, 1);  // old plugin unloaded
    EXPECT_EQ(load_calls, 2);    // new plugin loaded
}

TEST(HotReload, FailedReloadKeepsPreviousPluginMounted)
{
    std::uint64_t fake_version = 1;
    std::uint32_t next_id = 0;
    bool fail_next = false;
    int unload_calls = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            if (fail_next)
                return std::unexpected(
                    cd::plugin::loader_errors::make(cd::plugin::loader_errors::Code::kFactoryFailed));
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unload_calls; });
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    const auto first_id = dynamic_cast<FakePlugin*>(hr.current())->id();

    fake_version = 2;
    fail_next = true;
    auto r = hr.poll();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::loader_errors::Code::kFactoryFailed));
    // The previous plugin must still be live and untouched — the load
    // attempt happens BEFORE the unload, so a failed reload never
    // leaves the host without a plugin.
    ASSERT_TRUE(hr.is_mounted());
    EXPECT_EQ(unload_calls, 0);  // teardown never fired
    EXPECT_EQ(hr.reload_count(), 0U);
    auto* still_first = dynamic_cast<FakePlugin*>(hr.current());
    ASSERT_NE(still_first, nullptr);
    EXPECT_EQ(still_first->id(), first_id);
}

TEST(HotReload, PollBeforeMountReturnsError)
{
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(1, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    auto r = hr.poll();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::hot_reload_errors::Code::kNotMounted));
}

TEST(HotReload, DefaultPluginVersionReturnsZeroForMissingFile)
{
    EXPECT_EQ(cd::plugin::default_plugin_version("this_path_does_not_exist_zxyq.dll"), 0U);
}

// -----------------------------------------------------------------------------
// FileWatcher — Wave 38
// Uses the polling impl with interval=0 so we can drive ticks
// deterministically via poll_once() and never block the test on a real
// sleep. A small RAII fixture writes a temp file we can rewrite to
// simulate a change.
// -----------------------------------------------------------------------------

class TempFile
{
public:
    TempFile()
    {
        path_ = std::filesystem::temp_directory_path()
              / ("cd_watcher_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))
                 + ".bin");
        write("v0");
    }
    ~TempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    void write(std::string_view contents)
    {
        std::ofstream out { path_, std::ios::binary | std::ios::trunc };
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        // Bump mtime explicitly — some filesystems coarse-grain mtime to
        // ~1 s, which would defeat back-to-back rewrites in a single test.
        std::error_code ec;
        std::filesystem::last_write_time(
            path_, std::filesystem::file_time_type::clock::now() + std::chrono::seconds { 1 },
            ec);
    }

    [[nodiscard]] std::string path_str() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

TEST(FileWatcher, PollOnceFiresOnMtimeChange)
{
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { ++fires; }).has_value());
    EXPECT_TRUE(w->is_watching());
    EXPECT_EQ(w->change_count(), 0U);

    // First poll: nothing changed since watch(), no fire.
    w->poll_once();
    EXPECT_EQ(fires.load(), 0);

    // Rewrite the file → mtime advances → next poll fires once.
    f.write("v1");
    w->poll_once();
    EXPECT_EQ(fires.load(), 1);
    EXPECT_EQ(w->change_count(), 1U);

    // Idempotent: another poll without change does not fire.
    w->poll_once();
    EXPECT_EQ(fires.load(), 1);
    w->stop();
    EXPECT_FALSE(w->is_watching());
}

TEST(FileWatcher, WatchMissingFileReturnsError)
{
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    auto r = w->watch("definitely-not-here-zxyq.txt", [] {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::watcher_errors::Code::kFileNotFound));
}

TEST(FileWatcher, AlreadyWatchingReturnsError)
{
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    ASSERT_TRUE(w->watch(f.path_str(), [] {}).has_value());
    auto second = w->watch(f.path_str(), [] {});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code,
              static_cast<std::uint32_t>(cd::plugin::watcher_errors::Code::kAlreadyWatching));
}

TEST(FileWatcher, NativeFactoryReturnsNonNull)
{
    auto w = cd::plugin::make_native_file_watcher();
    ASSERT_NE(w, nullptr);
    EXPECT_FALSE(w->is_watching());
}

TEST(FileWatcher, BackgroundThreadFiresWithinTimeout)
{
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 25 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { ++fires; }).has_value());

    // Bump the file; the background poll should pick it up.
    std::this_thread::sleep_for(std::chrono::milliseconds { 30 });
    f.write("v1");
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds { 1000 };
    while (fires.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds { 10 });
    EXPECT_GE(fires.load(), 1);
    w->stop();
    EXPECT_FALSE(w->is_watching());
}

// --- FileWatcher edge / negative (deterministic, interval==0) ----------------

TEST(FileWatcher, MultipleModifyEachFireOncePerPoll)
{
    // "modify" detection across several consecutive rewrites: every
    // distinct mtime advance is exactly one fire; change_count accumulates.
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { ++fires; }).has_value());

    f.write("a");
    w->poll_once();
    f.write("b");
    w->poll_once();
    f.write("c");
    w->poll_once();
    EXPECT_EQ(fires.load(), 3);
    EXPECT_EQ(w->change_count(), 3U);
    w->stop();
}

TEST(FileWatcher, ThrottleMultiPollSingleChangeFiresOnce)
{
    // "throttle": many polls between two real changes fire only on the
    // poll that observes the new mtime — no duplicate fires.
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { ++fires; }).has_value());

    for (int i = 0; i < 5; ++i)
        w->poll_once();  // no change yet
    EXPECT_EQ(fires.load(), 0);

    f.write("changed");
    for (int i = 0; i < 5; ++i)
        w->poll_once();  // change observed once, then quiescent
    EXPECT_EQ(fires.load(), 1);
    EXPECT_EQ(w->change_count(), 1U);
    w->stop();
}

TEST(FileWatcher, DeleteThenRecreateDetectedAsChange)
{
    // "delete" + re-"add": removing the file makes stat() return 0 (which
    // the watcher treats as "unknown, do not fire"); recreating it with a
    // fresh mtime is detected as a change on the next poll.
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { ++fires; }).has_value());

    // Delete: version goes to 0 -> guarded, no fire.
    {
        std::error_code ec;
        std::filesystem::remove(f.path_str(), ec);
    }
    w->poll_once();
    EXPECT_EQ(fires.load(), 0);

    // Recreate (add) with an advanced mtime -> detected.
    f.write("reborn");
    w->poll_once();
    EXPECT_EQ(fires.load(), 1);
    w->stop();
}

TEST(FileWatcher, PollAfterStopIsNoOp)
{
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { ++fires; }).has_value());
    w->stop();
    EXPECT_FALSE(w->is_watching());

    f.write("after-stop");
    w->poll_once();  // not watching -> guarded no-op
    EXPECT_EQ(fires.load(), 0);
}

TEST(FileWatcher, StopIsIdempotent)
{
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    ASSERT_TRUE(w->watch(f.path_str(), [] {}).has_value());
    w->stop();
    w->stop();  // second stop must be a safe no-op
    EXPECT_FALSE(w->is_watching());
}

TEST(FileWatcher, RewatchAfterStopSucceeds)
{
    TempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    ASSERT_TRUE(w->watch(f.path_str(), [] {}).has_value());
    w->stop();
    // change_count resets on a fresh watch().
    ASSERT_TRUE(w->watch(f.path_str(), [] {}).has_value());
    EXPECT_TRUE(w->is_watching());
    EXPECT_EQ(w->change_count(), 0U);
    w->stop();
}

// -----------------------------------------------------------------------------
// HotReloader edge / negative — lifecycle paths not covered above
// -----------------------------------------------------------------------------

TEST(HotReload, MountTwiceReturnsAlreadyMounted)
{
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(1, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 7 }; }
    };
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    auto second = hr.mount("p.dll");
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code,
              static_cast<std::uint32_t>(cd::plugin::hot_reload_errors::Code::kAlreadyMounted));
}

TEST(HotReload, MountLoadFailurePropagatesAndLeavesUnmounted)
{
    cd::plugin::HotReloader hr {
        [](std::string_view) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return std::unexpected(
                cd::plugin::loader_errors::make(cd::plugin::loader_errors::Code::kLoadFailed));
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    auto r = hr.mount("bad.dll");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::loader_errors::Code::kLoadFailed));
    EXPECT_FALSE(hr.is_mounted());
    EXPECT_EQ(hr.current(), nullptr);
}

TEST(HotReload, UnmountFiresBeforeUnloadAndResetsState)
{
    std::uint64_t fake_version = 5;
    int unload_calls = 0;
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(1, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unload_calls; });
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    hr.unmount();
    EXPECT_EQ(unload_calls, 1);
    EXPECT_FALSE(hr.is_mounted());
    EXPECT_EQ(hr.current_version(), 0U);
    EXPECT_TRUE(hr.current_path().empty());
}

TEST(HotReload, UnmountWhenNotMountedIsNoOp)
{
    int unload_calls = 0;
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(1, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unload_calls; });
    hr.unmount();  // never mounted
    EXPECT_EQ(unload_calls, 0);
    EXPECT_FALSE(hr.is_mounted());
}

TEST(HotReload, PollAfterUnmountReturnsNotMounted)
{
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(1, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    hr.unmount();
    auto r = hr.poll();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::hot_reload_errors::Code::kNotMounted));
}

TEST(HotReload, RemountAfterUnmountReloadsFresh)
{
    std::uint32_t next_id = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_fake_loaded(++next_id, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    const auto first_id = dynamic_cast<FakePlugin*>(hr.current())->id();
    hr.unmount();
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    auto* second = dynamic_cast<FakePlugin*>(hr.current());
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second->id(), first_id);
}

TEST(HotReload, FailedReloadThenRecoveryReloadsSuccessfully)
{
    // After a failed reload (old kept), a subsequent good build must
    // reload cleanly — verifies the version/state was NOT advanced past
    // the failure.
    std::uint64_t fake_version = 1;
    std::uint32_t next_id = 0;
    bool fail_next = false;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            if (fail_next)
                return std::unexpected(cd::plugin::loader_errors::make(
                    cd::plugin::loader_errors::Code::kFactoryFailed));
            return make_fake_loaded(++next_id, std::string { p });
        },
        [&](std::string_view) { return fake_version; }
    };
    ASSERT_TRUE(hr.mount("p.dll").has_value());

    fake_version = 2;
    fail_next = true;
    EXPECT_FALSE(hr.poll().has_value());  // reload fails, old kept
    EXPECT_EQ(hr.reload_count(), 0U);

    fail_next = false;  // build fixed, version still 2 (still != mounted 1)
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ(hr.reload_count(), 1U);
    EXPECT_EQ(hr.current_version(), 2U);
}

// -----------------------------------------------------------------------------
// WatchedHotReloader — watcher-driven monitor (gap-closing surface)
// Deterministic: interval==0 polling watcher + forced-mtime TempFile.
// -----------------------------------------------------------------------------

namespace
{

// Build a monitor over a real polling watcher (interval==0) with mock load
// + mtime-based version fns so we exercise the full fuse without a DLL.
struct WatchedRig
{
    std::uint32_t next_id { 0 };
    std::string watched_path;

    cd::plugin::WatchedHotReloader make(std::string path)
    {
        watched_path = std::move(path);
        cd::plugin::HotReloader reloader {
            [this](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
                return make_fake_loaded(++next_id, std::string { p });
            },
            [](std::string_view p) { return cd::plugin::default_plugin_version(p); }
        };
        return cd::plugin::WatchedHotReloader {
            cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 }),
            std::move(reloader)
        };
    }
};

}  // namespace

TEST(WatchedHotReloader, MountStartsWatchingAndLoads)
{
    TempFile f;
    WatchedRig rig;
    auto mon = rig.make(f.path_str());
    ASSERT_TRUE(mon.mount(f.path_str()).has_value());
    EXPECT_TRUE(mon.is_mounted());
    EXPECT_TRUE(mon.is_watching());
    EXPECT_NE(mon.current(), nullptr);
    EXPECT_FALSE(mon.pending());
    mon.unmount();
}

TEST(WatchedHotReloader, MountMissingFileRollsBackPluginAndSurfacesError)
{
    // Watcher start fails (file gone) -> the already-mounted plugin must be
    // unmounted again so the monitor is left clean, and the watcher error
    // is surfaced.
    WatchedRig rig;
    auto mon = rig.make("definitely-not-here-zxyq.dll");
    auto r = mon.mount("definitely-not-here-zxyq.dll");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::watcher_errors::Code::kFileNotFound));
    EXPECT_FALSE(mon.is_mounted());
    EXPECT_FALSE(mon.is_watching());
    EXPECT_EQ(mon.current(), nullptr);
}

TEST(WatchedHotReloader, NoChangePollAndReloadReturnsFalse)
{
    TempFile f;
    WatchedRig rig;
    auto mon = rig.make(f.path_str());
    ASSERT_TRUE(mon.mount(f.path_str()).has_value());

    mon.poll_watcher_once();  // no mtime change since mount
    auto r = mon.poll_and_reload();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r);
    EXPECT_EQ(mon.reload_count(), 0U);
    mon.unmount();
}

TEST(WatchedHotReloader, FileChangeTriggersReloadThroughTheFuse)
{
    TempFile f;
    WatchedRig rig;
    auto mon = rig.make(f.path_str());
    ASSERT_TRUE(mon.mount(f.path_str()).has_value());
    auto* first = dynamic_cast<FakePlugin*>(mon.current());
    ASSERT_NE(first, nullptr);
    const auto first_id = first->id();

    f.write("rebuilt");           // disk changes
    mon.poll_watcher_once();      // watcher observes -> dirty flag set
    EXPECT_TRUE(mon.pending());

    auto r = mon.poll_and_reload();  // host pump consumes flag -> reload
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_FALSE(mon.pending());      // flag cleared
    EXPECT_EQ(mon.reload_count(), 1U);
    auto* second = dynamic_cast<FakePlugin*>(mon.current());
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second->id(), first_id);
    mon.unmount();
}

TEST(WatchedHotReloader, PollAndReloadWithoutPendingDoesNotReload)
{
    TempFile f;
    WatchedRig rig;
    auto mon = rig.make(f.path_str());
    ASSERT_TRUE(mon.mount(f.path_str()).has_value());

    f.write("changed-but-not-polled");  // watcher never pumped -> no flag
    auto r = mon.poll_and_reload();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r);  // dirty flag was clear, reload skipped
    EXPECT_EQ(mon.reload_count(), 0U);
    mon.unmount();
}

TEST(WatchedHotReloader, FailedReloadKeepsOldPluginAndSurfacesError)
{
    // Inject a load fn that fails after the first (mount) load. The
    // watcher flags a change; the host pump attempts a reload that fails;
    // the previous plugin must stay mounted (rollback) and the error is
    // surfaced through the fuse.
    TempFile f;
    std::uint32_t next_id = 0;
    bool fail_next = false;
    cd::plugin::HotReloader reloader {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            if (fail_next)
                return std::unexpected(cd::plugin::loader_errors::make(
                    cd::plugin::loader_errors::Code::kInitFailed));
            return make_fake_loaded(++next_id, std::string { p });
        },
        [](std::string_view p) { return cd::plugin::default_plugin_version(p); }
    };
    cd::plugin::WatchedHotReloader mon {
        cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 }),
        std::move(reloader)
    };
    ASSERT_TRUE(mon.mount(f.path_str()).has_value());
    const auto first_id = dynamic_cast<FakePlugin*>(mon.current())->id();

    f.write("broken-build");
    mon.poll_watcher_once();
    fail_next = true;
    auto r = mon.poll_and_reload();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::loader_errors::Code::kInitFailed));
    ASSERT_TRUE(mon.is_mounted());
    EXPECT_EQ(mon.reload_count(), 0U);
    auto* still_first = dynamic_cast<FakePlugin*>(mon.current());
    ASSERT_NE(still_first, nullptr);
    EXPECT_EQ(still_first->id(), first_id);
    mon.unmount();
}

TEST(WatchedHotReloader, UnmountStopsWatchingAndClearsPending)
{
    TempFile f;
    WatchedRig rig;
    auto mon = rig.make(f.path_str());
    ASSERT_TRUE(mon.mount(f.path_str()).has_value());

    f.write("dirty");
    mon.poll_watcher_once();
    EXPECT_TRUE(mon.pending());

    mon.unmount();
    EXPECT_FALSE(mon.is_mounted());
    EXPECT_FALSE(mon.is_watching());
    EXPECT_FALSE(mon.pending());
}

TEST(WatchedHotReloader, BeforeUnloadAfterLoadHooksPassThrough)
{
    TempFile f;
    WatchedRig rig;
    auto mon = rig.make(f.path_str());
    int unload_calls = 0;
    int load_calls = 0;
    mon.set_before_unload([&](cd::plugin::IPlugin&) { ++unload_calls; });
    mon.set_after_load([&](cd::plugin::IPlugin&) { ++load_calls; });

    ASSERT_TRUE(mon.mount(f.path_str()).has_value());
    EXPECT_EQ(load_calls, 1);  // mount -> after_load
    EXPECT_EQ(unload_calls, 0);

    f.write("v2");
    mon.poll_watcher_once();
    ASSERT_TRUE(mon.poll_and_reload().has_value());
    EXPECT_EQ(unload_calls, 1);
    EXPECT_EQ(load_calls, 2);
    mon.unmount();
}

TEST(WatchedHotReloader, FactoryWiresRealLoaderAndWatcher)
{
    // make_watched_hot_reloader over the real Loader. Mounting a real
    // (non-plugin) file fails at the Loader's dlopen/LoadLibrary stage,
    // proving the factory wired the real loader (not a mock) — and that a
    // mount failure leaves the monitor cleanly unmounted.
    TempFile f;  // a real file on disk, but NOT a valid plugin DLL
    cd::plugin::Loader loader;
    auto mon = cd::plugin::make_watched_hot_reloader(loader, std::chrono::milliseconds { 0 });
    ASSERT_NE(mon, nullptr);
    auto r = mon->mount(f.path_str());
    ASSERT_FALSE(r.has_value());  // real loader rejects a non-DLL
    EXPECT_FALSE(mon->is_mounted());
    EXPECT_FALSE(mon->is_watching());
}

}  // namespace
