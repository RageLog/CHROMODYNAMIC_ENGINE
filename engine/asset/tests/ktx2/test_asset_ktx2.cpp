// =============================================================================
// CHROMODYNAMIC — cd::asset_ktx2 tests
//
// Synthetic minimal KTX2 files written by the test, decoded by load().
// We don't reach for a real toktx-produced asset — the parser exercises
// the same code path either way and synthetic data keeps the test
// self-contained.
// =============================================================================
#include <cd/asset/ktx2/Ktx2.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

[[nodiscard]] fs::path tmp_path(std::string_view suffix = ".ktx2")
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("cd_ktx2_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                                        std::to_string(seq.fetch_add(1)) + std::string { suffix });
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 24));
}

void write_u64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    write_u32(out, static_cast<std::uint32_t>(v));
    write_u32(out, static_cast<std::uint32_t>(v >> 32));
}

/// Build a minimal valid KTX2 file with a single mip of the given
/// dimensions + format + payload bytes. Layout matches the parser's
/// expectations exactly.
[[nodiscard]] std::vector<std::uint8_t>
build_minimal_ktx2(std::uint32_t vk_format, std::uint32_t w, std::uint32_t h, std::vector<std::uint8_t> payload)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(80 + 24 + payload.size());
    // Magic
    const std::uint8_t magic[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    bytes.insert(bytes.end(), std::begin(magic), std::end(magic));
    // vkFormat, typeSize=1
    write_u32(bytes, vk_format);
    write_u32(bytes, 1);
    // pixelWidth, height, depth=0, layerCount=0, faceCount=1, levelCount=1
    write_u32(bytes, w);
    write_u32(bytes, h);
    write_u32(bytes, 0);
    write_u32(bytes, 0);
    write_u32(bytes, 1);
    write_u32(bytes, 1);
    // supercompressionScheme=0
    write_u32(bytes, 0);
    // DFD offset/length = 0 (we ignore it)
    write_u32(bytes, 0);
    write_u32(bytes, 0);
    // KVD offset/length = 0
    write_u32(bytes, 0);
    write_u32(bytes, 0);
    // SGD offset/length = 0
    write_u64(bytes, 0);
    write_u64(bytes, 0);
    // Level index (single entry): payload starts right after.
    const std::uint64_t payload_off = 80 + 24;
    write_u64(bytes, payload_off);
    write_u64(bytes, payload.size());
    write_u64(bytes, payload.size());  // uncompressedByteLength
    // Payload
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

void write_bytes(const fs::path& p, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST(Ktx2, BC7Minimal4x4Roundtrips)
{
    std::vector<std::uint8_t> payload(16, 0xAB);  // one 4x4 BC7 block
    const auto bytes =
        build_minimal_ktx2(static_cast<std::uint32_t>(cd::asset::ktx2::Ktx2VkFormat::kBC7_Unorm), 4, 4, payload);
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->format, cd::asset::ktx2::Ktx2VkFormat::kBC7_Unorm);
    EXPECT_EQ(r->width, 4U);
    EXPECT_EQ(r->height, 4U);
    ASSERT_EQ(r->mips.size(), 1U);
    EXPECT_EQ(r->mips[0].bytes.size(), 16U);
    EXPECT_EQ(r->mips[0].bytes[0], 0xAB);
}

TEST(Ktx2, RGBA8RoundtripsFromMemory)
{
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(8) * 8 * 4, 0x77);
    const auto bytes =
        build_minimal_ktx2(static_cast<std::uint32_t>(cd::asset::ktx2::Ktx2VkFormat::kR8G8B8A8_Unorm), 8, 8, payload);
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->format, cd::asset::ktx2::Ktx2VkFormat::kR8G8B8A8_Unorm);
    EXPECT_EQ(r->width, 8U);
    EXPECT_EQ(r->height, 8U);
}

TEST(Ktx2, DiskRoundtripMatchesMemory)
{
    std::vector<std::uint8_t> payload(64, 0x42);
    const auto bytes =
        build_minimal_ktx2(static_cast<std::uint32_t>(cd::asset::ktx2::Ktx2VkFormat::kBC7_Srgb), 4, 4, payload);
    const auto p = tmp_path();
    write_bytes(p, bytes);
    auto disk = cd::asset::ktx2::load(p.string());
    auto mem = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_TRUE(disk.has_value()) << disk.error().message;
    ASSERT_TRUE(mem.has_value());
    EXPECT_EQ(disk->width, mem->width);
    EXPECT_EQ(disk->format, mem->format);
    EXPECT_EQ(disk->mips[0].bytes, mem->mips[0].bytes);
    fs::remove(p);
}

TEST(Ktx2, MissingFileReturnsFileNotFound)
{
    auto r = cd::asset::ktx2::load("c:/no/such/file.ktx2");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::ktx2::ktx2_errors::Code::kFileNotFound));
}

TEST(Ktx2, BadMagicReturnsMagicMismatch)
{
    std::vector<std::uint8_t> junk(120, 0);
    junk[0] = 'n';
    auto r = cd::asset::ktx2::load_from_memory(junk);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::ktx2::ktx2_errors::Code::kMagicMismatch));
}

TEST(Ktx2, UnsupportedFormatReturnsKUnsupportedFormat)
{
    // Use VK_FORMAT_R64_UINT (110) — not on our short-list.
    std::vector<std::uint8_t> payload(64, 0);
    const auto bytes = build_minimal_ktx2(110, 4, 4, payload);
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::ktx2::ktx2_errors::Code::kUnsupportedFormat));
}

TEST(Ktx2, SupercompressionRejectedAsUnsupported)
{
    // Build a header that claims Zstd supercompression (scheme=2).
    auto bytes = build_minimal_ktx2(
        static_cast<std::uint32_t>(cd::asset::ktx2::Ktx2VkFormat::kBC7_Unorm),
        4,
        4,
        std::vector<std::uint8_t>(16, 0)
    );
    // Patch supercompressionScheme to 2 (Zstd). Field is at byte offset
    // 44 inside the KTX2 header (12-byte identifier + 8 prior u32 fields:
    // vkFormat, typeSize, w, h, d, layerCount, faceCount, levelCount).
    const std::size_t off = 12 + 4 * 8;
    bytes[off + 0] = 2;
    bytes[off + 1] = 0;
    bytes[off + 2] = 0;
    bytes[off + 3] = 0;
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(
        r.error().code,
        static_cast<std::uint32_t>(cd::asset::ktx2::ktx2_errors::Code::kUnsupportedSupercompression)
    );
}

TEST(Ktx2, CubemapRejectedAsUnsupportedFormat)
{
    auto bytes = build_minimal_ktx2(
        static_cast<std::uint32_t>(cd::asset::ktx2::Ktx2VkFormat::kBC7_Unorm),
        4,
        4,
        std::vector<std::uint8_t>(16, 0)
    );
    // Patch faceCount → 6 (offset 12 + 4*6 = 36 = 8th u32).
    // Field order: vkFormat typeSize w h d layer face level supercomp...
    // face is index 6 (0-based after identifier).
    const std::size_t face_off = 12 + 4 * 6;
    bytes[face_off + 0] = 6;
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::ktx2::ktx2_errors::Code::kUnsupportedFormat));
}

// ----- encode_to_memory + write (Phase 14.B / Wave 158) -----

TEST(Ktx2Encode, EncodeDecodeRoundTripsRgba8)
{
    cd::asset::ktx2::Ktx2 src;
    src.format = cd::asset::ktx2::Ktx2VkFormat::kR8G8B8A8_Unorm;
    src.width = 4;
    src.height = 4;
    cd::asset::ktx2::Ktx2Mip m0;
    m0.width = 4;
    m0.height = 4;
    m0.bytes.assign(static_cast<std::size_t>(4 * 4 * 4), std::uint8_t { 0x77 });
    src.mips.push_back(std::move(m0));

    auto enc = cd::asset::ktx2::encode_to_memory(src);
    ASSERT_TRUE(enc.has_value()) << enc.error().message;
    auto dec = cd::asset::ktx2::load_from_memory(*enc);
    ASSERT_TRUE(dec.has_value()) << dec.error().message;
    EXPECT_EQ(dec->format, src.format);
    EXPECT_EQ(dec->width, 4U);
    EXPECT_EQ(dec->height, 4U);
    ASSERT_EQ(dec->mips.size(), 1U);
    EXPECT_EQ(dec->mips[0].bytes, src.mips[0].bytes);
}

TEST(Ktx2Encode, EncodeMipChainPreservesEveryLevel)
{
    cd::asset::ktx2::Ktx2 src;
    src.format = cd::asset::ktx2::Ktx2VkFormat::kBC7_Unorm;
    src.width = 8;
    src.height = 8;
    // Three mip levels: 8x8 (4 BC7 blocks), 4x4 (1 block), 2x2 (1 padded block).
    for (std::uint32_t lvl = 0; lvl < 3; ++lvl)
    {
        cd::asset::ktx2::Ktx2Mip m;
        m.width = std::max(8U >> lvl, 1U);
        m.height = std::max(8U >> lvl, 1U);
        const std::size_t block_w = (m.width + 3) / 4;
        const std::size_t block_h = (m.height + 3) / 4;
        m.bytes.assign(block_w * block_h * 16,
                       static_cast<std::uint8_t>(0x10 + lvl));
        src.mips.push_back(std::move(m));
    }

    auto enc = cd::asset::ktx2::encode_to_memory(src);
    ASSERT_TRUE(enc.has_value()) << enc.error().message;
    auto dec = cd::asset::ktx2::load_from_memory(*enc);
    ASSERT_TRUE(dec.has_value()) << dec.error().message;
    ASSERT_EQ(dec->mips.size(), src.mips.size());
    for (std::size_t i = 0; i < src.mips.size(); ++i)
    {
        EXPECT_EQ(dec->mips[i].bytes, src.mips[i].bytes)
            << "mip " << i << " mismatched";
        EXPECT_EQ(dec->mips[i].width, src.mips[i].width);
        EXPECT_EQ(dec->mips[i].height, src.mips[i].height);
    }
}

TEST(Ktx2Encode, WriteToDiskMatchesLoadFromDisk)
{
    cd::asset::ktx2::Ktx2 src;
    src.format = cd::asset::ktx2::Ktx2VkFormat::kBC7_Srgb;
    src.width = 4;
    src.height = 4;
    cd::asset::ktx2::Ktx2Mip m0;
    m0.width = 4;
    m0.height = 4;
    m0.bytes.assign(16, std::uint8_t { 0x3C });
    src.mips.push_back(std::move(m0));

    const auto p = tmp_path();
    auto write_r = cd::asset::ktx2::write(src, p.string());
    ASSERT_TRUE(write_r.has_value()) << write_r.error().message;

    auto disk = cd::asset::ktx2::load(p.string());
    ASSERT_TRUE(disk.has_value()) << disk.error().message;
    EXPECT_EQ(disk->format, src.format);
    EXPECT_EQ(disk->width, src.width);
    EXPECT_EQ(disk->height, src.height);
    ASSERT_EQ(disk->mips.size(), 1U);
    EXPECT_EQ(disk->mips[0].bytes, src.mips[0].bytes);
    fs::remove(p);
}

TEST(Ktx2Encode, EmptyMipsRejected)
{
    cd::asset::ktx2::Ktx2 src;
    src.format = cd::asset::ktx2::Ktx2VkFormat::kR8G8B8A8_Unorm;
    src.width = 4;
    src.height = 4;
    // src.mips intentionally empty.

    auto enc = cd::asset::ktx2::encode_to_memory(src);
    ASSERT_FALSE(enc.has_value());
    EXPECT_EQ(enc.error().code,
              static_cast<std::uint32_t>(cd::asset::ktx2::ktx2_errors::Code::kInvalidArgument));
}

// ----- AssetLoader adapter -----

#include <cd/asset/ktx2/AssetLoader.hpp>

TEST(Ktx2AssetLoader, AdapterDecodesValid)
{
    constexpr std::uint32_t VK_FORMAT_R8G8B8A8_UNORM = 37;
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(4) * 4 * 4, 0x55);
    auto u8 = build_minimal_ktx2(VK_FORMAT_R8G8B8A8_UNORM, 4, 4, std::move(payload));
    std::vector<std::byte> bytes(u8.size());
    std::memcpy(bytes.data(), u8.data(), u8.size());
    cd::asset::ktx2::Ktx2AssetLoader loader;
    EXPECT_EQ(loader.tag(), "ktx2");
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "t.ktx2");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* ka = dynamic_cast<cd::asset::ktx2::Ktx2Asset*>(r->get());
    ASSERT_NE(ka, nullptr);
    EXPECT_EQ(ka->ktx2().width, 4u);
    EXPECT_EQ(ka->ktx2().height, 4u);
}

// =============================================================================
// Robustness / edge / negative coverage (≥80→100 marathon, ADD-ONLY).
// Defensive deserialization: every corrupt header field must yield a typed
// error rather than an out-of-bounds read.
// =============================================================================

namespace
{
using Code = cd::asset::ktx2::ktx2_errors::Code;
constexpr std::uint32_t kBc7Unorm = static_cast<std::uint32_t>(cd::asset::ktx2::Ktx2VkFormat::kBC7_Unorm);

// Header field byte offsets (after the 12-byte identifier).
[[maybe_unused]] constexpr std::size_t kOffVkFormat = 12 + 4 * 0;
constexpr std::size_t kOffPixelWidth = 12 + 4 * 2;
constexpr std::size_t kOffPixelHeight = 12 + 4 * 3;
constexpr std::size_t kOffPixelDepth = 12 + 4 * 4;
constexpr std::size_t kOffLayerCount = 12 + 4 * 5;
constexpr std::size_t kOffLevelCount = 12 + 4 * 7;

void patch_u32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x)
{
    v[off + 0] = static_cast<std::uint8_t>(x);
    v[off + 1] = static_cast<std::uint8_t>(x >> 8);
    v[off + 2] = static_cast<std::uint8_t>(x >> 16);
    v[off + 3] = static_cast<std::uint8_t>(x >> 24);
}
}  // namespace

TEST(Ktx2Edge, HeaderTooSmallReturnsCorrupt)
{
    std::vector<std::uint8_t> tiny(40, 0);
    auto r = cd::asset::ktx2::load_from_memory(tiny);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(Ktx2Edge, ZeroWidthReturnsCorrupt)
{
    auto bytes = build_minimal_ktx2(kBc7Unorm, 4, 4, std::vector<std::uint8_t>(16, 0));
    patch_u32(bytes, kOffPixelWidth, 0u);
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(Ktx2Edge, ZeroHeightReturnsCorrupt)
{
    auto bytes = build_minimal_ktx2(kBc7Unorm, 4, 4, std::vector<std::uint8_t>(16, 0));
    patch_u32(bytes, kOffPixelHeight, 0u);
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(Ktx2Edge, ThreeDimensionalTextureRejected)
{
    auto bytes = build_minimal_ktx2(kBc7Unorm, 4, 4, std::vector<std::uint8_t>(16, 0));
    patch_u32(bytes, kOffPixelDepth, 4u);  // pixelDepth > 1
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kUnsupportedFormat));
}

TEST(Ktx2Edge, ArrayTextureRejected)
{
    auto bytes = build_minimal_ktx2(kBc7Unorm, 4, 4, std::vector<std::uint8_t>(16, 0));
    patch_u32(bytes, kOffLayerCount, 8u);  // layerCount > 1
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kUnsupportedFormat));
}

TEST(Ktx2Edge, ZeroLevelCountReturnsCorrupt)
{
    auto bytes = build_minimal_ktx2(kBc7Unorm, 4, 4, std::vector<std::uint8_t>(16, 0));
    patch_u32(bytes, kOffLevelCount, 0u);
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(Ktx2Edge, LevelTableTruncatedReturnsCorrupt)
{
    // Claim 4 levels but provide a single-level file: the level index table
    // (4 * 24 = 96 bytes) won't fit after the 80-byte header.
    auto bytes = build_minimal_ktx2(kBc7Unorm, 4, 4, std::vector<std::uint8_t>(16, 0));
    patch_u32(bytes, kOffLevelCount, 4u);
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(Ktx2Edge, LevelByteRangeExceedingFileReturnsCorrupt)
{
    // Single level whose byte_length points past the end of the file.
    auto bytes = build_minimal_ktx2(kBc7Unorm, 4, 4, std::vector<std::uint8_t>(16, 0));
    // Level entry 0 starts at offset 80. byte_offset (u64) at 80, byte_length
    // (u64) at 88. Inflate byte_length way past file size.
    const std::size_t len_off = 80 + 8;
    for (std::size_t k = 0; k < 8; ++k)
        bytes[len_off + k] = 0xFF;
    auto r = cd::asset::ktx2::load_from_memory(bytes);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(Ktx2Edge, MagicTruncatedByOneByteReturnsCorruptNotOob)
{
    // A file shorter than the header but with a valid magic prefix must hit
    // the size guard, not read past the buffer.
    std::vector<std::uint8_t> magic_only { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                           0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    auto r = cd::asset::ktx2::load_from_memory(magic_only);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}
