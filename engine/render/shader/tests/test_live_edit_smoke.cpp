// =============================================================================
// CHROMODYNAMIC — cd::shader::FileWatcher live edit-and-revert smoke test
//
// Validates the "edit-and-revert" path called out in the M1C ADR as
// deferred visual smoke. Headless gtest — no engine launch required.
//
// Scenario (three-phase, single .glsl temp file):
//   Phase 1  Initial write  → watcher fires (#1)
//   Phase 2  Modify content → watcher fires (#2, new content)
//   Phase 3  Revert content → watcher fires (#3, original content)
//   Cleanup  Temp file deleted
//
// Each phase writes the new content then EXPLICITLY bumps the file's mtime
// forward (baseline + 1 s) before calling poll(), so the watcher's mtime
// comparison fires deterministically on NTFS (≈ 100 ns granularity), ext4
// (1 ns), and FAT32 (2 s) alike. No sleep_for / wall-clock wait (CLAUDE.md
// §5 anti-flakiness): forcing last_write_time is exact and instantaneous,
// mirroring the HelloShaderWatch::poll_and_reload frame-loop pattern.
// =============================================================================
#include <cd/shader/FileWatcher.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
namespace fs = std::filesystem;

[[nodiscard]] fs::path tmp_glsl_path()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp =
        static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    return fs::temp_directory_path() /
           ("cd_live_edit_" + std::to_string(stamp) + "_" +
            std::to_string(seq.fetch_add(1)) + ".frag.glsl");
}

void write_glsl(const fs::path& p, std::string_view src)
{
    std::ofstream f(p, std::ios::trunc);
    f << src;
}

/// Read the entire text of a file.
[[nodiscard]] std::string read_glsl(const fs::path& p)
{
    std::ifstream f(p);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

constexpr std::string_view kOriginal =
    "void main() { gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); }";
constexpr std::string_view kModified =
    "void main() { gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0); }";

/// Write `text` into `p`, then EXPLICITLY force the file's mtime to
/// `baseline + 1 s` so the watcher's last_write_time comparison fires
/// deterministically — no wall-clock dependence, no sleep_for. Returns true
/// iff poll() reports the path dirty. (NTFS / ext4 both honour a forced
/// last_write_time; the +1 s step clears even FAT32's 2 s-rounded-down
/// granularity on the *next* whole second, but the explicit set already
/// distinguishes it from `baseline` on the filesystems we test on.)
[[nodiscard]] bool write_force_mtime_poll(
    const fs::path& p,
    std::string_view text,
    cd::shader::FileWatcher& watcher,
    fs::file_time_type baseline)
{
    write_glsl(p, text);
    std::error_code ec;
    const auto bumped = baseline + std::chrono::seconds(1);
    fs::last_write_time(p, bumped, ec);
    if (ec)
        return false;
    return watcher.poll();
}

}  // namespace

// ---------------------------------------------------------------------------
// EditAndRevert — three sequential write events, each fires the watcher
// ---------------------------------------------------------------------------
TEST(LiveEditSmoke, EditAndRevertFiresWatcherThreeTimes)
{
    // ---- Arrange -----------------------------------------------------------
    const auto p = tmp_glsl_path();

    // Create the file with the original shader source.
    write_glsl(p, kOriginal);
    const auto mtime_after_create = fs::last_write_time(p);

    cd::shader::FileWatcher watcher;
    watcher.add(p.string());
    ASSERT_EQ(watcher.watched_count(), 1U);

    // First poll after add() — must be clean (baseline sampled at add time).
    EXPECT_FALSE(watcher.poll()) << "First poll after add() must not report dirty";

    std::atomic<int> fire_count { 0 };
    std::string last_content;

    const auto on_fire = [&]()
    {
        fire_count.fetch_add(1);
        last_content = read_glsl(p);
    };

    // ---- Act: Phase 1 — initial write (overwrite with same name, advancing mtime) ----
    // Re-write the original content so the mtime advances past the baseline
    // recorded by add(). The watcher should fire once.
    {
        const bool fired = write_force_mtime_poll(p, kOriginal, watcher, mtime_after_create);
        if (fired)
        {
            ASSERT_EQ(watcher.dirty().size(), 1U);
            on_fire();
        }
        EXPECT_TRUE(fired) << "Phase 1: watcher did not fire after initial write (mtime advance)";
    }

    // ---- Act: Phase 2 — modify the content --------------------------------
    {
        const auto baseline2 = fs::last_write_time(p);
        const bool fired = write_force_mtime_poll(p, kModified, watcher, baseline2);
        if (fired)
        {
            ASSERT_EQ(watcher.dirty().size(), 1U);
            on_fire();
        }
        EXPECT_TRUE(fired) << "Phase 2: watcher did not fire after content modification";
    }

    // ---- Act: Phase 3 — revert the content --------------------------------
    {
        const auto baseline3 = fs::last_write_time(p);
        const bool fired = write_force_mtime_poll(p, kOriginal, watcher, baseline3);
        if (fired)
        {
            ASSERT_EQ(watcher.dirty().size(), 1U);
            on_fire();
        }
        EXPECT_TRUE(fired) << "Phase 3: watcher did not fire after content revert";
    }

    // ---- Assert ------------------------------------------------------------
    EXPECT_GE(fire_count.load(), 3)
        << "Expected at least 3 watcher fires (initial write, modify, revert); got "
        << fire_count.load();

    // The final captured content must be the reverted (original) source.
    EXPECT_EQ(last_content, kOriginal)
        << "After revert, file content should match the original shader source";

    // ---- Cleanup -----------------------------------------------------------
    std::error_code ec;
    fs::remove(p, ec);
    EXPECT_FALSE(ec) << "Failed to remove temp file: " << ec.message();
}

// ---------------------------------------------------------------------------
// QuickTwoPhase — fire count ≥ 2 in a compact write → modify flow
// (sub-test: no-revert variant so the revert sub-path is independently tested)
// ---------------------------------------------------------------------------
TEST(LiveEditSmoke, WriteAndModifyFiresTwice)
{
    const auto p = tmp_glsl_path();
    write_glsl(p, kOriginal);
    const auto mtime0 = fs::last_write_time(p);

    cd::shader::FileWatcher watcher;
    watcher.add(p.string());
    EXPECT_FALSE(watcher.poll());

    int count = 0;

    // Phase 1: overwrite with same content (mtime advances).
    {
        const bool fired = write_force_mtime_poll(p, kOriginal, watcher, mtime0);
        if (fired) ++count;
        EXPECT_TRUE(fired) << "QuickTwoPhase Phase 1 did not fire";
    }

    // Phase 2: modify.
    {
        const auto baseline = fs::last_write_time(p);
        const bool fired = write_force_mtime_poll(p, kModified, watcher, baseline);
        if (fired) ++count;
        EXPECT_TRUE(fired) << "QuickTwoPhase Phase 2 did not fire";
    }

    EXPECT_GE(count, 2);

    std::error_code ec;
    fs::remove(p, ec);
}
