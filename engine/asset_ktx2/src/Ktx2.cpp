// =============================================================================
// CHROMODYNAMIC — cd/asset_ktx2/Ktx2.cpp
// =============================================================================
#include <cd/asset_ktx2/Ktx2.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace cd::asset_ktx2
{

namespace
{

// KTX2 header layout (Khronos KTX2 v1, little-endian):
//
//   ┌─────────────── HEADER (80 B) ───────────────┐
//   │ identifier[12]   « See magic table below »   │
//   │ vkFormat              u32  (Vulkan enum)     │
//   │ typeSize              u32  (bytes per scalar)│
//   │ pixelWidth            u32                    │
//   │ pixelHeight           u32                    │
//   │ pixelDepth            u32  (0 for 2D)        │
//   │ layerCount            u32  (0 for non-array) │
//   │ faceCount             u32  (1 for 2D, 6 cube)│
//   │ levelCount            u32                    │
//   │ supercompressionScheme u32  (0 = none)       │
//   │ DFD byte offset       u32                    │
//   │ DFD byte length       u32                    │
//   │ KVD byte offset       u32                    │
//   │ KVD byte length       u32                    │
//   │ SGD byte offset       u64                    │
//   │ SGD byte length       u64                    │
//   └─────────────────────────────────────────────┘
//   Level index table: levelCount * 24 bytes (offset+length+uncompressed)
//   then DFD, KVD, SGD blobs, then the actual mip data referenced by the
//   level index entries.

constexpr std::array<std::uint8_t, 12> kMagic {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};
constexpr std::size_t kHeaderSize = 12 + 4 * 13 + 8 * 2;  // 12 magic + 13 u32 + 2 u64 = 80
constexpr std::size_t kLevelEntrySize = 24;

static_assert(kHeaderSize == 80, "KTX2 header size sanity");

[[nodiscard]] std::uint32_t read_u32_le(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[nodiscard]] std::uint64_t read_u64_le(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint64_t>(read_u32_le(p)) | (static_cast<std::uint64_t>(read_u32_le(p + 4)) << 32);
}

[[nodiscard]] bool is_supported_format(std::uint32_t v) noexcept
{
    switch (v)
    {
        case static_cast<std::uint32_t>(Ktx2VkFormat::kR8G8B8A8_Unorm):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kR8G8B8A8_Srgb):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kB8G8R8A8_Unorm):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kB8G8R8A8_Srgb):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kBC7_Unorm):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kBC7_Srgb):
            return true;
        default:
            return false;
    }
}

[[nodiscard]] cd::core::Result<Ktx2> parse(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kHeaderSize)
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kCorrupt, "header too small"));
    }
    for (std::size_t i = 0; i < kMagic.size(); ++i)
    {
        if (bytes[i] != kMagic[i])
        {
            return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kMagicMismatch, "bad KTX2 magic"));
        }
    }

    const std::uint8_t* p = bytes.data() + 12;  // past identifier
    const std::uint32_t vk_format = read_u32_le(p + 0);
    // typeSize at +4 — we don't enforce
    const std::uint32_t pixel_w = read_u32_le(p + 8);
    const std::uint32_t pixel_h = read_u32_le(p + 12);
    const std::uint32_t pixel_d = read_u32_le(p + 16);
    const std::uint32_t layer_count = read_u32_le(p + 20);
    const std::uint32_t face_count = read_u32_le(p + 24);
    const std::uint32_t level_count = read_u32_le(p + 28);
    const std::uint32_t supercompression = read_u32_le(p + 32);
    // DFD offset/length at +36..+44
    // KVD offset/length at +44..+52
    // SGD offset/length at +52..+68 (u64s)

    if (supercompression != 0)
    {
        return std::unexpected(
            ktx2_errors::make(
                ktx2_errors::Code::kUnsupportedSupercompression,
                "v1 reader does not implement Basis / Zstd"
            )
        );
    }
    if (pixel_w == 0 || pixel_h == 0)
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kCorrupt, "zero dimension"));
    }
    if (pixel_d > 1)
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kUnsupportedFormat, "3D textures not in v1"));
    }
    if (face_count != 1)
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kUnsupportedFormat, "cubemaps not in v1"));
    }
    if (layer_count > 1)
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kUnsupportedFormat, "array textures not in v1"));
    }
    if (level_count == 0)
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kCorrupt, "levelCount == 0"));
    }
    if (!is_supported_format(vk_format))
    {
        return std::unexpected(
            ktx2_errors::make(ktx2_errors::Code::kUnsupportedFormat, "vkFormat not on supported short-list")
        );
    }

    // Level index table immediately follows the 80-byte header.
    const std::size_t level_table_off = kHeaderSize;
    const std::size_t level_table_bytes = static_cast<std::size_t>(level_count) * kLevelEntrySize;
    if (bytes.size() < level_table_off + level_table_bytes)
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kCorrupt, "level table out of range"));
    }

    Ktx2 out;
    out.format = static_cast<Ktx2VkFormat>(vk_format);
    out.width = pixel_w;
    out.height = pixel_h;
    out.mips.reserve(level_count);

    // KTX2 stores mips smallest-first in the level index table but
    // logically the application wants mip0 = base. We reverse on output.
    std::vector<Ktx2Mip> reversed(level_count);
    for (std::uint32_t lvl = 0; lvl < level_count; ++lvl)
    {
        const std::uint8_t* entry = bytes.data() + level_table_off + lvl * kLevelEntrySize;
        const std::uint64_t byte_offset = read_u64_le(entry + 0);
        const std::uint64_t byte_length = read_u64_le(entry + 8);
        // uncompressedByteLength at +16 (u64); we don't enforce.
        if (byte_offset > bytes.size() || byte_length > bytes.size() || byte_offset + byte_length > bytes.size())
        {
            return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kCorrupt, "level extends past file"));
        }
        Ktx2Mip m;
        m.width = std::max(pixel_w >> lvl, 1U);
        m.height = std::max(pixel_h >> lvl, 1U);
        m.bytes.resize(static_cast<std::size_t>(byte_length));
        std::memcpy(m.bytes.data(), bytes.data() + byte_offset, m.bytes.size());
        // KTX2 stores level 0 (largest) FIRST in the table — invariant
        // since v2 of the spec. So we keep insertion order = mip 0
        // first; the "smallest-first" rumour was about KTX1.
        out.mips.push_back(std::move(m));
    }
    (void)reversed;  // unused; left here as a marker for the v1/v2 confusion.

    return out;
}

}  // namespace

cd::core::Result<Ktx2> load_from_memory(std::span<const std::uint8_t> bytes)
{
    return parse(bytes);
}

namespace
{

void write_u32_le(std::uint8_t* p, std::uint32_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

void write_u64_le(std::uint8_t* p, std::uint64_t v) noexcept
{
    write_u32_le(p + 0, static_cast<std::uint32_t>(v & 0xFFFFFFFFu));
    write_u32_le(p + 4, static_cast<std::uint32_t>((v >> 32) & 0xFFFFFFFFu));
}

[[nodiscard]] std::uint32_t type_size_for(std::uint32_t vk_format) noexcept
{
    switch (vk_format)
    {
        case static_cast<std::uint32_t>(Ktx2VkFormat::kR8G8B8A8_Unorm):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kR8G8B8A8_Srgb):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kB8G8R8A8_Unorm):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kB8G8R8A8_Srgb):
            return 1;  // unsigned 8-bit components
        case static_cast<std::uint32_t>(Ktx2VkFormat::kBC7_Unorm):
        case static_cast<std::uint32_t>(Ktx2VkFormat::kBC7_Srgb):
            return 1;  // compressed: spec says typeSize == 1
        default:
            return 1;
    }
}

}  // namespace

cd::core::Result<std::vector<std::uint8_t>> encode_to_memory(const Ktx2& tex)
{
    if (tex.mips.empty())
    {
        return std::unexpected(
            ktx2_errors::make(ktx2_errors::Code::kInvalidArgument, "Ktx2 has no mips"));
    }
    const auto fmt_u = static_cast<std::uint32_t>(tex.format);
    if (!is_supported_format(fmt_u))
    {
        return std::unexpected(
            ktx2_errors::make(ktx2_errors::Code::kUnsupportedFormat,
                              "encode: vkFormat not on supported short-list"));
    }

    const auto level_count = static_cast<std::uint32_t>(tex.mips.size());

    // Layout: HEADER (80) + level index table (24 * N) + DFD/KVD/SGD
    // (all empty per the marathon-scope decision) + payload.
    // KTX2 stores levels smallest-first in payload but level entry 0
    // (largest) still appears first in the index table; offsets in
    // the index point at the payload regions however they land.
    //
    // We pack payload smallest-mip-first (per KTX2 spec for
    // "compactness"), but the *index entries* are written in mip0..N
    // order. Level 0's data lives at the highest payload offset.
    const std::size_t level_table_off = kHeaderSize;
    const std::size_t level_table_bytes =
        static_cast<std::size_t>(level_count) * kLevelEntrySize;
    const std::size_t payload_off = level_table_off + level_table_bytes;

    // Compute per-level payload offsets (smallest mip first in file).
    std::vector<std::uint64_t> level_off(level_count);
    std::vector<std::uint64_t> level_len(level_count);
    {
        std::size_t cursor = payload_off;
        // Smallest mip first: walk mips in reverse.
        for (std::uint32_t i = level_count; i-- > 0;)
        {
            level_off[i] = cursor;
            level_len[i] = static_cast<std::uint64_t>(tex.mips[i].bytes.size());
            cursor += tex.mips[i].bytes.size();
        }
    }
    const std::size_t total_size = level_off.empty()
        ? payload_off
        : (level_off[0] + level_len[0]);  // mip0 is the last in payload

    std::vector<std::uint8_t> out(total_size, 0);

    // Identifier.
    std::memcpy(out.data(), kMagic.data(), kMagic.size());

    // Header fields (start at +12).
    std::uint8_t* hp = out.data() + 12;
    write_u32_le(hp + 0,  fmt_u);                                       // vkFormat
    write_u32_le(hp + 4,  type_size_for(fmt_u));                        // typeSize
    write_u32_le(hp + 8,  tex.width);                                   // pixelWidth
    write_u32_le(hp + 12, tex.height);                                  // pixelHeight
    write_u32_le(hp + 16, 0);                                           // pixelDepth (2D)
    write_u32_le(hp + 20, 0);                                           // layerCount (non-array)
    write_u32_le(hp + 24, 1);                                           // faceCount (2D)
    write_u32_le(hp + 28, level_count);                                 // levelCount
    write_u32_le(hp + 32, 0);                                           // supercompressionScheme
    write_u32_le(hp + 36, 0);                                           // DFD offset
    write_u32_le(hp + 40, 0);                                           // DFD length
    write_u32_le(hp + 44, 0);                                           // KVD offset
    write_u32_le(hp + 48, 0);                                           // KVD length
    write_u64_le(hp + 52, 0);                                           // SGD offset
    write_u64_le(hp + 60, 0);                                           // SGD length

    // Level index table (mip0 .. mipN).
    for (std::uint32_t i = 0; i < level_count; ++i)
    {
        std::uint8_t* entry = out.data() + level_table_off + i * kLevelEntrySize;
        write_u64_le(entry + 0,  level_off[i]);
        write_u64_le(entry + 8,  level_len[i]);
        write_u64_le(entry + 16, level_len[i]);  // uncompressed = compressed (no SC)
    }

    // Payload.
    for (std::uint32_t i = 0; i < level_count; ++i)
    {
        if (level_len[i] == 0)
            continue;
        std::memcpy(out.data() + level_off[i],
                    tex.mips[i].bytes.data(),
                    tex.mips[i].bytes.size());
    }

    return out;
}

cd::core::Result<void> write(const Ktx2& tex, std::string_view path)
{
    auto encoded = encode_to_memory(tex);
    if (!encoded.has_value())
        return std::unexpected(encoded.error());

    if (path.empty())
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kInvalidArgument, "empty path"));

    const std::string p { path };
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kIoError, "open failed: " + p));

    out.write(reinterpret_cast<const char*>(encoded->data()),
              static_cast<std::streamsize>(encoded->size()));
    if (!out.good())
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kIoError, "write failed: " + p));

    return {};
}

cd::core::Result<Ktx2> load(std::string_view path)
{
    if (path.empty())
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kInvalidArgument, "empty path"));
    }
    const std::string p { path };
    if (!std::filesystem::exists(p))
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kFileNotFound, p));
    }
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in.is_open())
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kIoError, p));
    }
    const auto sz = in.tellg();
    if (sz <= 0)
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kCorrupt, "empty file"));
    }
    in.seekg(0);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    in.read(reinterpret_cast<char*>(buf.data()), sz);
    if (!in.good())
    {
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kIoError, "read failed"));
    }
    return parse(std::span<const std::uint8_t>(buf.data(), buf.size()));
}

}  // namespace cd::asset_ktx2
