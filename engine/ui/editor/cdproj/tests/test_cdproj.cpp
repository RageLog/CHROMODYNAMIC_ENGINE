// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/cdproj/tests/test_cdproj.cpp
//
// phase547 — unit tests for cd::editor::cdproj (CdprojFile).
//
// All tests are headless / filesystem-only. We verify:
//   WriteReadRoundTrip          — all fields survive a write + read cycle.
//   MissingFileReturnsNullopt   — read_cdproj() on absent path => nullopt.
//   MalformedJsonReturnsNullopt — truncated / garbled JSON => nullopt.
//   SchemaVersionMismatchHandled— schema_version != 1 => nullopt.
//   RecentFilesFifoEvicts       — push 11 paths, expect exactly 10 retained.
//   PushRecentFileMoveToFront   — duplicate path is moved to front (dedupe).
//   PushRecentFileOrderPreserved— newer pushes appear before older ones.
//   WindowDefaultValues         — default-constructed CdprojWindow values.
//   WriteCreatesParentDir       — write_cdproj creates missing parent dirs.
//   EmptyDockLayoutRoundTrips   — empty dock_layout string survives round-trip.
// =============================================================================

#include <cd/editor/cdproj/CdprojFile.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace cdproj = cd::editor::cdproj;
namespace fs     = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture — provides a per-test temporary directory so tests do not
// interfere with each other and the filesystem is cleaned up on exit.
// ---------------------------------------------------------------------------
class CdprojTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        tmp_dir_ = fs::temp_directory_path() / "cd_cdproj_test_XXXXXX";
        // Unique suffix using test info to keep runs deterministic in CI.
        const auto* ti = testing::UnitTest::GetInstance()->current_test_info();
        tmp_dir_ = fs::temp_directory_path() /
                   ("cd_cdproj_" + std::string(ti->name()));
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(tmp_dir_, ec);
    }

    [[nodiscard]] fs::path cdproj_path(const char* name = "test.cdproj") const
    {
        return tmp_dir_ / name;
    }

    fs::path tmp_dir_;
};

// ---------------------------------------------------------------------------
// TEST: WriteReadRoundTrip
//
// Write a fully-populated CdprojData and read it back. Every field must
// survive the JSON serialization + deserialization cycle unchanged.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, WriteReadRoundTrip)
{
    cdproj::CdprojData data;
    data.schema_version         = 1;
    data.last_opened_scene_path = "assets/scenes/level_01.cdscene";
    data.dock_layout            = "DOCKSTATE_OPAQUE_V1";
    data.window.x               = 100;
    data.window.y               = 50;
    data.window.w               = 1920;
    data.window.h               = 1080;
    data.window.maximized       = true;
    data.recent_files           = { "a.cdscene", "b.cdscene", "c.cdscene" };

    const auto path = cdproj_path();
    ASSERT_TRUE(cdproj::write_cdproj(data, path));

    const auto loaded = cdproj::read_cdproj(path);
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->schema_version,         1);
    EXPECT_EQ(loaded->last_opened_scene_path, "assets/scenes/level_01.cdscene");
    EXPECT_EQ(loaded->dock_layout,            "DOCKSTATE_OPAQUE_V1");
    EXPECT_EQ(loaded->window.x,               100);
    EXPECT_EQ(loaded->window.y,               50);
    EXPECT_EQ(loaded->window.w,               1920);
    EXPECT_EQ(loaded->window.h,               1080);
    EXPECT_TRUE(loaded->window.maximized);
    ASSERT_EQ(loaded->recent_files.size(),    static_cast<std::size_t>(3));
    EXPECT_EQ(loaded->recent_files[0],        "a.cdscene");
    EXPECT_EQ(loaded->recent_files[1],        "b.cdscene");
    EXPECT_EQ(loaded->recent_files[2],        "c.cdscene");
}

// ---------------------------------------------------------------------------
// TEST: MissingFileReturnsNullopt
//
// read_cdproj() on a path that does not exist must return std::nullopt.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, MissingFileReturnsNullopt)
{
    const auto path   = cdproj_path("does_not_exist.cdproj");
    const auto result = cdproj::read_cdproj(path);
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// TEST: MalformedJsonReturnsNullopt
//
// read_cdproj() on a file that contains invalid JSON must return nullopt.
// We test three representative failure modes:
//   1. Completely garbled content.
//   2. Truncated JSON (no closing brace).
//   3. Valid JSON but missing schema_version key.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, MalformedJsonReturnsNullopt)
{
    // Case 1: garbage bytes.
    {
        const auto path = cdproj_path("garbage.cdproj");
        std::ofstream f(path);
        f << "NOT JSON AT ALL @@##%%";
        f.close();
        EXPECT_FALSE(cdproj::read_cdproj(path).has_value()) << "garbage bytes";
    }

    // Case 2: truncated (missing closing brace).
    {
        const auto path = cdproj_path("truncated.cdproj");
        std::ofstream f(path);
        f << R"({ "schema_version": 1, "last_opened_scene_path": "x.cd)";
        f.close();
        EXPECT_FALSE(cdproj::read_cdproj(path).has_value()) << "truncated JSON";
    }

    // Case 3: valid JSON object but schema_version key absent.
    {
        const auto path = cdproj_path("no_schema_version.cdproj");
        std::ofstream f(path);
        f << R"({ "last_opened_scene_path": "foo.cdscene" })";
        f.close();
        EXPECT_FALSE(cdproj::read_cdproj(path).has_value()) << "missing schema_version";
    }
}

// ---------------------------------------------------------------------------
// TEST: SchemaVersionMismatchHandled
//
// read_cdproj() must return nullopt for any schema_version other than 1.
// This prevents a newer-format file from being silently misinterpreted.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, SchemaVersionMismatchHandled)
{
    // schema_version == 0 (invalid)
    {
        const auto path = cdproj_path("v0.cdproj");
        std::ofstream f(path);
        f << R"({ "schema_version": 0 })";
        f.close();
        EXPECT_FALSE(cdproj::read_cdproj(path).has_value()) << "schema_version=0";
    }

    // schema_version == 2 (future — not yet supported)
    {
        const auto path = cdproj_path("v2.cdproj");
        std::ofstream f(path);
        f << R"({ "schema_version": 2, "last_opened_scene_path": "" })";
        f.close();
        EXPECT_FALSE(cdproj::read_cdproj(path).has_value()) << "schema_version=2";
    }

    // schema_version == 1 must still succeed.
    {
        const auto path = cdproj_path("v1.cdproj");
        std::ofstream f(path);
        f << R"({ "schema_version": 1 })";
        f.close();
        EXPECT_TRUE(cdproj::read_cdproj(path).has_value()) << "schema_version=1";
    }
}

// ---------------------------------------------------------------------------
// TEST: RecentFilesFifoEvicts
//
// Push 11 distinct paths. The list must cap at 10 (FIFO tail eviction).
// The earliest push (index 10) must be absent; the latest push (index 0)
// must appear at the front.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, RecentFilesFifoEvicts)
{
    cdproj::CdprojData data;

    // Push 11 paths: path_00.cdscene … path_10.cdscene
    for (int idx = 0; idx < 11; ++idx)
    {
        cdproj::push_recent_file(data, "path_" + std::to_string(idx) + ".cdscene");
    }

    ASSERT_EQ(data.recent_files.size(), static_cast<std::size_t>(10));

    // Most-recently pushed (path_10) must be at the front.
    EXPECT_EQ(data.recent_files.front(), "path_10.cdscene");

    // The very first push (path_00) must have been evicted.
    const auto it = std::find(data.recent_files.begin(),
                              data.recent_files.end(),
                              "path_00.cdscene");
    EXPECT_EQ(it, data.recent_files.end()) << "path_00.cdscene should have been evicted";
}

// ---------------------------------------------------------------------------
// TEST: PushRecentFileMoveToFront
//
// Pushing a path that already exists in the list must move it to position 0
// without creating a duplicate and without changing the list size.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, PushRecentFileMoveToFront)
{
    cdproj::CdprojData data;
    data.recent_files = { "alpha.cdscene", "beta.cdscene", "gamma.cdscene" };

    cdproj::push_recent_file(data, "beta.cdscene");

    ASSERT_EQ(data.recent_files.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(data.recent_files[0], "beta.cdscene");
    EXPECT_EQ(data.recent_files[1], "alpha.cdscene");
    EXPECT_EQ(data.recent_files[2], "gamma.cdscene");
}

// ---------------------------------------------------------------------------
// TEST: PushRecentFileOrderPreserved
//
// A brand-new path is inserted at the front; the existing entries shift back.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, PushRecentFileOrderPreserved)
{
    cdproj::CdprojData data;
    data.recent_files = { "old_1.cdscene", "old_2.cdscene" };

    cdproj::push_recent_file(data, "new.cdscene");

    ASSERT_EQ(data.recent_files.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(data.recent_files[0], "new.cdscene");
    EXPECT_EQ(data.recent_files[1], "old_1.cdscene");
    EXPECT_EQ(data.recent_files[2], "old_2.cdscene");
}

// ---------------------------------------------------------------------------
// TEST: WindowDefaultValues
//
// Default-constructed CdprojWindow must have the expected defaults.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, WindowDefaultValues)
{
    const cdproj::CdprojWindow w {};
    EXPECT_EQ(w.x,          0);
    EXPECT_EQ(w.y,          0);
    EXPECT_EQ(w.w,          1280);
    EXPECT_EQ(w.h,          720);
    EXPECT_FALSE(w.maximized);
}

// ---------------------------------------------------------------------------
// TEST: WriteCreatesParentDir
//
// write_cdproj() must succeed even when the parent directory does not yet
// exist (it must create it, matching cd::game_save semantics).
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, WriteCreatesParentDir)
{
    const auto nested = tmp_dir_ / "subdir_a" / "subdir_b" / "proj.cdproj";
    ASSERT_FALSE(fs::exists(nested.parent_path()));

    cdproj::CdprojData data;
    data.last_opened_scene_path = "test.cdscene";
    EXPECT_TRUE(cdproj::write_cdproj(data, nested));
    EXPECT_TRUE(fs::exists(nested));
}

// ---------------------------------------------------------------------------
// TEST: EmptyDockLayoutRoundTrips
//
// An empty dock_layout string must survive write + read without corruption.
// ---------------------------------------------------------------------------
TEST_F(CdprojTest, EmptyDockLayoutRoundTrips)
{
    cdproj::CdprojData data;
    data.dock_layout = "";

    const auto path = cdproj_path("empty_dock.cdproj");
    ASSERT_TRUE(cdproj::write_cdproj(data, path));

    const auto loaded = cdproj::read_cdproj(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->dock_layout, "");
}
