// =============================================================================
// CHROMODYNAMIC — cd::ui::font tests
//
// Phase 1.1. The tests that don't need a TTF asset bundled in-repo run
// always; the rasterization smoke test optionally loads a system TTF
// (Arial on Windows / DejaVuSans on Linux fallback) and skips when the
// font isn't present. This keeps the test suite green on minimal CI
// containers while still validating the rasterizer on a real host.
// =============================================================================
#include <cd/ui/font/Font.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{

[[nodiscard]] std::vector<std::uint8_t> read_file_bytes(const fs::path& p)
{
    std::vector<std::uint8_t> out;
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return out;
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    out.resize(sz);
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
    return out;
}

[[nodiscard]] std::vector<std::uint8_t> find_system_font()
{
    static const std::array<const char*, 4> kCandidates {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (const char* p : kCandidates)
    {
        auto bytes = read_file_bytes(fs::path { p });
        if (!bytes.empty()) return bytes;
    }
    return {};
}

}  // namespace

TEST(Font, NewlyConstructedIsNotLoaded)
{
    cd::ui::font::Font f;
    EXPECT_FALSE(f.is_loaded());
    EXPECT_FALSE(f.glyph_uv(0x41).has_value());  // 'A'
    EXPECT_FLOAT_EQ(f.pixel_size(), 0.0F);
}

TEST(Font, EmptyDataRejected)
{
    cd::ui::font::Font f;
    EXPECT_FALSE(f.load_ttf_in_memory({}));
    EXPECT_FALSE(f.is_loaded());
}

TEST(Font, GarbageDataRejected)
{
    cd::ui::font::Font f;
    std::vector<std::uint8_t> junk { 0xDEU, 0xADU, 0xBEU, 0xEFU };
    junk.resize(32U, 0U);
    EXPECT_FALSE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(junk.data(), junk.size())));
    EXPECT_FALSE(f.is_loaded());
}

TEST(Font, RasterizeAsciiOnSystemFont)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }

    cd::ui::font::Font f;
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));

    // Rasterize printable ASCII at 16 px.
    ASSERT_TRUE(f.rasterize_range(0x0020U, 0x007EU, 16.0F, 1024U));

    // Atlas allocated.
    EXPECT_GT(f.atlas().width,  0U);
    EXPECT_GT(f.atlas().height, 0U);
    EXPECT_FALSE(f.atlas().pixels.empty());
    EXPECT_GT(f.line_height(), 0.0F);

    // 'A' glyph found.
    auto g_a = f.glyph_uv(0x41U);
    ASSERT_TRUE(g_a.has_value());
    EXPECT_GT(g_a->width,   0.0F);
    EXPECT_GT(g_a->height,  0.0F);
    EXPECT_GT(g_a->advance, 0.0F);

    // Whitespace glyph (space) has zero size but non-zero advance.
    auto g_sp = f.glyph_uv(0x20U);
    ASSERT_TRUE(g_sp.has_value());
    EXPECT_GE(g_sp->advance, 0.0F);
}

TEST(Font, KerningReturnsZeroForUnloadedFont)
{
    cd::ui::font::Font f;
    EXPECT_FLOAT_EQ(f.kerning(0x41U, 0x56U), 0.0F);
}
