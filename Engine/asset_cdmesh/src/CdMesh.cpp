// =============================================================================
// CHROMODYNAMIC — cd/asset_cdmesh/CdMesh.cpp
// =============================================================================
#include <cd/asset_cdmesh/CdMesh.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>

namespace cd::asset_cdmesh
{

namespace
{

constexpr std::size_t kHeaderSize = 4 + 4 + 4 + 4 + 4 + 4 + 4 + 12 + 12;  // 52 bytes
static_assert(kHeaderSize == 52, "header size sanity");

void write_u32(std::ostream& os, std::uint32_t v)
{
    const std::array<std::uint8_t, 4> b {
        static_cast<std::uint8_t>(v & 0xFFU),
        static_cast<std::uint8_t>((v >> 8) & 0xFFU),
        static_cast<std::uint8_t>((v >> 16) & 0xFFU),
        static_cast<std::uint8_t>((v >> 24) & 0xFFU),
    };
    os.write(reinterpret_cast<const char*>(b.data()), 4);
}

void write_f32(std::ostream& os, float f)
{
    // memcpy avoids reinterpret_cast UB on aliasing across types.
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    write_u32(os, u);
}

[[nodiscard]] std::uint32_t read_u32(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[nodiscard]] float read_f32(const std::uint8_t* p) noexcept
{
    const std::uint32_t u = read_u32(p);
    float f = 0.0F;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

}  // namespace

cd::core::Result<void> save(std::string_view path, const SaveDesc& desc)
{
    if (path.empty())
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "empty path"));
    if (desc.vertex_count == 0)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "vertex_count == 0"));
    if (desc.vertex_stride == 0)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "vertex_stride == 0"));
    if (desc.index_stride != 2 && desc.index_stride != 4)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "index_stride must be 2 or 4"));
    if (desc.vertices.size() < static_cast<std::size_t>(desc.vertex_count) * desc.vertex_stride)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "vertices span too small"));
    if (desc.indices.size() < static_cast<std::size_t>(desc.index_count) * desc.index_stride)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "indices span too small"));

    std::ofstream out { std::string { path }, std::ios::binary | std::ios::trunc };
    if (!out.is_open())
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kIoError, std::string { path }));

    out.write(kMagic.data(), 4);
    write_u32(out, kFormatVersion);
    write_u32(out, 0U);  // flags reserved
    write_u32(out, desc.vertex_count);
    write_u32(out, desc.index_count);
    write_u32(out, desc.vertex_stride);
    write_u32(out, desc.index_stride);
    write_f32(out, desc.bbox_min[0]);
    write_f32(out, desc.bbox_min[1]);
    write_f32(out, desc.bbox_min[2]);
    write_f32(out, desc.bbox_max[0]);
    write_f32(out, desc.bbox_max[1]);
    write_f32(out, desc.bbox_max[2]);

    out.write(
        reinterpret_cast<const char*>(desc.vertices.data()),
        static_cast<std::streamsize>(static_cast<std::size_t>(desc.vertex_count) * desc.vertex_stride)
    );
    out.write(
        reinterpret_cast<const char*>(desc.indices.data()),
        static_cast<std::streamsize>(static_cast<std::size_t>(desc.index_count) * desc.index_stride)
    );
    if (!out.good())
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kIoError, "write failed"));
    return {};
}

cd::core::Result<CdMesh> load(std::string_view path)
{
    if (path.empty())
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "empty path"));
    const std::string p { path };
    if (!std::filesystem::exists(p))
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kFileNotFound, p));

    std::ifstream in { p, std::ios::binary | std::ios::ate };
    if (!in.is_open())
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kIoError, p));

    const auto file_size = in.tellg();
    if (file_size < static_cast<std::streamoff>(kHeaderSize))
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kCorrupt, "header too small"));
    in.seekg(0);

    // Read full header into a scratch buffer; cheap and avoids many small reads.
    std::array<std::uint8_t, kHeaderSize> hdr {};
    in.read(reinterpret_cast<char*>(hdr.data()), static_cast<std::streamsize>(kHeaderSize));
    if (!in.good())
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kIoError, "header read failed"));

    if (hdr[0] != static_cast<std::uint8_t>(kMagic[0]) || hdr[1] != static_cast<std::uint8_t>(kMagic[1]) ||
        hdr[2] != static_cast<std::uint8_t>(kMagic[2]) || hdr[3] != static_cast<std::uint8_t>(kMagic[3]))
    {
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kMagicMismatch, "bad magic"));
    }
    const std::uint32_t version = read_u32(hdr.data() + 4);
    if (version != kFormatVersion)
    {
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kVersionMismatch, "unsupported version"));
    }

    CdMesh out;
    // flags at +8 reserved; skip
    out.vertex_count = read_u32(hdr.data() + 12);
    out.index_count = read_u32(hdr.data() + 16);
    out.vertex_stride = read_u32(hdr.data() + 20);
    out.index_stride = read_u32(hdr.data() + 24);
    out.bbox_min[0] = read_f32(hdr.data() + 28);
    out.bbox_min[1] = read_f32(hdr.data() + 32);
    out.bbox_min[2] = read_f32(hdr.data() + 36);
    out.bbox_max[0] = read_f32(hdr.data() + 40);
    out.bbox_max[1] = read_f32(hdr.data() + 44);
    out.bbox_max[2] = read_f32(hdr.data() + 48);

    if (out.index_stride != 2 && out.index_stride != 4)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kCorrupt, "bad index_stride"));
    if (out.vertex_stride == 0)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kCorrupt, "vertex_stride == 0"));

    const std::size_t vb_bytes = static_cast<std::size_t>(out.vertex_count) * out.vertex_stride;
    const std::size_t ib_bytes = static_cast<std::size_t>(out.index_count) * out.index_stride;
    const std::size_t expected = kHeaderSize + vb_bytes + ib_bytes;
    if (static_cast<std::size_t>(file_size) < expected)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kCorrupt, "truncated payload"));

    out.vertex_blob.resize(vb_bytes);
    out.index_blob.resize(ib_bytes);
    if (vb_bytes > 0)
    {
        in.read(reinterpret_cast<char*>(out.vertex_blob.data()), static_cast<std::streamsize>(vb_bytes));
        if (!in.good())
            return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kIoError, "vertex blob read"));
    }
    if (ib_bytes > 0)
    {
        in.read(reinterpret_cast<char*>(out.index_blob.data()), static_cast<std::streamsize>(ib_bytes));
        if (!in.good())
            return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kIoError, "index blob read"));
    }
    return out;
}

}  // namespace cd::asset_cdmesh
