// =============================================================================
// CHROMODYNAMIC — cd/asset_image tests
//
// Tests use a hand-built BMP file (the simplest format stb_image accepts)
// written to a temp dir, then decoded back. Strategy avoids checking in
// binary test assets and avoids depending on the host having PNG/JPG
// libraries — BMP is fully self-contained.
// =============================================================================
#include <cd/asset_image/Image.hpp>

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
        path = fs::temp_directory_path() /
               ("cd_img_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" + std::to_string(next_id()) +
                std::string { suffix });
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
    constexpr int w = 2;
    constexpr int h = 2;
    constexpr int row_bytes = 4 * ((24 * w + 31) / 32);  // BMP rows padded to 4 bytes.
    constexpr int pixel_data_size = row_bytes * h;
    constexpr int header_size = 14 + 40;
    constexpr int total_size = header_size + pixel_data_size;

    std::vector<std::uint8_t> buf(total_size, 0);

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
    write32(2, total_size);
    write32(6, 0);  // reserved
    write32(10, header_size);  // pixel data offset

    // DIB header (BITMAPINFOHEADER, 40 bytes)
    write32(14, 40);
    write32(18, w);
    write32(22, h);
    write16(26, 1);   // planes
    write16(28, 24);  // bpp
    write32(30, 0);   // BI_RGB no compression
    write32(34, pixel_data_size);

    // Pixel rows. BMP stores BGR per pixel, bottom-row first.
    // Bottom row (BMP row 0): blue, green
    // Top row    (BMP row 1): red,  white
    std::size_t off = header_size;
    // Row 0 (bottom in image, but written first in file).
    buf[off + 0] = 0xFF;
    buf[off + 1] = 0x00;
    buf[off + 2] = 0x00;  // blue
    buf[off + 3] = 0x00;
    buf[off + 4] = 0xFF;
    buf[off + 5] = 0x00;  // green
    // (padding to row_bytes already zeroed)

    off += row_bytes;
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
    auto r = cd::asset_image::load_image(f.path.string());
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

    cd::asset_image::LoadOptions opts {};
    opts.flip_vertical = true;
    auto r = cd::asset_image::load_image(f.path.string(), opts);
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

    auto disk = cd::asset_image::load_image(f.path.string());
    auto mem = cd::asset_image::load_image_from_memory(std::span<const std::uint8_t>(bytes));
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
    auto r = cd::asset_image::load_image("c:/definitely/does/not/exist.png");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_image::image_errors::Code::kFileNotFound));
}

TEST(AssetImage, EmptyPathReturnsInvalidArgument)
{
    auto r = cd::asset_image::load_image("");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_image::image_errors::Code::kInvalidArgument));
}

TEST(AssetImage, GarbageBufferReturnsDecodeFailed)
{
    const std::array<std::uint8_t, 16> junk { 'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e', 0, 0, 0, 0 };
    auto r = cd::asset_image::load_image_from_memory(std::span<const std::uint8_t>(junk.data(), junk.size()));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_image::image_errors::Code::kDecodeFailed));
}
