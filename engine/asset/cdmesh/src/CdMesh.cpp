// =============================================================================
// CHROMODYNAMIC — cd/asset/cdmesh/CdMesh.cpp
// =============================================================================
#include <cd/asset/cdmesh/CdMesh.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>

namespace cd::asset::cdmesh
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
        return std::unexpected(
            cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "index_stride must be 2 or 4")
        );
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

cd::core::Result<CdMesh> decode(const std::uint8_t* bytes, std::size_t size)
{
    if (bytes == nullptr)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kInvalidArgument, "null buffer"));
    if (size < kHeaderSize)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kCorrupt, "header too small"));

    if (bytes[0] != static_cast<std::uint8_t>(kMagic[0]) || bytes[1] != static_cast<std::uint8_t>(kMagic[1]) ||
        bytes[2] != static_cast<std::uint8_t>(kMagic[2]) || bytes[3] != static_cast<std::uint8_t>(kMagic[3]))
    {
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kMagicMismatch, "bad magic"));
    }
    const std::uint32_t version = read_u32(bytes + 4);
    if (version != kFormatVersion)
    {
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kVersionMismatch, "unsupported version"));
    }

    CdMesh out;
    // flags at +8 reserved; skip
    out.vertex_count = read_u32(bytes + 12);
    out.index_count = read_u32(bytes + 16);
    out.vertex_stride = read_u32(bytes + 20);
    out.index_stride = read_u32(bytes + 24);
    out.bbox_min[0] = read_f32(bytes + 28);
    out.bbox_min[1] = read_f32(bytes + 32);
    out.bbox_min[2] = read_f32(bytes + 36);
    out.bbox_max[0] = read_f32(bytes + 40);
    out.bbox_max[1] = read_f32(bytes + 44);
    out.bbox_max[2] = read_f32(bytes + 48);

    if (out.index_stride != 2 && out.index_stride != 4)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kCorrupt, "bad index_stride"));
    if (out.vertex_stride == 0)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kCorrupt, "vertex_stride == 0"));

    const std::size_t vb_bytes = static_cast<std::size_t>(out.vertex_count) * out.vertex_stride;
    const std::size_t ib_bytes = static_cast<std::size_t>(out.index_count) * out.index_stride;
    const std::size_t expected = kHeaderSize + vb_bytes + ib_bytes;
    if (size < expected)
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kCorrupt, "truncated payload"));

    out.vertex_blob.assign(bytes + kHeaderSize, bytes + kHeaderSize + vb_bytes);
    out.index_blob.assign(bytes + kHeaderSize + vb_bytes, bytes + kHeaderSize + vb_bytes + ib_bytes);
    return out;
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
    in.seekg(0);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(file_size));
    if (file_size > 0 && !in.read(reinterpret_cast<char*>(buf.data()), file_size))
        return std::unexpected(cdmesh_errors::make(cdmesh_errors::Code::kIoError, "read failed"));
    return decode(buf.data(), buf.size());
}

}  // namespace cd::asset::cdmesh
