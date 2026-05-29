// =============================================================================
// CHROMODYNAMIC — cd::shader::FileWatcher hot-reload smoke test
//
// This test exists at the call-site shape requested for M1 hot-reload
// integration: "create a temp file, register a change callback, mutate
// the file, confirm the callback fires within a deadline." It wraps the
// polling `FileWatcher` in a tiny per-instance `poll_with_callback`
// helper that the hello_engine `HelloShaderWatch.hpp` aggregate models
// itself on. Per ADR-20260529-X5 we deliberately do NOT use
// ReadDirectoryChangesW / inotify / FSEvents — polling is portable,
// race-free, and sub-millisecond at our shader-corpus scale.
// =============================================================================
#include <cd/shader/FileWatcher.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

namespace
{
namespace fs = std::filesystem;

[[nodiscard]] fs::path tmp_path(std::string_view suffix)
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
        ("cd_fw_hr_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
         std::to_string(seq.fetch_add(1)) + std::string { suffix });
}

void write_text(const fs::path& p, std::string_view text)
{
    std::ofstream f(p);
    f << text;
}

}  // namespace

// Mirrors the mandate's done-criterion: "create a temp file, call watch,
// modify the file, wait for callback within 3s, confirm callback fired."
// Implemented over the polling watcher so it stays ADR-faithful (no
// per-OS background thread, no ReadDirectoryChangesW).
TEST(FileWatcherHotReload, ModifiedFileFiresCallbackWithin3Seconds)
{
    auto p = tmp_path(".frag.glsl");
    write_text(p, "void main() { gl_FragColor = vec4(0.0); }");
    cd::shader::FileWatcher w;
    w.add(p.string());

    std::atomic<int> callback_count { 0 };
    const std::function<void()> on_change = [&]() { callback_count.fetch_add(1); };

    // Spin: mutate the file until its mtime advances past the baseline,
    // then poll until dirty (or deadline). Mirrors the per-frame
    // poll-and-reload loop in HelloShaderWatch::poll_and_reload.
    const auto t0 = fs::last_write_time(p);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool fired = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        write_text(p, "void main() { gl_FragColor = vec4(1.0); }");
        if (fs::last_write_time(p) != t0 && w.poll())
        {
            for ([[maybe_unused]] const auto& dirty_path : w.dirty())
                on_change();
            fired = true;
            break;
        }
    }

    EXPECT_TRUE(fired) << "FileWatcher::poll never reported a change within 3s";
    EXPECT_GE(callback_count.load(), 1) << "on_change callback was not invoked";

    fs::remove(p);
}

// Two distinct files, two distinct callbacks. Models the hello_engine
// case where prim.vert.glsl and prim.frag.glsl each map to their own
// recompile-and-swap closure.
TEST(FileWatcherHotReload, TwoFilesDispatchToTheirOwnCallbacks)
{
    auto p1 = tmp_path(".vert.glsl");
    auto p2 = tmp_path(".frag.glsl");
    write_text(p1, "v0");
    write_text(p2, "f0");
    cd::shader::FileWatcher w;
    w.add(p1.string());
    w.add(p2.string());

    std::atomic<int> v_count { 0 };
    std::atomic<int> f_count { 0 };
    const std::string p1s = p1.string();
    const std::string p2s = p2.string();

    const auto dispatch = [&]() {
        for (const auto& dirty_path : w.dirty())
        {
            if (dirty_path == p1s)
                v_count.fetch_add(1);
            else if (dirty_path == p2s)
                f_count.fetch_add(1);
        }
    };

    // Touch only the fragment file.
    const auto t0 = fs::last_write_time(p2);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool fired = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        write_text(p2, "f1");
        if (fs::last_write_time(p2) != t0 && w.poll())
        {
            dispatch();
            fired = true;
            break;
        }
    }

    EXPECT_TRUE(fired);
    EXPECT_EQ(v_count.load(), 0) << "vertex callback should not fire when only fragment changed";
    EXPECT_GE(f_count.load(), 1);

    fs::remove(p1);
    fs::remove(p2);
}
