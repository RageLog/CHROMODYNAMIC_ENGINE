// =============================================================================
// CHROMODYNAMIC — cd::shader::FileWatcher tests
// =============================================================================
#include <cd/shader/FileWatcher.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

namespace
{
namespace fs = std::filesystem;

[[nodiscard]] fs::path tmp_path(std::string_view suffix)
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("cd_fw_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                                        std::to_string(seq.fetch_add(1)) + std::string { suffix });
}

void write_text(const fs::path& p, std::string_view text)
{
    std::ofstream f(p);
    f << text;
}

// Write `text` then FORCE the file's mtime forward by 1 s past `baseline`.
// Deterministic + instantaneous — no sleep_for / wall-clock dependence
// (CLAUDE.md §5 anti-flakiness). NTFS / ext4 both honour a forced
// last_write_time, so poll()'s mtime comparison fires every time.
void write_bump_mtime(const fs::path& p, std::string_view text, fs::file_time_type baseline)
{
    write_text(p, text);
    std::error_code ec;
    fs::last_write_time(p, baseline + std::chrono::seconds(1), ec);
}

}  // namespace

TEST(FileWatcher, FreshlyAddedFileReportsNoChangeOnFirstPoll)
{
    auto p = tmp_path(".vert");
    write_text(p, "v0");
    cd::shader::FileWatcher w;
    w.add(p.string());

    EXPECT_FALSE(w.poll());
    EXPECT_TRUE(w.dirty().empty());

    fs::remove(p);
}

TEST(FileWatcher, ModifyingFileReportsDirty)
{
    auto p = tmp_path(".vert");
    write_text(p, "v0");
    cd::shader::FileWatcher w;
    w.add(p.string());

    // mtime granularity varies (Win NTFS ≈ 100 ns, ext4 1 ns, FAT 2 s).
    // Spin until mtime actually advances so the test is deterministic.
    const auto t0 = fs::last_write_time(p);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        write_text(p, "v1 contents differ");
        if (fs::last_write_time(p) != t0)
        {
            break;
        }
    }

    EXPECT_TRUE(w.poll());
    ASSERT_EQ(w.dirty().size(), 1U);
    EXPECT_EQ(w.dirty()[0], p.string());

    // Second poll without further change → clean.
    EXPECT_FALSE(w.poll());

    fs::remove(p);
}

TEST(FileWatcher, AppearingFileReportsDirty)
{
    auto p = tmp_path(".vert");
    cd::shader::FileWatcher w;
    w.add(p.string());  // path does not exist yet — tracked as missing.
    EXPECT_FALSE(w.poll());

    write_text(p, "now I exist");
    EXPECT_TRUE(w.poll());
    EXPECT_EQ(w.dirty().size(), 1U);

    fs::remove(p);
}

TEST(FileWatcher, DisappearingFileReportsDirty)
{
    auto p = tmp_path(".vert");
    write_text(p, "exists");
    cd::shader::FileWatcher w;
    w.add(p.string());
    EXPECT_FALSE(w.poll());

    fs::remove(p);
    EXPECT_TRUE(w.poll());
    EXPECT_EQ(w.dirty().size(), 1U);
}

TEST(FileWatcher, RemoveStopsTracking)
{
    auto p = tmp_path(".vert");
    write_text(p, "tracked");
    cd::shader::FileWatcher w;
    w.add(p.string());
    EXPECT_EQ(w.watched_count(), 1U);
    w.remove(p.string());
    EXPECT_EQ(w.watched_count(), 0U);

    // Modify after remove — should not appear in dirty list.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    write_text(p, "modified after remove");
    EXPECT_FALSE(w.poll());

    fs::remove(p);
}

// ---- Deterministic forced-mtime edge tests (no sleep_for) ------------------

// remove() of an untracked path is a documented no-op and must not throw or
// alter the watch set.
TEST(FileWatcher, RemoveUntrackedIsNoOp)
{
    cd::shader::FileWatcher w;
    w.remove("never_added_path.vert");
    EXPECT_EQ(w.watched_count(), 0U);
}

// Two tracked files both changed before a single poll() must BOTH appear in
// the dirty list (the loop reports every changed path, not just the first).
TEST(FileWatcher, MultipleFilesDirtyInOnePoll)
{
    auto p1 = tmp_path(".vert");
    auto p2 = tmp_path(".frag");
    write_text(p1, "a0");
    write_text(p2, "b0");
    cd::shader::FileWatcher w;
    w.add(p1.string());
    w.add(p2.string());
    EXPECT_FALSE(w.poll());

    write_bump_mtime(p1, "a1", fs::last_write_time(p1));
    write_bump_mtime(p2, "b1", fs::last_write_time(p2));

    EXPECT_TRUE(w.poll());
    EXPECT_EQ(w.dirty().size(), 2U);

    fs::remove(p1);
    fs::remove(p2);
}

// dirty() reflects only the MOST RECENT poll(): a second poll with no change
// clears the previously-reported path.
TEST(FileWatcher, DirtyListResetsBetweenPolls)
{
    auto p = tmp_path(".vert");
    write_text(p, "v0");
    cd::shader::FileWatcher w;
    w.add(p.string());
    EXPECT_FALSE(w.poll());

    write_bump_mtime(p, "v1", fs::last_write_time(p));
    EXPECT_TRUE(w.poll());
    EXPECT_EQ(w.dirty().size(), 1U);

    // No change → dirty() must be empty again.
    EXPECT_FALSE(w.poll());
    EXPECT_TRUE(w.dirty().empty());

    fs::remove(p);
}

// Re-add() of a tracked path re-samples its baseline: a change made BEFORE the
// re-add must not be reported by the next poll() (idempotent baseline reset).
TEST(FileWatcher, ReAddResetsBaseline)
{
    auto p = tmp_path(".vert");
    write_text(p, "v0");
    cd::shader::FileWatcher w;
    w.add(p.string());

    // Mutate, then re-add (re-sampling the post-mutation mtime as baseline).
    write_bump_mtime(p, "v1", fs::last_write_time(p));
    w.add(p.string());
    EXPECT_EQ(w.watched_count(), 1U);

    // The mutation predates the re-add baseline → no change to report.
    EXPECT_FALSE(w.poll());

    fs::remove(p);
}

// Delete-then-recreate is two distinct change events: disappearance fires
// once, reappearance fires again. Models a shader file saved by an editor
// that writes via delete + rename.
TEST(FileWatcher, DeleteThenRecreateFiresTwice)
{
    auto p = tmp_path(".frag");
    write_text(p, "exists");
    cd::shader::FileWatcher w;
    w.add(p.string());
    EXPECT_FALSE(w.poll());

    fs::remove(p);
    EXPECT_TRUE(w.poll());           // disappearance
    ASSERT_EQ(w.dirty().size(), 1U);

    write_text(p, "back again");
    EXPECT_TRUE(w.poll());           // reappearance
    ASSERT_EQ(w.dirty().size(), 1U);

    fs::remove(p);
}
