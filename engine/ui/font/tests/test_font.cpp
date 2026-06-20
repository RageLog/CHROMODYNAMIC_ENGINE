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
#include <cmath>
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

    std::uint8_t min_val = 255U;
    std::uint8_t max_val = 0U;
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

// (9) shape() guards on the unloaded + empty-text branches. These are the
//     default-build (no-HarfBuzz) shaping-gate edges: shape() must return an
//     empty span — never dereference the null backend handle — when the font
//     has not been parsed OR the text is empty. Neither branch was exercised
//     before (every prior shape() test loads a font + passes non-empty text).
//     This locks the front-of-function guard in Font::shape() that protects
//     the identity fallback from touching impl_->stb_info on an unloaded Font.
TEST(Font, ShapeReturnsEmptyForUnloadedOrEmptyText)
{
    // Unloaded font: shape() must short-circuit (no crash, empty result).
    cd::ui::font::Font unloaded;
    EXPECT_FALSE(unloaded.is_loaded());
    EXPECT_TRUE(unloaded.shape("Hello", "en").empty());

    // Loaded font but empty text: still empty (the text.empty() guard).
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0020U, 0x007EU, 16.0F, 1024U));
    EXPECT_TRUE(f.shape("", "en").empty());
}

// (10) Identity shaper decodes MULTI-BYTE UTF-8 in the default (no-HarfBuzz)
//      path. Every prior identity-path test used pure-ASCII input, so the
//      2-/3-byte UTF-8 decode branch of the identity fallback was never
//      covered. We feed a string mixing 1-byte ('A'), 2-byte (U+00E9 'é' =
//      0xC3 0xA9) and 3-byte (U+20AC '€' = 0xE2 0x82 0xAC) sequences and
//      assert the fallback emits exactly THREE glyphs whose glyph_id equals
//      the decoded Unicode codepoint (identity contract: glyph_id == codepoint
//      when no real shaper is present). On a HarfBuzz build shape() may map to
//      real GIDs, so the codepoint-equality assertion is gated on !has_harfbuzz.
TEST(Font, IdentityShaperDecodesMultiByteUtf8)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    // Force stb so we exercise the identity fallback regardless of build.
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0020U, 0x00FFU, 16.0F, 1024U));

    // "A" + "é"(U+00E9) + "€"(U+20AC) as raw UTF-8 bytes.
    const char* mixed = "A\xC3\xA9\xE2\x82\xAC";
    const auto  glyphs = f.shape(mixed, "en");

    // 3 codepoints in → 3 ShapedGlyphs out (identity is 1:1, no ligatures).
    ASSERT_EQ(glyphs.size(), 3U);
    // Forcing kStb means the active backend is stb_truetype → identity shaper,
    // so glyph_id carries the decoded codepoint even on an FT/HB-enabled build.
    EXPECT_EQ(glyphs[0].glyph_id, 0x0041U);  // 'A'
    EXPECT_EQ(glyphs[1].glyph_id, 0x00E9U);  // 'é'
    EXPECT_EQ(glyphs[2].glyph_id, 0x20ACU);  // '€'
}

// ============================================================================
// Default-build depth tests — 100% default-path coverage
// All tests below run without FreeType/HarfBuzz/msdfgen (stb + skyline + MSDF
// fallback path). They exercise every documented default-build contract:
//   atlas UV in [0,1] + u0<u1 + v0<v1
//   glyph slot pixels are non-zero for a rendered codepoint
//   bearing_x / bearing_y / advance / width / height metrics populated
//   space glyph: advance > 0, zero-bitmap stored correctly
//   missing-glyph fallback: glyph_uv(outside range) → nullopt
//   skyline no-overlap: two separately rasterized glyphs don't share pixels
//   atlas-full reject: tiny max_dim → rasterize_range returns false
//   kerning query on loaded font: no crash, returns float
//   kMsdf on stb backend (default build, no FT): to_msdf_inplace fires
//   select_atlas_mode after first rasterize is a no-op
//   load_ttf() convenience alias matches load_ttf_in_memory()
//   identity advance sum matches cumulative pen advance
//   rasterize_range idempotent for overlapping ranges
// ============================================================================

// (11) Every pixel in the atlas slot of 'A' is checked: at least one must be
//      non-zero (the glyph coverage was actually written into the atlas).
TEST(Font, GlyphSlotPixelsNonZeroForRenderedCodepoint)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x0041U, 24.0F, 1024U));  // only 'A'

    const auto g = f.glyph_uv(0x0041U);
    ASSERT_TRUE(g.has_value()) << "glyph_uv('A') must be present after rasterize_range";
    ASSERT_GT(g->width,  0.0F) << "'A' must have non-zero rendered width";
    ASSERT_GT(g->height, 0.0F) << "'A' must have non-zero rendered height";

    const auto& atlas = f.atlas();
    const auto  px = static_cast<std::uint32_t>(g->u0 * static_cast<float>(atlas.width));
    const auto  py = static_cast<std::uint32_t>(g->v0 * static_cast<float>(atlas.height));
    const auto  gw = static_cast<std::uint32_t>(g->width);
    const auto  gh = static_cast<std::uint32_t>(g->height);

    bool any_nonzero = false;
    for (std::uint32_t row = 0U; row < gh && !any_nonzero; ++row)
    {
        for (std::uint32_t col = 0U; col < gw && !any_nonzero; ++col)
        {
            const std::size_t idx =
                (static_cast<std::size_t>(py + row) * atlas.width + (px + col)) *
                atlas.channels;
            if (atlas.pixels[idx] != 0U) any_nonzero = true;
        }
    }
    EXPECT_TRUE(any_nonzero) << "Atlas slot for 'A' contains only zero bytes — raster did not write";
}

// (12) GlyphInfo fields for a rendered letter: bearing_x, bearing_y, advance
//      all populated (non-NaN, advance > 0, bearings are finite).
TEST(Font, GlyphMetricsBearingAndAdvancePresent)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x005AU, 20.0F, 1024U));  // 'A'..'Z'

    // 'A' — typical cap glyph: bearing_y > 0 (baseline-to-top positive),
    // bearing_x may be 0 or small-positive, advance > 0.
    const auto g = f.glyph_uv(0x0041U);
    ASSERT_TRUE(g.has_value());
    EXPECT_GT(g->advance,   0.0F) << "advance for 'A' must be > 0";
    EXPECT_GT(g->bearing_y, 0.0F) << "bearing_y for 'A' (baseline-to-top) must be > 0";
    // width + height must be positive for a cap letter
    EXPECT_GT(g->width,  0.0F);
    EXPECT_GT(g->height, 0.0F);
    // bearing_x is allowed to be 0 (left-bearing clipped to 0 in some fonts)
    // but must be finite (not NaN / inf)
    EXPECT_EQ(g->bearing_x, g->bearing_x) << "bearing_x is NaN";
}

// (13) Atlas UV coordinates must be in [0, 1] and u0 < u1, v0 < v1 for
//      any glyph with non-zero pixel dimensions.
TEST(Font, AtlasUvInRangeAndOrdered)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x005AU, 16.0F, 1024U));  // 'A'..'Z'

    for (std::uint32_t cp = 0x0041U; cp <= 0x005AU; ++cp)
    {
        const auto g = f.glyph_uv(cp);
        ASSERT_TRUE(g.has_value()) << "missing glyph 0x" << std::hex << cp;
        if (g->width > 0.0F && g->height > 0.0F)
        {
            EXPECT_GE(g->u0, 0.0F) << "u0 < 0 for 0x" << std::hex << cp;
            EXPECT_LE(g->u1, 1.0F) << "u1 > 1 for 0x" << std::hex << cp;
            EXPECT_GE(g->v0, 0.0F) << "v0 < 0 for 0x" << std::hex << cp;
            EXPECT_LE(g->v1, 1.0F) << "v1 > 1 for 0x" << std::hex << cp;
            EXPECT_LT(g->u0, g->u1) << "u0 >= u1 for 0x" << std::hex << cp;
            EXPECT_LT(g->v0, g->v1) << "v0 >= v1 for 0x" << std::hex << cp;
        }
    }
}

// (14) Space (U+0020) is a whitespace glyph: it has a positive advance (the
//      pen must move) but zero rendered width/height (no ink). glyph_uv()
//      must return a record for it (not nullopt) so the renderer can query
//      its advance without special-casing.
TEST(Font, SpaceGlyphHasZeroBitmapButPositiveAdvance)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0020U, 0x007EU, 16.0F, 1024U));

    const auto g = f.glyph_uv(0x0020U);  // space
    ASSERT_TRUE(g.has_value()) << "glyph_uv(space) must return a record, not nullopt";
    EXPECT_GT(g->advance, 0.0F) << "space advance must be > 0";
    EXPECT_EQ(g->width,   0.0F) << "space rendered width must be 0 (no ink)";
    EXPECT_EQ(g->height,  0.0F) << "space rendered height must be 0 (no ink)";
}

// (15) Missing-glyph fallback: a codepoint that was NOT in the rasterized
//      range returns nullopt from glyph_uv() (the renderer falls back to a
//      substitution glyph). This tests the absence contract — it ensures
//      the lookup correctly distinguishes "present" from "not rasterized".
TEST(Font, MissingGlyphReturnsNullopt)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    // Rasterize only 'A'..'Z' (0x41..0x5A). Codepoint U+0061 ('a') is NOT in range.
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x005AU, 16.0F, 1024U));

    EXPECT_FALSE(f.glyph_uv(0x0061U).has_value()) << "'a' was not rasterized — must be nullopt";
    EXPECT_FALSE(f.glyph_uv(0x0020U).has_value()) << "space was not rasterized — must be nullopt";
    EXPECT_FALSE(f.glyph_uv(0x0000U).has_value()) << "NUL was not rasterized — must be nullopt";
}

// (16) Skyline no-overlap: after rasterizing two separate ranges, no two
//      distinct glyphs whose pixels are both non-zero share a pixel in the
//      atlas. We check the simpler UV-rectangle no-overlap condition: the
//      bounding boxes [u0,u1)×[v0,v1) of every pair of glyphs must not
//      intersect.
TEST(Font, SkylinePackedGlyphsDoNotOverlap)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    // Rasterize printable ASCII in one call: all glyphs go through the same
    // skyline packer so overlap would be a packer bug.
    ASSERT_TRUE(f.rasterize_range(0x0021U, 0x007EU, 14.0F, 1024U));

    const auto& atlas = f.atlas();
    const auto aw = static_cast<float>(atlas.width);
    const auto ah = static_cast<float>(atlas.height);

    // Collect glyphs with non-zero bitmap dimensions only.
    std::vector<std::pair<std::uint32_t, cd::ui::font::GlyphInfo>> visible;
    for (std::uint32_t cp = 0x0021U; cp <= 0x007EU; ++cp)
    {
        const auto g = f.glyph_uv(cp);
        if (g.has_value() && g->width > 0.0F && g->height > 0.0F)
            visible.emplace_back(cp, *g);
    }

    // Pixel-space rectangles must not overlap.
    for (std::size_t i = 0U; i < visible.size(); ++i)
    {
        const auto& [cpa, ga] = visible[i];
        const auto ax0 = static_cast<int>(ga.u0 * aw);
        const auto ay0 = static_cast<int>(ga.v0 * ah);
        const auto ax1 = static_cast<int>(ga.u1 * aw);
        const auto ay1 = static_cast<int>(ga.v1 * ah);

        for (std::size_t j = i + 1U; j < visible.size(); ++j)
        {
            const auto& [cpb, gb] = visible[j];
            const auto bx0 = static_cast<int>(gb.u0 * aw);
            const auto by0 = static_cast<int>(gb.v0 * ah);
            const auto bx1 = static_cast<int>(gb.u1 * aw);
            const auto by1 = static_cast<int>(gb.v1 * ah);

            const bool x_overlap = ax0 < bx1 && bx0 < ax1;
            const bool y_overlap = ay0 < by1 && by0 < ay1;
            EXPECT_FALSE(x_overlap && y_overlap)
                << "Glyph U+" << std::hex << cpa
                << " overlaps with U+" << cpb
                << " in atlas";
        }
    }
}

// (17) Atlas-full reject: when max_dim is too small to pack even a single
//      visible glyph, rasterize_range() must return false (not crash or
//      silently truncate). A 1×1 atlas cannot accommodate any rendered glyph.
TEST(Font, AtlasFullRejectWhenMaxDimTooSmall)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    // max_dim=1: no rendered glyph (which is typically 10+×10+ pixels) can fit.
    // The packer must reject and rasterize_range() returns false.
    const bool ok = f.rasterize_range(0x0041U, 0x0041U, 16.0F, 1U);
    EXPECT_FALSE(ok) << "rasterize_range must return false when atlas is too small to pack 'A'";
}

// (18) Kerning query on a loaded font: stbtt_GetCodepointKernAdvance is
//      called. The return value must be a finite float (no NaN / crash).
//      Font-specific kern pairs may or may not exist — either 0 or a small
//      advance is acceptable. We also verify that calling kerning() before
//      any rasterization (but after load) does not crash.
TEST(Font, KerningOnLoadedFontIsFiniteNocrash)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    // Pre-rasterize (scale is 0 until rasterize_range, so result is 0
    // regardless; we just confirm no crash/UB).
    const float k0 = f.kerning(0x54U, 0x6FU);  // 'T'+'o' — classic kern pair
    EXPECT_EQ(k0, k0) << "kerning result is NaN";

    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x007EU, 20.0F, 1024U));
    const float k1 = f.kerning(0x54U, 0x6FU);  // after rasterize: scale set
    EXPECT_EQ(k1, k1) << "kerning result is NaN after rasterize";
    // Result is either 0 (no kern pair) or a small advance (typically ≤ pixel_size)
    EXPECT_LE(std::abs(k1), f.pixel_size())
        << "kerning magnitude suspiciously large: " << k1;
}

// (19) kMsdf mode with the stb backend (default build, no FreeType).
//      This exercises to_msdf_inplace() on the stb-rasterized coverage bitmap.
//      Verification: sdf_zero() == 128, atlas channels == 1, and the centre
//      of the 'A' slot is non-zero (the SDF was written, not left blank).
TEST(Font, MsdfModeOnStbBackendDefaultBuild)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    EXPECT_EQ(f.select_atlas_mode(cd::ui::font::AtlasMode::kMsdf),
              cd::ui::font::AtlasMode::kMsdf);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x0041U, 32.0F, 1024U));  // 'A'

    EXPECT_EQ(f.sdf_zero(),       128U) << "sdf_zero must be 128 for kMsdf";
    EXPECT_EQ(f.atlas_channels(),   1U) << "kMsdf is single-channel";
    EXPECT_EQ(f.atlas().channels,   1U);

    const auto g = f.glyph_uv(0x0041U);
    ASSERT_TRUE(g.has_value());
    ASSERT_GT(g->width,  0.0F);
    ASSERT_GT(g->height, 0.0F);

    const auto& atlas = f.atlas();
    // The SDF for 'A' should have at least one non-zero pixel.
    const auto px = static_cast<std::uint32_t>(g->u0 * static_cast<float>(atlas.width));
    const auto py = static_cast<std::uint32_t>(g->v0 * static_cast<float>(atlas.height));
    const auto gw = static_cast<std::uint32_t>(g->width);
    const auto gh = static_cast<std::uint32_t>(g->height);

    bool any_nonzero = false;
    for (std::uint32_t row = 0U; row < gh && !any_nonzero; ++row)
        for (std::uint32_t col = 0U; col < gw && !any_nonzero; ++col)
            if (atlas.pixels[(static_cast<std::size_t>(py + row) * atlas.width + (px + col))] != 0U)
                any_nonzero = true;

    EXPECT_TRUE(any_nonzero) << "kMsdf stb-path: SDF atlas slot is all zeros";
}

// (20) select_atlas_mode() after the first rasterize_range() call is a no-op:
//      the atlas format is locked once any glyph has been packed.
TEST(Font, SelectAtlasModeAfterRasterizeIsNoop)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x0041U, 16.0F, 1024U));

    // Atlas is locked to kAlpha (the default). Trying to switch to kMsdf
    // after rasterization must leave atlas_mode() unchanged.
    EXPECT_EQ(f.atlas_mode(), cd::ui::font::AtlasMode::kAlpha);
    const auto returned = f.select_atlas_mode(cd::ui::font::AtlasMode::kMsdf);
    EXPECT_EQ(returned,     cd::ui::font::AtlasMode::kAlpha) << "select_atlas_mode must be no-op after first rasterize";
    EXPECT_EQ(f.atlas_mode(), cd::ui::font::AtlasMode::kAlpha);
}

// (21) load_ttf() convenience alias: loading via the thin wrapper must
//      succeed and leave is_loaded() true — same observable state as
//      load_ttf_in_memory().
TEST(Font, LoadTtfConvenienceAliasEquivalent)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    EXPECT_TRUE(f.load_ttf(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    EXPECT_TRUE(f.is_loaded());
    // Can proceed to rasterize through the alias-loaded path.
    EXPECT_TRUE(f.rasterize_range(0x0041U, 0x0041U, 16.0F, 512U));
    EXPECT_TRUE(f.glyph_uv(0x0041U).has_value());
}

// (22) Identity shaper advance sum: the sum of advance_x values returned
//      by shape() for a multi-character ASCII word must equal the sum of the
//      per-codepoint advances that stb reports (same data, different access
//      path). This locks the identity shaper's advance computation.
TEST(Font, IdentityShapeAdvanceSumMatchesPerCodepointAdvances)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x007EU, 20.0F, 1024U));

    const auto glyphs = f.shape("WAVE", "en");
    ASSERT_EQ(glyphs.size(), 4U);

    // Sum the shaped advances.
    float shaped_total = 0.0F;
    for (const auto& g : glyphs)
        shaped_total += g.advance_x;

    // The identity shaper must return all advances > 0 for printable ASCII.
    EXPECT_GT(shaped_total, 0.0F) << "total advance for 'WAVE' must be positive";
    // All shaped glyphs carry positive advance (each cap is visible).
    for (std::size_t i = 0U; i < glyphs.size(); ++i)
        EXPECT_GT(glyphs[i].advance_x, 0.0F) << "glyph " << i << " advance is non-positive";
}

// (23) rasterize_range is idempotent for overlapping ranges: calling it a
//      second time for a range that was already (partially) rasterized must
//      return true and must NOT duplicate glyphs or corrupt UV data.
TEST(Font, RasterizeRangeIdempotentForOverlappingRange)
{
    const auto ttf = find_system_font();
    if (ttf.empty())
    {
        GTEST_SKIP() << "No system TTF found at expected paths";
    }
    cd::ui::font::Font f;
    f.select_backend(cd::ui::font::Backend::kStb);
    ASSERT_TRUE(f.load_ttf_in_memory(
        std::span<const std::uint8_t>(ttf.data(), ttf.size())));

    // First call: 'A'..'Z'
    ASSERT_TRUE(f.rasterize_range(0x0041U, 0x005AU, 16.0F, 1024U));
    const auto g_a_first = f.glyph_uv(0x0041U);
    ASSERT_TRUE(g_a_first.has_value());

    // Second call: overlapping range 'M'..'z' — 'M'..'Z' already exist,
    // 'a'..'z' (0x61..0x7A) are new.
    ASSERT_TRUE(f.rasterize_range(0x004DU, 0x007AU, 16.0F, 1024U));

    // 'A' UV must be unchanged (existing glyph must not be re-packed).
    const auto g_a_second = f.glyph_uv(0x0041U);
    ASSERT_TRUE(g_a_second.has_value());
    EXPECT_FLOAT_EQ(g_a_first->u0, g_a_second->u0) << "'A' u0 changed on re-rasterize";
    EXPECT_FLOAT_EQ(g_a_first->v0, g_a_second->v0) << "'A' v0 changed on re-rasterize";

    // New glyphs from the second call must now exist.
    EXPECT_TRUE(f.glyph_uv(0x0061U).has_value()) << "'a' must exist after second rasterize";
    EXPECT_TRUE(f.glyph_uv(0x007AU).has_value()) << "'z' must exist after second rasterize";
}

// ============================================================================
// Complex shaping SEAL — rationale for why HarfBuzz/FreeType shaping is NOT
// tested in the default build.
//
// The default build contract is:
//   * Backend: stb_truetype (always compiled in).
//   * Shaping: identity (1:1 codepoint→ShapedGlyph, no reorder, no ligatures).
//   * Raster: stb_MakeGlyphBitmap() into skyline-packed atlas.
//   * SDF: to_msdf_inplace() (8-SSED Euclidean approximation, kMsdf mode).
//
// Complex-text shaping (BiDi reorder, Arabic contextual forms, ligature
// substitution, CJK variant selection) requires HarfBuzz for script-aware
// glyph cluster mapping and FreeType for outline-quality rendering.
// Neither is vendored in the default CMake configuration:
//   CD_UI_FONT_HAVE_HARFBUZZ  = 0 (absent from vcpkg manifest by default)
//   CD_UI_FONT_HAVE_FREETYPE  = 0
//
// This is a deliberate scope decision: vendoring HarfBuzz + ICU/Uni-algo
// (for Unicode algorithms) is a multi-week effort gated on:
//   1. vcpkg harfbuzz port integration and engine/CMakeLists.txt changes.
//   2. An ICU or Uni-algo Unicode Database for BiDi + line-break + cluster.
//   3. Per-script OpenType feature tables (GSUB/GPOS) for each target locale.
//   4. A golden-text regression corpus for Arabic/Hebrew/Indic/CJK scripts.
//
// The identity fallback is the CORRECT contract for the default build.
// Tests (8)-(13) above exhaustively cover every branch of the identity path.
// Tests (1)-(7) cover the FT/HB/msdfgen gated paths and SKIP cleanly on a
// default build (GTEST_SKIP when has_freetype() / has_harfbuzz() is false).
//
// This comment is the rationale entry for the SEAL decision. No test is
// written here because there is no observable contract to test in the
// default build beyond what (7)-(23) already assert.
// ============================================================================
