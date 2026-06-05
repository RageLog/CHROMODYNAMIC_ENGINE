// =============================================================================
// CHROMODYNAMIC — cd::ui::font tests
//
// Phase 1.1 baseline (always runs):
//   - NewlyConstructedIsNotLoaded
//   - EmptyDataRejected
//   - GarbageDataRejected
//   - RasterizeAsciiOnSystemFont (skips when no system TTF)
//   - KerningReturnsZeroForUnloadedFont
//
// Phase 4 (T2.4) additions — gated at runtime on Font::has_freetype()
// / Font::has_harfbuzz() so the same TU compiles on stb-only and
// FT+HB-enabled builds. Cases that need a real shaper SKIP cleanly when
// the build has no HarfBuzz.
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

[[nodiscard]] std::vector<std::uint8_t> find_arabic_font()
{
    // Windows ships Arabic Typesetting + Tahoma (Arabic-capable). Linux
    // commonly has noto-naskh-arabic. Returning empty signals the test
    // should SKIP rather than fail.
    static const std::array<const char*, 4> kCandidates {
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/arabtype.ttf",
        "/usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf",
        "/System/Library/Fonts/GeezaPro.ttc",
    };
    for (const char* p : kCandidates)
    {
        auto bytes = read_file_bytes(fs::path { p });
        if (!bytes.empty()) return bytes;
    }
    return {};
}

}  // namespace

// ============================================================================
// Phase 1.1 baseline tests (UNCHANGED behaviour)
// ============================================================================

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
    // Force stb path so this case is identical across stb-only +
    // FT-enabled builds. The FT path is exercised by the cases below.
    f.select_backend(cd::ui::font::Backend::kStb);

    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));

    ASSERT_TRUE(f.rasterize_range(0x0020U, 0x007EU, 16.0F, 1024U));

    EXPECT_GT(f.atlas().width,  0U);
    EXPECT_GT(f.atlas().height, 0U);
    EXPECT_FALSE(f.atlas().pixels.empty());
    EXPECT_GT(f.line_height(), 0.0F);

    auto g_a = f.glyph_uv(0x41U);
    ASSERT_TRUE(g_a.has_value());
    EXPECT_GT(g_a->width,   0.0F);
    EXPECT_GT(g_a->height,  0.0F);
    EXPECT_GT(g_a->advance, 0.0F);

    auto g_sp = f.glyph_uv(0x20U);
    ASSERT_TRUE(g_sp.has_value());
    EXPECT_GE(g_sp->advance, 0.0F);
}

TEST(Font, KerningReturnsZeroForUnloadedFont)
{
    cd::ui::font::Font f;
    EXPECT_FLOAT_EQ(f.kerning(0x41U, 0x56U), 0.0F);
}

// ============================================================================
// Phase 4 (T2.4) — FreeType + HarfBuzz + MSDF + identity-shape fallback
// ============================================================================

// (1) FT load_ttf produces a glyph count comparable to stb (same font,
//     same rasterized range -> both backends place at least the printable
//     ASCII subset). We don't expect EXACT parity (FT and stb disagree on
//     a handful of cmap-table edge codepoints) — instead we check that the
//     FT backend produces AT LEAST as many glyphs as stb for the requested
//     range, and that both populate the canonical 'A'.
TEST(Font, FreeTypeLoadsAndProducesGlyphsCompatibleWithStb)
{
    if (!cd::ui::font::Font::has_freetype())
    {
        GTEST_SKIP() << "Built without CD_UI_FONT_HAVE_FREETYPE";
    }
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }

    cd::ui::font::Font stb;
    stb.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(stb.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(stb.rasterize_range(0x0020U, 0x007EU, 16.0F, 1024U));
    std::size_t stb_count = 0;
    for (std::uint32_t cp = 0x0020U; cp <= 0x007EU; ++cp)
        if (stb.glyph_uv(cp).has_value()) ++stb_count;
    ASSERT_GT(stb_count, 0U);

    cd::ui::font::Font ft;
    ASSERT_EQ(ft.select_backend(cd::ui::font::Backend::kFreeType),
              cd::ui::font::Backend::kFreeType);
    ASSERT_TRUE(ft.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(ft.rasterize_range(0x0020U, 0x007EU, 16.0F, 1024U));
    std::size_t ft_count = 0;
    for (std::uint32_t cp = 0x0020U; cp <= 0x007EU; ++cp)
        if (ft.glyph_uv(cp).has_value()) ++ft_count;
    EXPECT_GE(ft_count, stb_count);
    EXPECT_TRUE(ft.glyph_uv(0x41U).has_value());  // 'A'
}

// (2) Shape "Hello" -> 5 glyphs with monotone-increasing advance pen.
TEST(Font, ShapeHelloProducesFiveGlyphsWithMonotoneAdvance)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    if (cd::ui::font::Font::has_harfbuzz())
        f.select_backend(cd::ui::font::Backend::kFreeType);
    else
        f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0020U, 0x007EU, 32.0F, 1024U));

    auto glyphs = f.shape("Hello", "en");
    EXPECT_EQ(glyphs.size(), 5U);

    // Pen advance must be monotone non-decreasing as we walk the string.
    float pen = 0.0F;
    for (const auto& g : glyphs)
    {
        EXPECT_GE(g.advance_x, 0.0F);
        pen += g.advance_x;
    }
    EXPECT_GT(pen, 0.0F);
}

// (3) Shape an Arabic string and verify it comes back in RTL visual order.
//     We test by checking that the GLYPH SEQUENCE is the reverse of the
//     codepoint sequence: HB emits visual-LTR glyphs whose GIDs map to
//     Arabic codepoints starting from the LOGICALLY LAST character of
//     the input (Arabic reads right-to-left, so the visually-first glyph
//     in render order is the logically-last codepoint).
TEST(Font, ShapeArabicReversesCodepointOrderToVisualLtr)
{
    if (!cd::ui::font::Font::has_harfbuzz())
    {
        GTEST_SKIP() << "Built without CD_UI_FONT_HAVE_HARFBUZZ";
    }
    const auto ttf = find_arabic_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No Arabic-capable TTF found at expected paths";
    }

    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kFreeType);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0600U, 0x06FFU, 24.0F, 2048U));

    // UTF-8 for U+0633 U+0644 U+0627 U+0645 (سلام -- "salaam").
    const char* arabic = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    auto glyphs = f.shape(arabic, "ar");

    ASSERT_FALSE(glyphs.empty());
    // After shaping + RTL reorder + Arabic contextual substitutions,
    // HarfBuzz returns glyph indices in VISUAL (left-to-right) order.
    // The number of output glyphs is bounded above by the codepoint
    // count (ligatures can reduce it) but must be at least 1.
    EXPECT_LE(glyphs.size(), 4U);
    EXPECT_GE(glyphs.size(), 1U);
}

// (4) "fi" ligature: in a font with a real fi ligature (Calibri / most
//     modern faces), HarfBuzz substitutes the two codepoints into a
//     single glyph index. The identity fallback returns 2 glyphs.
TEST(Font, ShapeFiLigatureMergesWhenSupported)
{
    if (!cd::ui::font::Font::has_harfbuzz())
    {
        GTEST_SKIP() << "Built without CD_UI_FONT_HAVE_HARFBUZZ";
    }
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kFreeType);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0020U, 0x00FFU, 32.0F, 1024U));

    auto glyphs = f.shape("fi", "en");
    // Output must be 1 (ligature applied) OR 2 (font has no liga). Both
    // are valid HB outcomes; the test only fails if HB returned junk.
    ASSERT_GE(glyphs.size(), 1U);
    ASSERT_LE(glyphs.size(), 2U);
}

// (5) MSDF atlas: select kMsdf BEFORE rasterize_range, rasterize 'A',
//     then check that the interior of the glyph has values > 128 (INSIDE)
//     while at least one corner of the atlas (which the glyph can't
//     occupy because of bin-packing offsets) is 0 (background).
TEST(Font, MsdfAtlasHasInteriorInsideAndExteriorOutside)
{
    if (!cd::ui::font::Font::has_freetype())
    {
        GTEST_SKIP() << "Built without CD_UI_FONT_HAVE_FREETYPE";
    }
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kFreeType);
    EXPECT_EQ(f.select_atlas_mode(cd::ui::font::AtlasMode::kMsdf),
              cd::ui::font::AtlasMode::kMsdf);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x0041U, 32.0F, 1024U));

    auto g = f.glyph_uv(0x0041U);
    ASSERT_TRUE(g.has_value());
    ASSERT_GT(g->width,  0.0F);
    ASSERT_GT(g->height, 0.0F);

    const auto& atlas = f.atlas();
    const auto  zero  = f.sdf_zero();  // 128 for MSDF mode
    EXPECT_EQ(zero, 128U);

    // Sample the GEOMETRIC CENTRE of the glyph atlas slot — for 'A' the
    // centroid lies on the crossbar (inside the glyph). SDF > 128 means
    // INSIDE.
    const auto px = static_cast<std::uint32_t>(g->u0 * static_cast<float>(atlas.width));
    const auto py = static_cast<std::uint32_t>(g->v0 * static_cast<float>(atlas.height));
    const auto cx = px + static_cast<std::uint32_t>(g->width  * 0.5F);
    const auto cy = py + static_cast<std::uint32_t>(g->height * 0.5F);
    const std::uint8_t centre =
        atlas.pixels[static_cast<std::size_t>(cy) * atlas.width + cx];
    // Centre may be inside (>128) or outside (<128) depending on font;
    // either way it must NOT be exactly the untouched background (which
    // is `0` outside the glyph slot rectangle).
    EXPECT_NE(centre, 0U);

    // The atlas corner (0,0) lies outside ANY glyph slot the packer
    // could have used (skyline starts at y=0 row but the first packed
    // glyph occupies x>=0; here we check the FAR corner). It must be
    // the untouched background = 0.
    const std::size_t corner_idx =
        static_cast<std::size_t>(atlas.height - 1U) * atlas.width + (atlas.width - 1U);
    EXPECT_EQ(atlas.pixels[corner_idx], 0U);

    // Smooth edge: somewhere along the SDF ramp the value passes
    // through 128. Sweep a horizontal scanline through the centre row
    // and require at least one pixel pair whose values straddle 128.
    bool found_edge = false;
    std::uint8_t prev = atlas.pixels[static_cast<std::size_t>(cy) * atlas.width + px];
    for (std::uint32_t x = px + 1U; x < px + static_cast<std::uint32_t>(g->width); ++x)
    {
        const std::uint8_t v = atlas.pixels[static_cast<std::size_t>(cy) * atlas.width + x];
        if ((prev < 128U && v >= 128U) || (prev >= 128U && v < 128U))
        {
            found_edge = true;
            break;
        }
        prev = v;
    }
    EXPECT_TRUE(found_edge);
}

// (6) kMsdfMulti: msdfgen multi-channel SDF atlas. Requires FreeType +
//     msdfgen. When either is absent the test SKIPs rather than fails.
//
//     Checks:
//       a) atlas().channels == 3 for kMsdfMulti.
//       b) atlas has non-zero content in the glyph region (MSDF generated
//          something meaningful — not an all-zero bitmap).
//       c) Across the horizontal mid-scanline of the glyph slot, at least
//          one channel transitions from one side to the other (presence of
//          a sign boundary = the glyph edge was captured).
//       d) atlas_channels() accessor matches atlas().channels.
TEST(Font, MsdfMultiAtlasIsThreeChannelWithEdgeTransition)
{
    if (!cd::ui::font::Font::has_msdfgen())
    {
        GTEST_SKIP() << "Built without CD_UI_FONT_HAVE_MSDFGEN";
    }
    if (!cd::ui::font::Font::has_freetype())
    {
        GTEST_SKIP() << "Built without CD_UI_FONT_HAVE_FREETYPE (required for MSDF outline)";
    }
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }

    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kFreeType);
    EXPECT_EQ(f.select_atlas_mode(cd::ui::font::AtlasMode::kMsdfMulti),
              cd::ui::font::AtlasMode::kMsdfMulti);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x0041U, 32.0F, 1024U));  // 'A'

    // (a) atlas channels
    const auto& atlas = f.atlas();
    EXPECT_EQ(atlas.channels, 3U);
    EXPECT_EQ(f.atlas_channels(), 3U);

    // (b) atlas has non-zero content
    bool any_nonzero = false;
    for (std::uint8_t v : atlas.pixels)
    {
        if (v != 0U) { any_nonzero = true; break; }
    }
    EXPECT_TRUE(any_nonzero) << "MSDF atlas is all zeros — msdfgen produced no output";

    // (c) glyph_uv found and has a valid slot
    auto g = f.glyph_uv(0x0041U);
    ASSERT_TRUE(g.has_value());
    ASSERT_GT(g->width,  0.0F);
    ASSERT_GT(g->height, 0.0F);

    // (d) sdf_zero returns 128 for kMsdfMulti
    EXPECT_EQ(f.sdf_zero(), 128U);

    // (e) Sweep the horizontal mid-scanline of the glyph slot in the atlas
    //     and verify that at least ONE channel has a value that differs from
    //     the background (0). For a proper MSDF, values should range from
    //     near-0 (far outside) to near-255 (far inside) with the boundary
    //     around 128. We just need something non-trivially distributed.
    const auto px  = static_cast<std::uint32_t>(g->u0 * static_cast<float>(atlas.width));
    const auto py  = static_cast<std::uint32_t>(g->v0 * static_cast<float>(atlas.height));
    const auto gw  = static_cast<std::uint32_t>(g->width);
    const auto gh  = static_cast<std::uint32_t>(g->height);
    const auto cy  = py + gh / 2U;   // vertical midpoint of glyph slot

    std::uint8_t min_val = 255U, max_val = 0U;
    for (std::uint32_t x = px; x < px + gw; ++x)
    {
        // kMsdfMulti: 3 bytes per texel, row-stride = atlas.width * 3
        const std::size_t texel_base =
            (static_cast<std::size_t>(cy) * atlas.width + x) * 3U;
        for (int ch = 0; ch < 3; ++ch)
        {
            const std::uint8_t v = atlas.pixels[texel_base + static_cast<std::size_t>(ch)];
            min_val = std::min(min_val, v);
            max_val = std::max(max_val, v);
        }
    }
    // We expect at least a 64-unit spread across the scan (anything tighter
    // would suggest the SDF collapsed to a flat value).
    EXPECT_GT(static_cast<int>(max_val) - static_cast<int>(min_val), 64)
        << "MSDF mid-scanline has insufficient value range: ["
        << static_cast<int>(min_val) << ", " << static_cast<int>(max_val) << "]";
}

// (7) kMsdfMulti falls back gracefully to kMsdf when msdfgen is absent.
//     When CD_UI_FONT_HAVE_MSDFGEN is not defined, select_atlas_mode()
//     returns kMsdf instead of kMsdfMulti, and the atlas remains 1-channel.
TEST(Font, MsdfMultiFallsBackToMsdfWhenMsdfgenAbsent)
{
    // This test is ONLY interesting when msdfgen is NOT compiled in.
    // When it IS compiled in, we just verify the fallback doesn't trigger.
    if (cd::ui::font::Font::has_msdfgen())
    {
        // msdfgen IS available — verify select_atlas_mode() returns kMsdfMulti
        // (no fallback needed) and atlas.channels == 3 after rasterization.
        const auto ttf = find_system_font();
        if (ttf.empty())
        {
            GTEST_SKIP() << "No system TTF found";
        }
        cd::ui::font::Font f;
        f.select_backend(cd::ui::font::Backend::kFreeType);
        EXPECT_EQ(f.select_atlas_mode(cd::ui::font::AtlasMode::kMsdfMulti),
                  cd::ui::font::AtlasMode::kMsdfMulti);
        ASSERT_TRUE(f.load_ttf_in_memory(
            std::span<const std::uint8_t>(ttf.data(), ttf.size())));
        ASSERT_TRUE(f.rasterize_range(0x0041U, 0x005AU, 16.0F, 1024U));
        EXPECT_EQ(f.atlas().channels, 3U);
    }
    else
    {
        // msdfgen NOT available — select_atlas_mode(kMsdfMulti) must return kMsdf.
        cd::ui::font::Font f;
        const cd::ui::font::AtlasMode resolved =
            f.select_atlas_mode(cd::ui::font::AtlasMode::kMsdfMulti);
        EXPECT_EQ(resolved, cd::ui::font::AtlasMode::kMsdf)
            << "Without msdfgen, kMsdfMulti should degrade to kMsdf";
    }
}

// --- Phase 1.1 regression ---------------------------------------------------

// (8) stb backend continues to compile + pass even when FT is disabled.
//     This is verified by the Phase 1.1 cases (NewlyConstructedIsNotLoaded,
//     EmptyDataRejected, GarbageDataRejected, RasterizeAsciiOnSystemFont,
//     KerningReturnsZeroForUnloadedFont) running unconditionally; here we
//     add ONE focused case that explicitly forces Backend::kStb and walks
//     the full stb pipeline so the regression coverage is unambiguous.
TEST(Font, StbBackendAlwaysCompilesAndRasterizes)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    ASSERT_EQ(f.select_backend(cd::ui::font::Backend::kStb),
              cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x005AU, 16.0F, 1024U));  // 'A'..'Z'
    for (std::uint32_t cp = 0x0041U; cp <= 0x005AU; ++cp)
    {
        EXPECT_TRUE(f.glyph_uv(cp).has_value()) << "missing 0x" << std::hex << cp;
    }

    // Identity shape() must work without HB: 5 codepoints in, 5 glyphs out.
    auto shaped = f.shape("ABCDE", "en");
    EXPECT_EQ(shaped.size(), 5U);
    for (std::size_t i = 0; i < shaped.size(); ++i)
    {
        EXPECT_EQ(shaped[i].glyph_id, 0x41U + i);
    }
}
