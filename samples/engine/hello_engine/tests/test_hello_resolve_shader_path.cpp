// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/tests/test_hello_resolve_shader_path.cpp
//
// X5 hardening step 2 (ADR-20260608 addendum A.3(a)) -- unit net for
// cd_sample::resolve_shader_path, the SOURCE-TREE shader resolution that
// keeps the dev-build compile path and the FileWatcher watch path on the
// SAME file the developer edits (fix for the relink-only stale-exe-copy
// trap: the POST_BUILD copy_if_different only re-fires on a relink, so a
// shader-ONLY edit used to hot-reload a stale exe-side blob).
//
// Contract pinned here:
//   (1) source file EXISTS under source_dir       -> "<source_dir>/<basename>"
//   (2) source file MISSING under source_dir      -> relative, byte-identical
//   (3) source_dir empty (shipped-binary default) -> relative, byte-identical
//   (4) source_dir itself absent on disk          -> relative, byte-identical
//   (5) relative WITHOUT a "shaders/" prefix      -> basename mapping intact
//
// `source_dir` is an injectable parameter (HELLO_ENGINE_SHADER_SOURCE_DIR is
// only its default, and that macro is a PRIVATE define of the sample target),
// so none of these tests depend on compile definitions -- no ifdef-gated
// assertions. Anti-flakiness (CLAUDE.md 5): no sleeps, no GPU, per-test
// unique temp dirs cleaned by an RAII guard, pure-CPU deterministic.
// =============================================================================

// HelloMaterials.hpp expects the sample's on-disk-shaders toggle to be
// defined; force the embedded-fallback value like the sibling
// test_hello_materials_prim_bindless.cpp (the helper itself is NOT gated
// by this macro).
#define HELLO_ENGINE_USE_ON_DISK_SHADERS 0

#include "../HelloMaterials.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace
{

/// RAII per-test temp dir: unique subdir under the system temp path,
/// force-created fresh in the ctor, best-effort removed in the dtor
/// (error_code overload -- a dtor must not throw).
class TempShaderDir
{
public:
    explicit TempShaderDir(std::string_view tag)
        : dir_ { std::filesystem::temp_directory_path() /
                 (std::string { "cd_resolve_shader_path_" } += tag) }
    {
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }
    ~TempShaderDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);  // best-effort cleanup
    }
    TempShaderDir(const TempShaderDir&)            = delete;
    TempShaderDir& operator=(const TempShaderDir&) = delete;
    TempShaderDir(TempShaderDir&&)                 = delete;
    TempShaderDir& operator=(TempShaderDir&&)      = delete;

    [[nodiscard]] std::string str() const { return dir_.string(); }
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return dir_;
    }

private:
    std::filesystem::path dir_;
};

void write_stub_file(const std::filesystem::path& p)
{
    std::ofstream f { p };
    f << "// stub shader\n";
}

// (1) Source file exists -> the source-tree path is returned, built as
//     "<source_dir>/<basename-of-relative>".
TEST(ResolveShaderPath, SourceFileExistsReturnsSourceTreePath)
{
    // Arrange
    const TempShaderDir dir { "exists" };
    write_stub_file(dir.path() / "prim.vert.glsl");
    ASSERT_TRUE(std::filesystem::exists(dir.path() / "prim.vert.glsl"));

    // Act
    const std::string resolved =
        cd_sample::resolve_shader_path("shaders/prim.vert.glsl", dir.str());

    // Assert
    EXPECT_EQ(resolved, dir.str() + "/prim.vert.glsl");
}

// (2) Source dir exists but the file does not -> byte-identical relative
//     fallback (the shipped-binary / partial-source-tree shape).
TEST(ResolveShaderPath, SourceFileMissingReturnsRelativeByteIdentical)
{
    // Arrange
    const TempShaderDir dir { "missing" };

    // Act
    const std::string resolved =
        cd_sample::resolve_shader_path("shaders/prim.frag.glsl", dir.str());

    // Assert
    EXPECT_EQ(resolved, std::string { "shaders/prim.frag.glsl" });
}

// (3) Empty source_dir (the default when HELLO_ENGINE_SHADER_SOURCE_DIR
//     is not defined, i.e. OFF configs + shipped binaries) -> byte-identical
//     relative fallback. Passed explicitly so the assertion is
//     compile-definition-independent.
TEST(ResolveShaderPath, EmptySourceDirReturnsRelativeByteIdentical)
{
    // Arrange / Act
    const std::string resolved = cd_sample::resolve_shader_path(
        "shaders/shadow.vert.glsl", std::string_view {});

    // Assert
    EXPECT_EQ(resolved, std::string { "shaders/shadow.vert.glsl" });
}

// (4) source_dir itself does not exist on disk (source tree deleted /
//     moved after build) -> byte-identical relative fallback.
TEST(ResolveShaderPath, AbsentSourceDirReturnsRelativeByteIdentical)
{
    // Arrange
    const std::string ghost =
        (std::filesystem::temp_directory_path() /
         "cd_resolve_shader_path_ghost_never_created")
            .string();
    ASSERT_FALSE(std::filesystem::exists(ghost));

    // Act
    const std::string resolved =
        cd_sample::resolve_shader_path("shaders/line.frag.glsl", ghost);

    // Assert
    EXPECT_EQ(resolved, std::string { "shaders/line.frag.glsl" });
}

// (5) Relative without a directory prefix: the basename IS the whole
//     relative; mapping still lands under source_dir.
TEST(ResolveShaderPath, RelativeWithoutDirPrefixMapsBasename)
{
    // Arrange
    const TempShaderDir dir { "noprefix" };
    write_stub_file(dir.path() / "line.vert.glsl");

    // Act
    const std::string resolved =
        cd_sample::resolve_shader_path("line.vert.glsl", dir.str());

    // Assert
    EXPECT_EQ(resolved, dir.str() + "/line.vert.glsl");
}

}  // namespace
