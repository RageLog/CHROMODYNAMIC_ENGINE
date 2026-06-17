// =============================================================================
// CHROMODYNAMIC — cd::vfs tests (Sprint S2.9)
// =============================================================================
#include <cd/vfs/FilesystemSource.hpp>
#include <cd/vfs/MemorySource.hpp>
#include <cd/vfs/VirtualFileSystem.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <ranges>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{

// --- MemorySource -----------------------------------------------------------
TEST(VfsMemorySource, PutReadExistsRoundTrip)
{
    cd::vfs::MemorySource src;
    src.put_text("hello.txt", "world");
    EXPECT_TRUE(src.exists("hello.txt"));
    EXPECT_FALSE(src.exists("missing"));
    auto r = src.read("hello.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 5u);
    EXPECT_EQ(static_cast<std::uint8_t>((*r)[0]), 'w');
}

TEST(VfsMemorySource, ReadMissingReturnsNotFound)
{
    cd::vfs::MemorySource src;
    auto r = src.read("nope");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::vfs::vfs_errors::Code::kNotFound));
}

TEST(VfsMemorySource, ListByPrefix)
{
    cd::vfs::MemorySource src;
    src.put_text("shaders/a.vert", "");
    src.put_text("shaders/b.frag", "");
    src.put_text("audio/x.wav", "");
    auto shaders = src.list("shaders/");
    std::ranges::sort(shaders);
    ASSERT_EQ(shaders.size(), 2u);
    EXPECT_EQ(shaders[0], "shaders/a.vert");
    EXPECT_EQ(shaders[1], "shaders/b.frag");
}

TEST(VfsMemorySource, EraseRemoves)
{
    cd::vfs::MemorySource src;
    src.put_text("temp.txt", "abc");
    EXPECT_TRUE(src.exists("temp.txt"));
    src.erase("temp.txt");
    EXPECT_FALSE(src.exists("temp.txt"));
}

// --- VirtualFileSystem (overlay) -------------------------------------------
TEST(VfsOverlay, FirstLayerWins)
{
    auto base = std::make_shared<cd::vfs::MemorySource>("base");
    base->put_text("config.txt", "BASE");
    auto over = std::make_shared<cd::vfs::MemorySource>("override");
    over->put_text("config.txt", "OVER");

    cd::vfs::VirtualFileSystem vfs;
    vfs.mount_back(base);
    vfs.mount_front(over);  // higher priority

    auto r = vfs.read("config.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<std::uint8_t>((*r)[0]), 'O');
    EXPECT_EQ(vfs.resolving_layer("config.txt"), "override");
}

TEST(VfsOverlay, MissingInAllLayers)
{
    auto a = std::make_shared<cd::vfs::MemorySource>("a");
    cd::vfs::VirtualFileSystem vfs;
    vfs.mount_back(a);
    auto r = vfs.read("missing");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::vfs::vfs_errors::Code::kNotFound));
    EXPECT_TRUE(vfs.resolving_layer("missing").empty());
}

TEST(VfsOverlay, ListUnion)
{
    auto a = std::make_shared<cd::vfs::MemorySource>("a");
    a->put_text("x", "");
    a->put_text("shared", "from-a");
    auto b = std::make_shared<cd::vfs::MemorySource>("b");
    b->put_text("y", "");
    b->put_text("shared", "from-b");
    cd::vfs::VirtualFileSystem vfs;
    vfs.mount_back(a);
    vfs.mount_back(b);
    auto all = vfs.list("");
    std::ranges::sort(all);
    ASSERT_EQ(all.size(), 3u);  // x, y, shared (de-duped)
    EXPECT_EQ(all[0], "shared");
    EXPECT_EQ(all[1], "x");
    EXPECT_EQ(all[2], "y");
}

TEST(VfsOverlay, FallthroughLowerLayer)
{
    auto base = std::make_shared<cd::vfs::MemorySource>("base");
    base->put_text("base_only.txt", "BASE");
    auto over = std::make_shared<cd::vfs::MemorySource>("override");
    over->put_text("override_only.txt", "OVER");
    cd::vfs::VirtualFileSystem vfs;
    vfs.mount_back(base);
    vfs.mount_front(over);
    EXPECT_EQ(vfs.resolving_layer("base_only.txt"), "base");
    EXPECT_EQ(vfs.resolving_layer("override_only.txt"), "override");
}

// --- FilesystemSource (uses temp dir) --------------------------------------
class VfsFilesystemTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        temp_root_ = std::filesystem::temp_directory_path() /
                     ("cd_vfs_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(temp_root_);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(temp_root_, ec);
    }

    void write_file(const std::string& rel, std::string_view content)
    {
        auto p = temp_root_ / rel;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out { p, std::ios::binary };
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::filesystem::path temp_root_;
};

TEST_F(VfsFilesystemTest, ReadExistingFile)
{
    write_file("foo.txt", "filesystem-content");
    cd::vfs::FilesystemSource src { temp_root_ };
    EXPECT_TRUE(src.exists("foo.txt"));
    auto r = src.read("foo.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 18u);  // "filesystem-content" is 18 chars
}

TEST_F(VfsFilesystemTest, MissingFileNotFound)
{
    cd::vfs::FilesystemSource src { temp_root_ };
    EXPECT_FALSE(src.exists("nope.txt"));
    auto r = src.read("nope.txt");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::vfs::vfs_errors::Code::kNotFound));
}

TEST_F(VfsFilesystemTest, ListRecursively)
{
    write_file("a.txt", "");
    write_file("nested/b.txt", "");
    write_file("nested/deeper/c.txt", "");
    cd::vfs::FilesystemSource src { temp_root_ };
    auto all = src.list("");
    std::ranges::sort(all);
    ASSERT_EQ(all.size(), 3u);
}

}  // namespace
