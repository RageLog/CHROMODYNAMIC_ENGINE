// =============================================================================
// CHROMODYNAMIC — cd/asset_image tests
//
// Tests use a hand-built BMP file (the simplest format stb_image accepts)
// written to a temp dir, then decoded back. Strategy avoids checking in
// binary test assets and avoids depending on the host having PNG/JPG
// libraries — BMP is fully self-contained.
// =============================================================================
#include <cd/asset/image/Image.hpp>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

namespace fs = std::filesystem;

[[nodiscard]] std::uint64_t next_id()
{
    static std::atomic<std::uint64_t> c { 0 };
    return c.fetch_add(1, std::memory_order_relaxed);
}

struct TempFile
{
    fs::path path;

    explicit TempFile(std::string_view suffix)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("cd_img_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                                            std::to_string(next_id()) + std::string { suffix });
    }

    ~TempFile()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;
};

/// Build a minimal 2×2 24-bit BMP — four solid pixels in a known palette.
/// Row order in BMP is bottom-to-top, so the decoded "pixel at (x=0, y=0)"
/// corresponds to the LAST row we write. Knowing this lets the tests
/// distinguish flip_vertical from no-flip behavior.
[[nodiscard]] std::vector<std::uint8_t> make_2x2_bmp()
{
    // 14-byte file header + 40-byte DIB header + 4×3 bytes pixels +
    // (4 - (6 mod 4)) mod 4 = 2 bytes padding per row × 2 rows = 4 bytes.
    constexpr int kW = 2;
    constexpr int kH = 2;
    constexpr int kRowBytes = 4 * ((24 * kW + 31) / 32);  // BMP rows padded to 4 bytes.
    constexpr int kPixelDataSize = kRowBytes * kH;
    constexpr int kHeaderSize = 14 + 40;
    constexpr int kTotalSize = kHeaderSize + kPixelDataSize;

    std::vector<std::uint8_t> buf(kTotalSize, 0);

    auto write16 = [&](std::size_t off, std::uint16_t v)
    {
        buf[off + 0] = static_cast<std::uint8_t>(v & 0xFF);
        buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    };
    auto write32 = [&](std::size_t off, std::uint32_t v)
    {
        buf[off + 0] = static_cast<std::uint8_t>(v & 0xFF);
        buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        buf[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        buf[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    };

    // BMP file header
    buf[0] = 'B';
    buf[1] = 'M';
    write32(2, kTotalSize);
    write32(6, 0);             // reserved
    write32(10, kHeaderSize);  // pixel data offset

    // DIB header (BITMAPINFOHEADER, 40 bytes)
    write32(14, 40);
    write32(18, kW);
    write32(22, kH);
    write16(26, 1);   // planes
    write16(28, 24);  // bpp
    write32(30, 0);   // BI_RGB no compression
    write32(34, kPixelDataSize);

    // Pixel rows. BMP stores BGR per pixel, bottom-row first.
    // Bottom row (BMP row 0): blue, green
    // Top row    (BMP row 1): red,  white
    std::size_t off = kHeaderSize;
    // Row 0 (bottom in image, but written first in file).
    buf[off + 0] = 0xFF;
    buf[off + 1] = 0x00;
    buf[off + 2] = 0x00;  // blue
    buf[off + 3] = 0x00;
    buf[off + 4] = 0xFF;
    buf[off + 5] = 0x00;  // green
    // (padding to row_bytes already zeroed)

    off += kRowBytes;
    // Row 1 (top in image)
    buf[off + 0] = 0x00;
    buf[off + 1] = 0x00;
    buf[off + 2] = 0xFF;  // red
    buf[off + 3] = 0xFF;
    buf[off + 4] = 0xFF;
    buf[off + 5] = 0xFF;  // white
    return buf;
}

void write_bytes(const fs::path& p, std::span<const std::uint8_t> bytes)
{
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST(AssetImage, Bmp2x2DecodesToCorrectRgbaPalette)
{
    TempFile f { ".bmp" };
    const auto bytes = make_2x2_bmp();
    write_bytes(f.path, bytes);

    // Default (no flip) — stb returns top-down so pixel (0,0) = TOP-LEFT,
    // which in our BMP is the red pixel.
    auto r = cd::asset::image::load_image(f.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->width, 2U);
    EXPECT_EQ(r->height, 2U);
    ASSERT_EQ(r->rgba.size(), 2U * 2U * 4U);

    // (0,0) = red (255,0,0,255)
    EXPECT_EQ(r->rgba[0], 0xFF);
    EXPECT_EQ(r->rgba[1], 0x00);
    EXPECT_EQ(r->rgba[2], 0x00);
    EXPECT_EQ(r->rgba[3], 0xFF);
    // (1,0) = white
    EXPECT_EQ(r->rgba[4], 0xFF);
    EXPECT_EQ(r->rgba[5], 0xFF);
    EXPECT_EQ(r->rgba[6], 0xFF);
    // (0,1) = blue
    EXPECT_EQ(r->rgba[8], 0x00);
    EXPECT_EQ(r->rgba[9], 0x00);
    EXPECT_EQ(r->rgba[10], 0xFF);
    // (1,1) = green
    EXPECT_EQ(r->rgba[12], 0x00);
    EXPECT_EQ(r->rgba[13], 0xFF);
    EXPECT_EQ(r->rgba[14], 0x00);

    // BMP has no alpha channel; has_alpha should be false (stb reports comp=3).
    EXPECT_FALSE(r->has_alpha);
}

TEST(AssetImage, FlipVerticalSwapsRows)
{
    TempFile f { ".bmp" };
    write_bytes(f.path, make_2x2_bmp());

    cd::asset::image::LoadOptions opts {};
    opts.flip_vertical = true;
    auto r = cd::asset::image::load_image(f.path.string(), opts);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    // Flipped: (0,0) is now bottom-left of the image = blue.
    EXPECT_EQ(r->rgba[0], 0x00);
    EXPECT_EQ(r->rgba[1], 0x00);
    EXPECT_EQ(r->rgba[2], 0xFF);
}

TEST(AssetImage, LoadFromMemoryMatchesDiskLoad)
{
    TempFile f { ".bmp" };
    const auto bytes = make_2x2_bmp();
    write_bytes(f.path, bytes);

    auto disk = cd::asset::image::load_image(f.path.string());
    auto mem = cd::asset::image::load_image_from_memory(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(disk.has_value());
    ASSERT_TRUE(mem.has_value());

    EXPECT_EQ(disk->width, mem->width);
    EXPECT_EQ(disk->height, mem->height);
    ASSERT_EQ(disk->rgba.size(), mem->rgba.size());
    for (std::size_t i = 0; i < disk->rgba.size(); ++i)
        EXPECT_EQ(disk->rgba[i], mem->rgba[i]) << "byte " << i;
}

TEST(AssetImage, MissingFileReturnsFileNotFound)
{
    auto r = cd::asset::image::load_image("c:/definitely/does/not/exist.png");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::image::image_errors::Code::kFileNotFound));
}

TEST(AssetImage, EmptyPathReturnsInvalidArgument)
{
    auto r = cd::asset::image::load_image("");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::image::image_errors::Code::kInvalidArgument));
}

TEST(AssetImage, GarbageBufferReturnsDecodeFailed)
{
    const std::array<std::uint8_t, 16> junk { 'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e', 0, 0, 0, 0 };
    auto r = cd::asset::image::load_image_from_memory(std::span<const std::uint8_t>(junk.data(), junk.size()));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::image::image_errors::Code::kDecodeFailed));
}

TEST(AssetImage, WritePngRoundTripPreservesRgbaPixels)
{
    // Build a tiny 2x2 RGBA8 test image with distinct corners.
    const std::array<std::uint8_t, 16> rgba {
        255, 0,   0,   255,   // top-left:     red
        0,   255, 0,   255,   // top-right:    green
        0,   0,   255, 255,   // bottom-left:  blue
        255, 255, 255, 128,   // bottom-right: white at 50% alpha
    };
    const auto tmp = std::filesystem::temp_directory_path() / "cd_test_write_png_rgba_2x2.png";
    auto w = cd::asset::image::write_png_rgba(tmp.string(), rgba.data(), 2, 2);
    ASSERT_TRUE(w.has_value()) << "write_png_rgba failed: " << w.error().message;

    auto r = cd::asset::image::load_image(tmp.string());
    ASSERT_TRUE(r.has_value()) << "load_image failed after write_png_rgba: " << r.error().message;
    EXPECT_EQ(r->width, 2u);
    EXPECT_EQ(r->height, 2u);
    ASSERT_EQ(r->rgba.size(), rgba.size());
    for (std::size_t i = 0; i < rgba.size(); ++i)
        EXPECT_EQ(r->rgba[i], rgba[i]) << "byte mismatch at index " << i;

    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(AssetImage, WritePngNullPointerReturnsInvalidArgument)
{
    auto r = cd::asset::image::write_png_rgba("dummy.png", nullptr, 4, 4);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::image::image_errors::Code::kInvalidArgument));
}

TEST(AssetImage, WritePngZeroSizeReturnsInvalidArgument)
{
    const std::array<std::uint8_t, 4> px { 1, 2, 3, 4 };
    auto r = cd::asset::image::write_png_rgba("dummy.png", px.data(), 0, 1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::image::image_errors::Code::kInvalidArgument));
}

TEST(AssetImage, GenerateMipsFullChainOn256Square)
{
    cd::asset::image::Image src;
    src.width = 256;
    src.height = 256;
    src.has_alpha = true;
    src.rgba.assign(static_cast<std::size_t>(256U) * 256U * 4U, 128U);  // solid gray
    auto r = cd::asset::image::generate_mips(src);
    ASSERT_TRUE(r.has_value());
    // 256 → 128 → 64 → 32 → 16 → 8 → 4 → 2 → 1 = 9 levels.
    ASSERT_EQ(r->size(), 9U);
    EXPECT_EQ((*r)[0].width, 256U);
    EXPECT_EQ((*r)[0].height, 256U);
    EXPECT_EQ(r->back().width, 1U);
    EXPECT_EQ(r->back().height, 1U);
    // Box filter of solid gray should still be gray (modulo rounding).
    EXPECT_NEAR(static_cast<int>(r->back().rgba[0]), 128, 1);
}

TEST(AssetImage, GenerateMipsCappedLevels)
{
    cd::asset::image::Image src;
    src.width = 32;
    src.height = 32;
    src.rgba.assign(static_cast<std::size_t>(32U) * 32U * 4U, 200U);
    auto r = cd::asset::image::generate_mips(src, 3);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 3U);
    EXPECT_EQ((*r)[0].width, 32U);
    EXPECT_EQ((*r)[1].width, 16U);
    EXPECT_EQ((*r)[2].width, 8U);
}

TEST(AssetImage, GenerateMipsNonSquareWidthOrHeightOf1)
{
    // 4x1 → 2x1 → 1x1 (3 mips).
    cd::asset::image::Image src;
    src.width = 4;
    src.height = 1;
    src.rgba.assign(static_cast<std::size_t>(4U) * 1U * 4U, 99U);
    auto r = cd::asset::image::generate_mips(src);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 3U);
    EXPECT_EQ((*r)[1].width, 2U);
    EXPECT_EQ((*r)[1].height, 1U);
    EXPECT_EQ((*r)[2].width, 1U);
    EXPECT_EQ((*r)[2].height, 1U);
}

TEST(AssetImage, GenerateMipsZeroDimensionRejected)
{
    cd::asset::image::Image src;
    src.width = 0;
    src.height = 16;
    auto r = cd::asset::image::generate_mips(src);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::image::image_errors::Code::kInvalidArgument));
}

// ----- AssetLoader adapter -----

#include <cd/asset/image/AssetLoader.hpp>

TEST(ImageAssetLoader, AdapterDecodesBmp)
{
    const auto u8 = make_2x2_bmp();
    std::vector<std::byte> bytes(u8.size());
    std::memcpy(bytes.data(), u8.data(), u8.size());
    cd::asset::image::ImageAssetLoader loader;
    EXPECT_EQ(loader.tag(), "image");
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "t.bmp");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* ia = dynamic_cast<cd::asset::image::ImageAsset*>(r->get());
    ASSERT_NE(ia, nullptr);
    EXPECT_EQ(ia->image().width, 2u);
    EXPECT_EQ(ia->image().height, 2u);
}

// =============================================================================
// Robustness / edge / negative coverage (≥80→100 marathon, ADD-ONLY).
// stb-backed decode of VALID inputs is unchanged; these probe the rejection
// paths and the engine-side mip generator.
// =============================================================================

namespace
{
using ImgCode = cd::asset::image::image_errors::Code;
}  // namespace

TEST(AssetImageEdge, EmptyMemoryBufferReturnsInvalidArgument)
{
    auto r = cd::asset::image::load_image_from_memory(std::span<const std::uint8_t> {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(ImgCode::kInvalidArgument));
}

TEST(AssetImageEdge, TruncatedBmpHeaderReturnsDecodeFailed)
{
    // Valid "BM" magic but the file is cut off mid-header — stb must reject it.
    auto bmp = make_2x2_bmp();
    bmp.resize(10);  // keep "BM" + a few bytes, drop the rest
    auto r = cd::asset::image::load_image_from_memory(std::span<const std::uint8_t>(bmp.data(), bmp.size()));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(ImgCode::kDecodeFailed));
}

TEST(AssetImageEdge, HeaderOnlyBmpDecodedLeniently)
{
    // Header claims 2x2 24bpp but the 54-byte buffer carries no pixel rows.
    // stb respects the buffer length (no OOB read) and LENIENTLY produces a
    // 2x2 image rather than failing. Locked as a regression guard — a stricter
    // pre-validation would instead return kDecodeFailed here.
    auto bmp = make_2x2_bmp();
    bmp.resize(54);  // exactly the 54-byte header, no pixel rows
    auto r = cd::asset::image::load_image_from_memory(std::span<const std::uint8_t>(bmp.data(), bmp.size()));
    EXPECT_TRUE(r.has_value());
}

TEST(AssetImageEdge, SingleByteBufferReturnsDecodeFailed)
{
    const std::array<std::uint8_t, 1> one { 0x42 };
    auto r = cd::asset::image::load_image_from_memory(std::span<const std::uint8_t>(one.data(), one.size()));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(ImgCode::kDecodeFailed));
}

TEST(AssetImageEdge, GenerateMipsRgbaTooSmallRejected)
{
    cd::asset::image::Image src;
    src.width = 16;
    src.height = 16;
    src.rgba.assign(8, 0u);  // far smaller than 16*16*4
    auto r = cd::asset::image::generate_mips(src);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(ImgCode::kInvalidArgument));
}

TEST(AssetImageEdge, GenerateMipsLevelsOneReturnsSourceOnly)
{
    cd::asset::image::Image src;
    src.width = 64;
    src.height = 64;
    src.rgba.assign(static_cast<std::size_t>(64U) * 64U * 4U, 50U);
    auto r = cd::asset::image::generate_mips(src, 1);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->size(), 1U);
    EXPECT_EQ((*r)[0].width, 64U);
    EXPECT_EQ((*r)[0].height, 64U);
}

TEST(AssetImageEdge, GenerateMipsZeroHeightRejected)
{
    cd::asset::image::Image src;
    src.width = 16;
    src.height = 0;
    auto r = cd::asset::image::generate_mips(src);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(ImgCode::kInvalidArgument));
}

TEST(AssetImageEdge, OnePixelImageGeneratesSingleMip)
{
    cd::asset::image::Image src;
    src.width = 1;
    src.height = 1;
    src.rgba.assign(4U, 222U);
    auto r = cd::asset::image::generate_mips(src);
    ASSERT_TRUE(r.has_value());
    // A 1x1 source has no smaller mip — the chain is just the source.
    ASSERT_EQ(r->size(), 1U);
    EXPECT_EQ((*r)[0].width, 1U);
}
