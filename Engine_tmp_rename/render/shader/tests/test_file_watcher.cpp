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
