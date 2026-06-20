// =============================================================================
// CHROMODYNAMIC — cd::vfs tests (Sprint S2.9)
// =============================================================================
#include <cd/vfs/FilesystemSource.hpp>
#include <cd/vfs/MemorySource.hpp>
#include <cd/vfs/PathUtil.hpp>
#include <cd/vfs/VirtualFileSystem.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <ranges>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <cstdint>

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

// =============================================================================
// PathUtil: normalize_path
// =============================================================================

TEST(VfsPathUtil, DotSegmentEliminated)
{
    // Arrange + Act
    const std::string result = cd::vfs::normalize_path("a/./b");
    // Assert
    EXPECT_EQ(result, "a/b");
}

TEST(VfsPathUtil, DotDotSegmentResolved)
{
    EXPECT_EQ(cd::vfs::normalize_path("shaders/../textures/a.png"), "textures/a.png");
}

TEST(VfsPathUtil, DotDotClampedAtRoot)
{
    // "../escape" should not escape above the VFS root
    EXPECT_EQ(cd::vfs::normalize_path("../escape"), "escape");
}

TEST(VfsPathUtil, TrailingSeparatorStripped)
{
    EXPECT_EQ(cd::vfs::normalize_path("trailing/"), "trailing");
}

TEST(VfsPathUtil, LeadingSeparatorStripped)
{
    EXPECT_EQ(cd::vfs::normalize_path("/leading/slash"), "leading/slash");
}

TEST(VfsPathUtil, DuplicateSeparatorsCollapsed)
{
    EXPECT_EQ(cd::vfs::normalize_path("a//b///c"), "a/b/c");
}

TEST(VfsPathUtil, BackslashConvertedToForwardSlash)
{
    EXPECT_EQ(cd::vfs::normalize_path("a\\b\\c"), "a/b/c");
}

TEST(VfsPathUtil, EmptyPathReturnsEmpty)
{
    EXPECT_EQ(cd::vfs::normalize_path(""), "");
}

// =============================================================================
// MemorySource: additional edge cases
// =============================================================================

TEST(VfsMemorySource, OverwriteViaSecondPut)
{
    // Arrange
    cd::vfs::MemorySource src;
    src.put_text("file.txt", "v1");
    // Act — overwrite
    src.put_text("file.txt", "version2");
    // Assert
    auto r = src.read("file.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 8u);  // "version2" = 8 chars
}

TEST(VfsMemorySource, PutRawBytes)
{
    // Arrange
    cd::vfs::MemorySource src;
    std::vector<std::byte> raw { std::byte { 0xDE }, std::byte { 0xAD }, std::byte { 0xBE }, std::byte { 0xEF } };
    // Act
    src.put("blob.bin", raw);
    // Assert
    auto r = src.read("blob.bin");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->size(), 4u);
    EXPECT_EQ(static_cast<std::uint8_t>((*r)[0]), 0xDEu);
    EXPECT_EQ(static_cast<std::uint8_t>((*r)[3]), 0xEFu);
}

TEST(VfsMemorySource, SizeReflectsFileCount)
{
    cd::vfs::MemorySource src;
    EXPECT_EQ(src.size(), 0u);
    src.put_text("a", "");
    src.put_text("b", "");
    EXPECT_EQ(src.size(), 2u);
    src.erase("a");
    EXPECT_EQ(src.size(), 1u);
}

TEST(VfsMemorySource, PathNormalisedOnPutAndRead)
{
    // Arrange: put with un-clean path, read with different-but-equivalent path
    cd::vfs::MemorySource src;
    src.put_text("a/../textures/x.png", "data");
    // Act
    const bool found = src.exists("textures/x.png");
    // Assert
    EXPECT_TRUE(found);
    auto r = src.read("textures/x.png");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 4u);
}

// =============================================================================
// VirtualFileSystem: additional overlay / unmount / edge cases
// =============================================================================

TEST(VfsOverlay, EmptyVfsReadReturnsNotFound)
{
    // Arrange
    cd::vfs::VirtualFileSystem vfs;
    // Act
    auto r = vfs.read("anything");
    // Assert
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::vfs::vfs_errors::Code::kNotFound));
}

TEST(VfsOverlay, EmptyVfsExistsReturnsFalse)
{
    cd::vfs::VirtualFileSystem vfs;
    EXPECT_FALSE(vfs.exists("anything"));
}

TEST(VfsOverlay, ThreeLayerPriorityOrder)
{
    // Arrange: three layers — low, mid, high. Same path exists in all three.
    auto low = std::make_shared<cd::vfs::MemorySource>("low");
    low->put_text("item", "LOW");
    auto mid = std::make_shared<cd::vfs::MemorySource>("mid");
    mid->put_text("item", "MID");
    auto high = std::make_shared<cd::vfs::MemorySource>("high");
    high->put_text("item", "HIGH");

    cd::vfs::VirtualFileSystem vfs;
    vfs.mount_back(low);
    vfs.mount_back(mid);
    vfs.mount_front(high);  // highest priority

    // Act
    auto r = vfs.read("item");
    // Assert — high must win
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<char>((*r)[0]), 'H');
    EXPECT_EQ(vfs.resolving_layer("item"), "high");
    EXPECT_EQ(vfs.layer_count(), 3u);
}

TEST(VfsOverlay, UnmountRemovesLayer)
{
    // Arrange
    auto base = std::make_shared<cd::vfs::MemorySource>("base");
    base->put_text("shared.txt", "BASE");
    auto over = std::make_shared<cd::vfs::MemorySource>("override");
    over->put_text("shared.txt", "OVER");

    cd::vfs::VirtualFileSystem vfs;
    vfs.mount_back(base);
    vfs.mount_front(over);
    ASSERT_EQ(vfs.layer_count(), 2u);
    EXPECT_EQ(vfs.resolving_layer("shared.txt"), "override");

    // Act — unmount the higher-priority layer
    const bool removed = vfs.unmount("override");

    // Assert — base now serves the path
    EXPECT_TRUE(removed);
    EXPECT_EQ(vfs.layer_count(), 1u);
    EXPECT_EQ(vfs.resolving_layer("shared.txt"), "base");
    auto r = vfs.read("shared.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<char>((*r)[0]), 'B');
}

TEST(VfsOverlay, UnmountNonExistentReturnsFalse)
{
    cd::vfs::VirtualFileSystem vfs;
    auto src = std::make_shared<cd::vfs::MemorySource>("real");
    vfs.mount_back(src);
    EXPECT_FALSE(vfs.unmount("ghost"));
    EXPECT_EQ(vfs.layer_count(), 1u);
}

TEST(VfsOverlay, PathNormalisedOnRead)
{
    // Arrange: file stored under clean path
    auto mem = std::make_shared<cd::vfs::MemorySource>("m");
    mem->put_text("textures/diffuse.png", "pixels");

    cd::vfs::VirtualFileSystem vfs;
    vfs.mount_back(mem);

    // Act: query with un-clean path containing ".." and "//"
    auto r = vfs.read("textures/sub/..//diffuse.png");

    // Assert: should resolve to "textures/diffuse.png"
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 6u);  // "pixels"
}

TEST_F(VfsFilesystemTest, PathNormalisedOnFsRead)
{
    // Arrange
    write_file("sub/file.txt", "hello");
    cd::vfs::FilesystemSource src { temp_root_ };

    // Act: read via path with ".." that resolves to same location
    auto r = src.read("sub/deeper/../file.txt");

    // Assert
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 5u);
}

TEST_F(VfsFilesystemTest, ListWithPrefix)
{
    // Arrange
    write_file("shaders/vertex.glsl", "");
    write_file("shaders/fragment.glsl", "");
    write_file("textures/diffuse.png", "");
    cd::vfs::FilesystemSource src { temp_root_ };

    // Act
    auto shaders = src.list("shaders/");
    std::ranges::sort(shaders);

    // Assert — only shaders, not textures
    ASSERT_EQ(shaders.size(), 2u);
    // paths are relative, forward-slash separated
    EXPECT_NE(std::ranges::find(shaders, "shaders/fragment.glsl"), shaders.end());
    EXPECT_NE(std::ranges::find(shaders, "shaders/vertex.glsl"), shaders.end());
}

}  // namespace
