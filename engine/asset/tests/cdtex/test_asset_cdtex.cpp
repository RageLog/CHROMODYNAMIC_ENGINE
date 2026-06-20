// =============================================================================
// CHROMODYNAMIC — cd::asset_cdtex tests
//
// Tests write a synthetic .cdtex file (matches the cook_texture layout
// byte-for-byte) and assert the loader returns the expected fields.
// No actual BC7 encoder is needed — we just want to verify the binary
// framing.
// =============================================================================
#include <cd/asset/cdtex/CdTex.hpp>
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
    return fs::temp_directory_path() / ("cd_cdtex_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                                        std::to_string(seq.fetch_add(1)) + ".cdtex");
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

void write_u32(std::ofstream& f, std::uint32_t v)
{
    const std::uint8_t b[4] { static_cast<std::uint8_t>(v),
                              static_cast<std::uint8_t>(v >> 8),
                              static_cast<std::uint8_t>(v >> 16),
                              static_cast<std::uint8_t>(v >> 24) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

void write_u16(std::ofstream& f, std::uint16_t v)
{
    const std::uint8_t b[2] { static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8) };
    f.write(reinterpret_cast<const char*>(b), 2);
}

void write_valid_cdtex(const fs::path& p, std::uint32_t w, std::uint32_t h, std::uint8_t fill = 0xAA)
{
    const auto bw = static_cast<std::uint16_t>((w + 3) / 4);
    const auto bh = static_cast<std::uint16_t>((h + 3) / 4);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write("CDBC7", 5);
    const std::uint8_t version = 1;
    f.write(reinterpret_cast<const char*>(&version), 1);
    write_u32(f, w);
    write_u32(f, h);
    write_u16(f, bw);
    write_u16(f, bh);
    // 16 bytes per BC7 block × bw × bh, filled with the marker so the test
    // can verify the loader didn't truncate or shift bytes.
    const std::size_t payload = static_cast<std::size_t>(bw) * bh * 16U;
    std::vector<std::uint8_t> blocks(payload, fill);
    f.write(reinterpret_cast<const char*>(blocks.data()), static_cast<std::streamsize>(payload));
}

}  // namespace

TEST(CdTex, RoundtripsMinimal4x4)
{
    PathGuard g { tmp_path() };
    write_valid_cdtex(g.path, 4, 4, 0xCC);
    auto r = cd::asset::cdtex::load(g.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->width, 4U);
    EXPECT_EQ(r->height, 4U);
    EXPECT_EQ(r->block_w, 1U);
    EXPECT_EQ(r->block_h, 1U);
    ASSERT_EQ(r->blocks.size(), 16U);
    for (auto b : r->blocks)
        EXPECT_EQ(b, 0xCC);
}

TEST(CdTex, RoundtripsLargerWithBlockRounding)
{
    PathGuard g { tmp_path() };
    // 257×130 → blocks 65×33 (ceil-div by 4)
    write_valid_cdtex(g.path, 257, 130, 0x55);
    auto r = cd::asset::cdtex::load(g.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->width, 257U);
    EXPECT_EQ(r->height, 130U);
    EXPECT_EQ(r->block_w, 65U);
    EXPECT_EQ(r->block_h, 33U);
    EXPECT_EQ(r->blocks.size(), 65U * 33U * 16U);
}

TEST(CdTex, MissingFileReturnsFileNotFound)
{
    auto r = cd::asset::cdtex::load("c:/no/such/file.cdtex");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::cdtex::cdtex_errors::Code::kFileNotFound));
}

TEST(CdTex, EmptyPathReturnsInvalidArgument)
{
    auto r = cd::asset::cdtex::load("");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::cdtex::cdtex_errors::Code::kInvalidArgument));
}

TEST(CdTex, BadMagicReturnsMagicMismatch)
{
    PathGuard g { tmp_path() };
    {
        std::ofstream f(g.path, std::ios::binary);
        const std::vector<std::uint8_t> junk(32, 0);
        f.write(reinterpret_cast<const char*>(junk.data()), 32);
    }
    auto r = cd::asset::cdtex::load(g.path.string());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::cdtex::cdtex_errors::Code::kMagicMismatch));
}

TEST(CdTex, BadVersionReturnsVersionMismatch)
{
    PathGuard g { tmp_path() };
    {
        std::ofstream f(g.path, std::ios::binary);
        f.write("CDBC7", 5);
        const std::uint8_t bad_version = 99;
        f.write(reinterpret_cast<const char*>(&bad_version), 1);
        // Pad to header size + small payload
        const std::vector<std::uint8_t> rest(32, 0);
        f.write(reinterpret_cast<const char*>(rest.data()), static_cast<std::streamsize>(rest.size()));
    }
    auto r = cd::asset::cdtex::load(g.path.string());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::cdtex::cdtex_errors::Code::kVersionMismatch));
}

TEST(CdTex, TruncatedPayloadReturnsCorrupt)
{
    PathGuard g { tmp_path() };
    // Claim 1000×1000 blocks but write only the header.
    {
        std::ofstream f(g.path, std::ios::binary);
        f.write("CDBC7", 5);
        const std::uint8_t ver = 1;
        f.write(reinterpret_cast<const char*>(&ver), 1);
        write_u32(f, 4000U);  // width
        write_u32(f, 4000U);  // height
        write_u16(f, 1000U);  // block_w
        write_u16(f, 1000U);  // block_h
        // ZERO payload bytes — loader must reject.
    }
    auto r = cd::asset::cdtex::load(g.path.string());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::cdtex::cdtex_errors::Code::kCorrupt));
}

TEST(CdTex, V2MipChainRoundtrips)
{
    PathGuard g { tmp_path() };
    {
        std::ofstream f(g.path, std::ios::binary);
        f.write("CDBC7", 5);
        const std::uint8_t ver = 2;
        f.write(reinterpret_cast<const char*>(&ver), 1);
        write_u32(f, 8U);                  // width
        write_u32(f, 8U);                  // height
        write_u16(f, 2U);                  // block_w (mip 0)
        write_u16(f, 2U);                  // block_h (mip 0)
        const std::uint8_t mip_count = 4;  // 8 → 4 → 2 → 1
        f.write(reinterpret_cast<const char*>(&mip_count), 1);
        // Mip 0: 2x2 blocks = 64 bytes, fill 0x01
        std::vector<std::uint8_t> m0(64, 0x01);
        f.write(reinterpret_cast<const char*>(m0.data()), 64);
        // Mip 1: 4x4 pixels → 1x1 block = 16 bytes, fill 0x02
        std::vector<std::uint8_t> m1(16, 0x02);
        f.write(reinterpret_cast<const char*>(m1.data()), 16);
        // Mip 2: 2x2 → 1x1 block = 16 bytes, fill 0x03
        std::vector<std::uint8_t> m2(16, 0x03);
        f.write(reinterpret_cast<const char*>(m2.data()), 16);
        // Mip 3: 1x1 → 1x1 block = 16 bytes, fill 0x04
        std::vector<std::uint8_t> m3(16, 0x04);
        f.write(reinterpret_cast<const char*>(m3.data()), 16);
    }
    auto r = cd::asset::cdtex::load(g.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->width, 8U);
    EXPECT_EQ(r->height, 8U);
    ASSERT_EQ(r->mips.size(), 4U);
    EXPECT_EQ(r->mips[0].width, 8U);
    EXPECT_EQ(r->mips[1].width, 4U);
    EXPECT_EQ(r->mips[2].width, 2U);
    EXPECT_EQ(r->mips[3].width, 1U);
    EXPECT_EQ(r->mips[0].blocks[0], 0x01);
    EXPECT_EQ(r->mips[1].blocks[0], 0x02);
    EXPECT_EQ(r->mips[2].blocks[0], 0x03);
    EXPECT_EQ(r->mips[3].blocks[0], 0x04);
    EXPECT_EQ(r->blocks.size(), 64U);
    EXPECT_EQ(r->blocks[0], 0x01);
}

TEST(CdTex, V1FileStillLoadsAsSingleMipChain)
{
    PathGuard g { tmp_path() };
    write_valid_cdtex(g.path, 16, 16, 0x77);
    auto r = cd::asset::cdtex::load(g.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->mips.size(), 1U);
    EXPECT_EQ(r->mips[0].width, 16U);
    EXPECT_EQ(r->mips[0].blocks[0], 0x77);
}

// ----- AssetLoader adapter -----

#include <cd/asset/cdtex/AssetLoader.hpp>

#include <cstring>
#include <span>

namespace
{

std::vector<std::byte> build_minimal_cdtex_bytes(std::uint32_t w, std::uint32_t h, std::uint8_t fill = 0xAA)
{
    const auto bw = static_cast<std::uint16_t>((w + 3) / 4);
    const auto bh = static_cast<std::uint16_t>((h + 3) / 4);
    std::vector<std::byte> bytes;
    auto put = [&](const void* p, std::size_t n)
    {
        const auto* b = static_cast<const std::byte*>(p);
        bytes.insert(bytes.end(), b, b + n);
    };
    put("CDBC7", 5);
    const std::uint8_t version = 1;
    put(&version, 1);
    put(&w, 4);
    put(&h, 4);
    put(&bw, 2);
    put(&bh, 2);
    const std::size_t payload = static_cast<std::size_t>(bw) * bh * 16U;
    std::vector<std::uint8_t> blocks(payload, fill);
    put(blocks.data(), payload);
    return bytes;
}

}  // namespace

TEST(CdTexAssetLoader, AdapterDecodesValid)
{
    const auto bytes = build_minimal_cdtex_bytes(8, 8, 0x42);
    cd::asset::cdtex::CdTexAssetLoader loader;
    EXPECT_EQ(loader.tag(), "cdtex");
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "t.cdtex");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* a = dynamic_cast<cd::asset::cdtex::CdTexAsset*>(r->get());
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->tex().width, 8u);
    EXPECT_EQ(a->tex().height, 8u);
    EXPECT_EQ(a->tex().mips.size(), 1u);
    EXPECT_EQ(a->tex().mips[0].blocks[0], 0x42);
}

TEST(CdTexAssetLoader, AdapterRejectsBadMagic)
{
    auto bytes = build_minimal_cdtex_bytes(4, 4);
    bytes[0] = std::byte { 'X' };
    cd::asset::cdtex::CdTexAssetLoader loader;
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "x.cdtex");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::cdtex::cdtex_errors::Code::kMagicMismatch));
}

// =============================================================================
// Robustness / edge / negative coverage (≥80→100 marathon, ADD-ONLY).
// decode() directly so corrupt headers are exercised without disk round-trips.
// =============================================================================

namespace
{
using Code = cd::asset::cdtex::cdtex_errors::Code;

// Build an 18-byte v1 header (+ optional payload). block_w/block_h are taken
// verbatim (callers fuzz them); version defaults to 1.
std::vector<std::uint8_t> make_cdtex_v1(std::uint32_t w,
                                        std::uint32_t h,
                                        std::uint16_t block_w,
                                        std::uint16_t block_h,
                                        std::size_t payload_bytes,
                                        std::uint8_t version = 1)
{
    std::vector<std::uint8_t> b;
    auto w32 = [&](std::uint32_t v)
    {
        b.push_back(static_cast<std::uint8_t>(v));
        b.push_back(static_cast<std::uint8_t>(v >> 8));
        b.push_back(static_cast<std::uint8_t>(v >> 16));
        b.push_back(static_cast<std::uint8_t>(v >> 24));
    };
    auto w16 = [&](std::uint16_t v)
    {
        b.push_back(static_cast<std::uint8_t>(v));
        b.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    for (char c : std::string_view { "CDBC7", 5 })
        b.push_back(static_cast<std::uint8_t>(c));
    b.push_back(version);
    w32(w);
    w32(h);
    w16(block_w);
    w16(block_h);
    b.resize(18 + payload_bytes, 0u);
    return b;
}
}  // namespace

TEST(CdTexEdge, NullBufferReturnsInvalidArgument)
{
    auto r = cd::asset::cdtex::decode(nullptr, 64);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kInvalidArgument));
}

TEST(CdTexEdge, HeaderTooSmallReturnsCorrupt)
{
    std::vector<std::uint8_t> tiny(10, 0u);
    auto r = cd::asset::cdtex::decode(tiny.data(), tiny.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(CdTexEdge, ZeroBlockGridReturnsCorrupt)
{
    auto b = make_cdtex_v1(4, 4, 0 /*block_w==0*/, 1, 16);
    auto r = cd::asset::cdtex::decode(b.data(), b.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(CdTexEdge, V1TruncatedPayloadReturnsCorrupt)
{
    // 64x64 → 16x16 blocks → 256 blocks * 16 = 4096 bytes promised, none given.
    auto b = make_cdtex_v1(64, 64, 16, 16, /*payload_bytes=*/0);
    auto r = cd::asset::cdtex::decode(b.data(), b.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(CdTexEdge, V2MipCountByteTruncatedReturnsCorrupt)
{
    // version 2 but no mip_count byte after the 18-byte header.
    auto b = make_cdtex_v1(8, 8, 2, 2, /*payload_bytes=*/0, /*version=*/2);
    auto r = cd::asset::cdtex::decode(b.data(), b.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(CdTexEdge, V2MipCountZeroReturnsCorrupt)
{
    auto b = make_cdtex_v1(8, 8, 2, 2, /*payload_bytes=*/1, /*version=*/2);
    b[18] = 0u;  // mip_count == 0
    auto r = cd::asset::cdtex::decode(b.data(), b.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(CdTexEdge, V2TruncatedMipPayloadReturnsCorrupt)
{
    // mip_count=4 for an 8x8 texture but no actual mip bytes follow.
    auto b = make_cdtex_v1(8, 8, 2, 2, /*payload_bytes=*/1, /*version=*/2);
    b[18] = 4u;  // mip_count
    auto r = cd::asset::cdtex::decode(b.data(), b.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kCorrupt));
}

TEST(CdTexEdge, BadVersionInDecodeReturnsVersionMismatch)
{
    auto b = make_cdtex_v1(4, 4, 1, 1, 16, /*version=*/3);  // only 1 and 2 valid
    auto r = cd::asset::cdtex::decode(b.data(), b.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(Code::kVersionMismatch));
}
