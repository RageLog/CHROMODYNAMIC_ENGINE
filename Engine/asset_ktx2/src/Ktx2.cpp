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

constexpr std::array<std::uint8_t, 12> kMagic { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
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
    return static_cast<std::uint64_t>(read_u32_le(p)) |
           (static_cast<std::uint64_t>(read_u32_le(p + 4)) << 32);
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
            ktx2_errors::make(ktx2_errors::Code::kUnsupportedSupercompression,
                              "v1 reader does not implement Basis / Zstd")
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
        return std::unexpected(ktx2_errors::make(ktx2_errors::Code::kUnsupportedFormat,
                                                 "vkFormat not on supported short-list"));
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
        if (byte_offset > bytes.size() || byte_length > bytes.size() ||
            byte_offset + byte_length > bytes.size())
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
