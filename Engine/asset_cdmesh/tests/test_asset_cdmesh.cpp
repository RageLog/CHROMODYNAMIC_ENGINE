// =============================================================================
// CHROMODYNAMIC — cd::asset_cdmesh tests
// =============================================================================
#include <cd/asset_cdmesh/CdMesh.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

[[nodiscard]] fs::path tmp_path()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("cd_cdmesh_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                                        std::to_string(seq.fetch_add(1)) + ".cdmesh");
}

struct PathGuard
{
    fs::path path;
    explicit PathGuard(fs::path p)
        : path { std::move(p) }
    {
    }
    ~PathGuard()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    PathGuard(const PathGuard&) = delete;
    PathGuard& operator=(const PathGuard&) = delete;
    PathGuard(PathGuard&&) = delete;
    PathGuard& operator=(PathGuard&&) = delete;
};

}  // namespace

TEST(CdMesh, SaveLoadRoundtripStdVertex)
{
    PathGuard g { tmp_path() };

    std::vector<cd::asset_cdmesh::CdVertexStd> verts = {
        { { 0.0F, 0.5F, 0.0F }, { 0.0F, 0.0F, 1.0F }, { 0.5F, 0.0F } },
        { { -0.5F, -0.5F, 0.0F }, { 0.0F, 0.0F, 1.0F }, { 0.0F, 1.0F } },
        { { 0.5F, -0.5F, 0.0F }, { 0.0F, 0.0F, 1.0F }, { 1.0F, 1.0F } },
    };
    std::vector<std::uint32_t> idx = { 0, 1, 2 };

    cd::asset_cdmesh::SaveDesc d {};
    d.vertices = std::span<const std::uint8_t> { reinterpret_cast<const std::uint8_t*>(verts.data()),
                                                  verts.size() * sizeof(cd::asset_cdmesh::CdVertexStd) };
    d.indices = std::span<const std::uint8_t> { reinterpret_cast<const std::uint8_t*>(idx.data()),
                                                 idx.size() * sizeof(std::uint32_t) };
    d.vertex_count = static_cast<std::uint32_t>(verts.size());
    d.index_count = static_cast<std::uint32_t>(idx.size());
    d.vertex_stride = sizeof(cd::asset_cdmesh::CdVertexStd);
    d.index_stride = 4;
    d.bbox_min = { -0.5F, -0.5F, 0.0F };
    d.bbox_max = { 0.5F, 0.5F, 0.0F };

    ASSERT_TRUE(cd::asset_cdmesh::save(g.path.string(), d).has_value());

    auto loaded = cd::asset_cdmesh::load(g.path.string());
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_EQ(loaded->vertex_count, 3U);
    EXPECT_EQ(loaded->index_count, 3U);
    EXPECT_EQ(loaded->vertex_stride, sizeof(cd::asset_cdmesh::CdVertexStd));
    EXPECT_EQ(loaded->index_stride, 4U);

    const auto vspan = loaded->std_vertices();
    ASSERT_EQ(vspan.size(), 3U);
    EXPECT_FLOAT_EQ(vspan[0].position[1], 0.5F);
    EXPECT_FLOAT_EQ(vspan[2].position[0], 0.5F);

    const auto ispan = loaded->indices_u32();
    ASSERT_EQ(ispan.size(), 3U);
    EXPECT_EQ(ispan[0], 0U);
    EXPECT_EQ(ispan[1], 1U);
    EXPECT_EQ(ispan[2], 2U);

    EXPECT_FLOAT_EQ(loaded->bbox_min[0], -0.5F);
    EXPECT_FLOAT_EQ(loaded->bbox_max[1], 0.5F);
}

TEST(CdMesh, RoundtripU16Indices)
{
    PathGuard g { tmp_path() };
    std::vector<std::uint16_t> idx = { 0, 1, 2, 0, 2, 3 };
    std::vector<cd::asset_cdmesh::CdVertexStd> verts(4);

    cd::asset_cdmesh::SaveDesc d {};
    d.vertices = std::span<const std::uint8_t> { reinterpret_cast<const std::uint8_t*>(verts.data()),
                                                  verts.size() * sizeof(cd::asset_cdmesh::CdVertexStd) };
    d.indices = std::span<const std::uint8_t> { reinterpret_cast<const std::uint8_t*>(idx.data()),
                                                 idx.size() * sizeof(std::uint16_t) };
    d.vertex_count = 4;
    d.index_count = static_cast<std::uint32_t>(idx.size());
    d.vertex_stride = sizeof(cd::asset_cdmesh::CdVertexStd);
    d.index_stride = 2;
    ASSERT_TRUE(cd::asset_cdmesh::save(g.path.string(), d).has_value());

    auto loaded = cd::asset_cdmesh::load(g.path.string());
    ASSERT_TRUE(loaded.has_value());
    const auto ispan = loaded->indices_u16();
    ASSERT_EQ(ispan.size(), 6U);
    EXPECT_EQ(ispan[5], 3U);
    // u32 view returns empty when stride is 2.
    EXPECT_TRUE(loaded->indices_u32().empty());
}

TEST(CdMesh, MissingFileReturnsFileNotFound)
{
    auto r = cd::asset_cdmesh::load("c:/no/such/path.cdmesh");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_cdmesh::cdmesh_errors::Code::kFileNotFound));
}

TEST(CdMesh, BadMagicReturnsMagicMismatch)
{
    PathGuard g { tmp_path() };
    // Write 64 bytes of zeros — header passes the size check but magic bytes don't match.
    {
        std::ofstream f(g.path, std::ios::binary);
        std::vector<std::uint8_t> zeros(64, 0);
        f.write(reinterpret_cast<const char*>(zeros.data()), 64);
    }
    auto r = cd::asset_cdmesh::load(g.path.string());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_cdmesh::cdmesh_errors::Code::kMagicMismatch));
}

TEST(CdMesh, TruncatedPayloadReturnsCorrupt)
{
    PathGuard g { tmp_path() };
    // Build a valid header that promises 1000 vertices then write just the header.
    {
        std::ofstream f(g.path, std::ios::binary);
        f.write(cd::asset_cdmesh::kMagic.data(), 4);
        auto w32 = [&](std::uint32_t v)
        {
            std::uint8_t b[4] { static_cast<std::uint8_t>(v),
                                static_cast<std::uint8_t>(v >> 8),
                                static_cast<std::uint8_t>(v >> 16),
                                static_cast<std::uint8_t>(v >> 24) };
            f.write(reinterpret_cast<const char*>(b), 4);
        };
        w32(cd::asset_cdmesh::kFormatVersion);
        w32(0);     // flags
        w32(1000);  // vertex_count
        w32(0);     // index_count
        w32(32);    // vertex_stride
        w32(4);     // index_stride
        for (int i = 0; i < 6; ++i)
            w32(0);  // bbox floats as zeros (interpreted as 0.0f)
    }
    auto r = cd::asset_cdmesh::load(g.path.string());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_cdmesh::cdmesh_errors::Code::kCorrupt));
}

TEST(CdMesh, RejectsZeroVertexCountOnSave)
{
    cd::asset_cdmesh::SaveDesc d {};
    d.vertex_stride = 32;
    d.index_stride = 4;
    auto r = cd::asset_cdmesh::save("anything.cdmesh", d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_cdmesh::cdmesh_errors::Code::kInvalidArgument));
}

TEST(CdMesh, RejectsBadIndexStride)
{
    std::vector<cd::asset_cdmesh::CdVertexStd> v(1);
    std::vector<std::uint8_t> i(1);
    cd::asset_cdmesh::SaveDesc d {};
    d.vertices = std::span<const std::uint8_t> { reinterpret_cast<const std::uint8_t*>(v.data()),
                                                  v.size() * sizeof(cd::asset_cdmesh::CdVertexStd) };
    d.indices = std::span<const std::uint8_t> { i.data(), i.size() };
    d.vertex_count = 1;
    d.index_count = 1;
    d.vertex_stride = sizeof(cd::asset_cdmesh::CdVertexStd);
    d.index_stride = 1;  // illegal — must be 2 or 4
    auto r = cd::asset_cdmesh::save("anything.cdmesh", d);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_cdmesh::cdmesh_errors::Code::kInvalidArgument));
}

// ----- AssetLoader adapter -----

#include <cd/asset_cdmesh/AssetLoader.hpp>
#include <cstring>
#include <span>

namespace
{

std::vector<std::byte> build_minimal_cdmesh_bytes()
{
    // Round-trip via save() into an in-memory ostringstream-like buffer
    // is complex; instead, save() writes a real file and we then re-read
    // it as bytes. Use a temp file. Keep this helper local to the test.
    cd::asset_cdmesh::SaveDesc d;
    cd::asset_cdmesh::CdVertexStd v {};
    std::vector<std::uint8_t> vb(sizeof(v), 0);
    std::memcpy(vb.data(), &v, sizeof(v));
    std::vector<std::uint8_t> ib(sizeof(std::uint16_t), 0);
    d.vertices = std::span<const std::uint8_t> { vb.data(), vb.size() };
    d.indices = std::span<const std::uint8_t> { ib.data(), ib.size() };
    d.vertex_count = 1;
    d.index_count = 1;
    d.vertex_stride = sizeof(cd::asset_cdmesh::CdVertexStd);
    d.index_stride = 2;
    const auto p = std::filesystem::temp_directory_path() / "cd_adapter_tmp.cdmesh";
    auto save_r = cd::asset_cdmesh::save(p.string(), d);
    if (!save_r) return {};
    std::vector<std::byte> out;
    {
        // Scope the ifstream so it is closed before remove() runs — on
        // Windows the file is exclusively held until destruction.
        std::ifstream f { p, std::ios::binary | std::ios::ate };
        out.resize(static_cast<std::size_t>(f.tellg()));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    std::error_code ec;
    std::filesystem::remove(p, ec);  // best-effort; leak a few KB if Windows fights us.
    return out;
}

}  // namespace

TEST(CdMeshAssetLoader, AdapterDecodesValid)
{
    auto bytes = build_minimal_cdmesh_bytes();
    ASSERT_FALSE(bytes.empty());
    cd::asset_cdmesh::CdMeshAssetLoader loader;
    EXPECT_EQ(loader.tag(), "cdmesh");
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "t.cdmesh");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* m = dynamic_cast<cd::asset_cdmesh::CdMeshAsset*>(r->get());
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->mesh().vertex_count, 1u);
    EXPECT_EQ(m->mesh().index_count, 1u);
}
