// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_compress/tests/test_texture_compress.cpp
// Phase 650 (Sprint-1) + Phase 750 (Sprint-2)
//
// Sprint-1 tests (T1–T10): BC1 encoder + analyze().
// Sprint-2 tests (T11–T14): BC7 (bc7enc_rdo) + ASTC 4×4 / 8×8 (ARM astcenc).
//   Encoder-conditional: each Sprint-2 test skips gracefully (via GTEST_SKIP)
//   when the respective encoder was not compiled in (CD_TC_HAS_BC7ENC /
//   CD_TC_HAS_ASTCENC = 0).
//
// Tests:
//   T1  encode() round-trip: 4×4 synthetic image → BC1 → non-empty blob
//   T2  BC1 compression ratio ≈ 8:1 for RGBA8 input
//   T3  bad input rejected: wrong pixel count returns nullopt
//   T4  bad input rejected: dimensions not multiples of 4 returns nullopt
//   T5  BC7 format attempt: returns real blob when CD_TC_HAS_BC7ENC=1,
//       nullopt when CD_TC_HAS_BC7ENC=0
//   T6  mip generation: generate_mips=true produces more bytes than mip 0 alone
//   T7  mip count: 16×16 with generate_mips correct total byte count
//   T8  analyze() returns ratio ≈ 8.0 for a 4×4 BC1-encoded image
//   T9  analyze() returns nullopt for mismatched pixel count
//   T10 analyze() RMSE == 0 for solid-colour input (lossless for solid BC1 block)
//   T11 BC7 encode: 4×4 RGBA8 → BC7 blob = 16 bytes  [CD_TC_HAS_BC7ENC]
//   T12 BC7 compression ratio: input / output = 4.0 for 16×16  [CD_TC_HAS_BC7ENC]
//   T13 ASTC 4×4 encode: 4×4 → blob = 16 bytes        [CD_TC_HAS_ASTCENC]
//   T14 ASTC 8×8 encode: 8×8 → blob = 16 bytes        [CD_TC_HAS_ASTCENC]
//
// BAND-3 edge tests (phase1241 — seal-the-v1, lock-the-untested-branches):
//   T15 encode() BC3 / BC5 (not-yet-implemented format) → nullopt
//   T16 analyze() rejects a non-BC1 format (BC7) → nullopt
//   T17 analyze() rejects a blob shorter than one mip0 → nullopt
//   T18 solid-WHITE (255,255,255) block: analyze RMSE == 0 (BC1 lossless for a
//       single-colour block; the prior solid test only covered solid BLACK)
//   T19 decode 3-colour+transparent BC1 mode (c0 <= c1): the encoder always
//       emits 4-colour blocks (c0 >= c1), so the decoder's c0<=c1 branch
//       (palette[2]=midpoint, palette[3]=black-transparent) was never exercised.
//       A hand-crafted blob with c0 < c1 routed through analyze() reaches it.
// =============================================================================

#include <cd/asset/texture_compress/TextureCompress.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace
{

using cd::asset::texture_compress::analyze;
using cd::asset::texture_compress::CompressedTexture;
using cd::asset::texture_compress::EncodeOptions;
using cd::asset::texture_compress::encode;
using cd::asset::texture_compress::Format;

// ---- Test helpers -----------------------------------------------------------

/// Generate a synthetic RGBA8 image of size w×h with a simple gradient.
[[nodiscard]] std::vector<std::uint8_t>
make_gradient_image(std::uint32_t w, std::uint32_t h)
{
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4U);
    for (std::uint32_t y = 0U; y < h; ++y)
    {
        for (std::uint32_t x = 0U; x < w; ++x)
        {
            const std::size_t off = (static_cast<std::size_t>(y) * w + x) * 4U;
            img[off + 0U] = static_cast<std::uint8_t>((x * 255U) / std::max(1U, w - 1U));
            img[off + 1U] = static_cast<std::uint8_t>((y * 255U) / std::max(1U, h - 1U));
            img[off + 2U] = 128U;
            img[off + 3U] = 255U;
        }
    }
    return img;
}

/// Generate a solid-colour RGBA8 image.
[[nodiscard]] std::vector<std::uint8_t>
make_solid_image(std::uint32_t w, std::uint32_t h,
                 std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4U);
    for (std::size_t i = 0U; i < static_cast<std::size_t>(w) * h; ++i)
    {
        img[i * 4U + 0U] = r;
        img[i * 4U + 1U] = g;
        img[i * 4U + 2U] = b;
        img[i * 4U + 3U] = 255U;
    }
    return img;
}

// ============================================================================
// Sprint-1 tests — BC1 encoder + analyze()
// ============================================================================

// ---- T1: round-trip produces a non-empty blob -------------------------------

TEST(TextureCompress, T1_RoundTripProducesBlob)
{
    const auto img = make_gradient_image(4U, 4U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span{ img }, 4U, 4U, opts);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->format, Format::kBC1);
    EXPECT_EQ(result->width,  4U);
    EXPECT_EQ(result->height, 4U);
    // BC1: 1 block of 4×4 = 8 bytes
    EXPECT_EQ(result->blob.size(), 8UZ);
    EXPECT_FALSE(result->blob.empty());
}

// ---- T2: BC1 compression ratio is 8:1 for RGBA8 input ----------------------

TEST(TextureCompress, T2_Bc1RatioIsEightToOne)
{
    constexpr std::uint32_t kW = 16U;
    constexpr std::uint32_t kH = 16U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span{ img }, kW, kH, opts);

    ASSERT_TRUE(result.has_value());

    const std::size_t input_bytes  = img.size();           // kW*kH*4 = 1024
    const std::size_t output_bytes = result->blob.size();  // kW*kH/2 = 128

    EXPECT_EQ(input_bytes,  1024UZ);
    EXPECT_EQ(output_bytes,  128UZ);

    const double ratio = static_cast<double>(input_bytes) /
                         static_cast<double>(output_bytes);
    EXPECT_NEAR(ratio, 8.0, 1e-9);
}

// ---- T3: bad input — wrong pixel count returns nullopt ----------------------

TEST(TextureCompress, T3_WrongPixelCountReturnsNullopt)
{
    const std::array<std::uint8_t, 3> tiny{ 0U, 0U, 0U };
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span<const std::uint8_t>{ tiny }, 4U, 4U, opts);
    EXPECT_FALSE(result.has_value());
}

// ---- T4: bad input — dimensions not multiples of 4 returns nullopt ---------

TEST(TextureCompress, T4_NonMultipleDimensionsReturnsNullopt)
{
    const std::vector<std::uint8_t> img(static_cast<std::size_t>(5U) * 5U * 4U, 128U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span{ img }, 5U, 5U, opts);
    EXPECT_FALSE(result.has_value());
}

// ---- T5: BC7 format — real encode when available, nullopt otherwise --------
// Sprint-2: kBC7 now has a real implementation when CD_TC_HAS_BC7ENC=1.

TEST(TextureCompress, T5_Bc7EncodeOrNullopt)
{
    const auto img = make_gradient_image(4U, 4U);
    const EncodeOptions opts{ .target = Format::kBC7, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span{ img }, 4U, 4U, opts);

#if CD_TC_HAS_BC7ENC
    // With real encoder: should produce exactly 16 bytes (1 BC7 block).
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->format, Format::kBC7);
    EXPECT_EQ(result->blob.size(), 16UZ);
#else
    // Without encoder: should return nullopt.
    EXPECT_FALSE(result.has_value());
#endif
}

// ---- T6: mip generation produces more bytes than mip 0 alone ---------------

TEST(TextureCompress, T6_MipGenerationProducesMoreBytes)
{
    constexpr std::uint32_t kW = 16U;
    constexpr std::uint32_t kH = 16U;

    const auto img = make_gradient_image(kW, kH);

    const EncodeOptions no_mips { .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const EncodeOptions with_mips{ .target = Format::kBC1, .quality = 128U, .generate_mips = true  };

    const auto result_no_mips   = encode(std::span{ img }, kW, kH, no_mips);
    const auto result_with_mips = encode(std::span{ img }, kW, kH, with_mips);

    ASSERT_TRUE(result_no_mips.has_value());
    ASSERT_TRUE(result_with_mips.has_value());

    EXPECT_GT(result_with_mips->blob.size(), result_no_mips->blob.size());
}

// ---- T7: mip count — 16×16 with generate_mips produces correct byte count --

TEST(TextureCompress, T7_MipChainByteCount)
{
    constexpr std::uint32_t kW = 16U;
    constexpr std::uint32_t kH = 16U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = true };
    const auto result = encode(std::span{ img }, kW, kH, opts);

    ASSERT_TRUE(result.has_value());

    // Mips: 16x16=128, 8x8=32, 4x4=8, 2x2=8(padded), 1x1=8(padded) → 184
    EXPECT_EQ(result->blob.size(), 184UZ);
}

// ---- T8: analyze() returns ratio ≈ 8.0 for a 4×4 BC1-encoded image ---------

TEST(TextureCompress, T8_AnalyzeRatioIsEightToOne)
{
    constexpr std::uint32_t kW = 4U;
    constexpr std::uint32_t kH = 4U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, kW, kH, opts);
    ASSERT_TRUE(compressed.has_value());

    const auto stats = analyze(std::span{ img }, *compressed);
    ASSERT_TRUE(stats.has_value());

    EXPECT_EQ(stats->input_bytes,  static_cast<std::uint64_t>(kW * kH * 4U));
    EXPECT_EQ(stats->output_bytes, 8ULL);
    EXPECT_NEAR(stats->ratio, 8.0, 1e-6);
}

// ---- T9: analyze() returns nullopt for mismatched pixel count ---------------

TEST(TextureCompress, T9_AnalyzeRejectsWrongPixelCount)
{
    const auto img = make_gradient_image(4U, 4U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, 4U, 4U, opts);
    ASSERT_TRUE(compressed.has_value());

    const std::array<std::uint8_t, 4U> wrong{ 0U, 0U, 0U, 255U };
    const auto stats = analyze(std::span<const std::uint8_t>{ wrong }, *compressed);
    EXPECT_FALSE(stats.has_value());
}

// ---- T10: analyze() RMSE == 0 for solid-colour input -----------------------

TEST(TextureCompress, T10_AnalyzeRmseZeroForSolidColor)
{
    constexpr std::uint32_t kW = 4U;
    constexpr std::uint32_t kH = 4U;

    const auto img = make_solid_image(kW, kH, 0U, 0U, 0U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, kW, kH, opts);
    ASSERT_TRUE(compressed.has_value());

    const auto stats = analyze(std::span{ img }, *compressed);
    ASSERT_TRUE(stats.has_value());

    EXPECT_GE(stats->rmse, 0.0);
    EXPECT_TRUE(std::isfinite(stats->rmse));
    EXPECT_NEAR(stats->rmse, 0.0, 1e-6);
}

// ============================================================================
// Sprint-2 tests — BC7 + ASTC real encoders
// ============================================================================

// ---- T11: BC7 encode — 4×4 → 16-byte block ---------------------------------

TEST(TextureCompress, T11_Bc7Encode4x4Produces16Bytes)
{
#if !CD_TC_HAS_BC7ENC
    GTEST_SKIP() << "BC7 encoder (bc7enc_rdo) not compiled in";
#else
    const auto img = make_gradient_image(4U, 4U);
    const EncodeOptions opts{ .target = Format::kBC7, .quality = 64U, .generate_mips = false };
    const auto result = encode(std::span{ img }, 4U, 4U, opts);

    ASSERT_TRUE(result.has_value()) << "encode_bc7 returned nullopt";
    EXPECT_EQ(result->format, Format::kBC7);
    EXPECT_EQ(result->width,  4U);
    EXPECT_EQ(result->height, 4U);
    // BC7: 1 block of 4×4 = 16 bytes
    EXPECT_EQ(result->blob.size(), 16UZ);
    EXPECT_FALSE(result->blob.empty());
#endif
}

// ---- T12: BC7 compression ratio — RGBA8 input 4:1 --------------------------

TEST(TextureCompress, T12_Bc7RatioIsFourToOne)
{
#if !CD_TC_HAS_BC7ENC
    GTEST_SKIP() << "BC7 encoder (bc7enc_rdo) not compiled in";
#else
    // BC7: 16 bytes per 4×4 block = 1 byte/texel = 8 bpp.
    // RGBA8 = 4 bytes/texel = 32 bpp.
    // Ratio = 32 / 8 = 4.0.
    constexpr std::uint32_t kW = 16U;
    constexpr std::uint32_t kH = 16U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC7, .quality = 64U, .generate_mips = false };
    const auto result = encode(std::span{ img }, kW, kH, opts);

    ASSERT_TRUE(result.has_value()) << "encode_bc7 returned nullopt";

    const std::size_t input_bytes  = img.size();           // 16*16*4 = 1024
    const std::size_t output_bytes = result->blob.size();  // 16*16*1 = 256

    EXPECT_EQ(input_bytes,  1024UZ);
    EXPECT_EQ(output_bytes,  256UZ);

    const double ratio = static_cast<double>(input_bytes) /
                         static_cast<double>(output_bytes);
    EXPECT_NEAR(ratio, 4.0, 1e-9);
#endif
}

// ---- T13: ASTC 4×4 encode — 4×4 → 16-byte block ----------------------------

TEST(TextureCompress, T13_Astc4x4Encode4x4Produces16Bytes)
{
#if !CD_TC_HAS_ASTCENC
    GTEST_SKIP() << "ASTC encoder (ARM astc-encoder) not compiled in";
#else
    const auto img = make_gradient_image(4U, 4U);
    const EncodeOptions opts{ .target = Format::kAstc4x4, .quality = 64U, .generate_mips = false };
    const auto result = encode(std::span{ img }, 4U, 4U, opts);

    ASSERT_TRUE(result.has_value()) << "encode_astc 4x4 returned nullopt";
    EXPECT_EQ(result->format, Format::kAstc4x4);
    EXPECT_EQ(result->width,  4U);
    EXPECT_EQ(result->height, 4U);
    // ASTC: 1 block of 4×4 = 16 bytes
    EXPECT_EQ(result->blob.size(), 16UZ);
    EXPECT_FALSE(result->blob.empty());
#endif
}

// ---- T14: ASTC 8×8 encode — 8×8 → 16-byte block ----------------------------

TEST(TextureCompress, T14_Astc8x8Encode8x8Produces16Bytes)
{
#if !CD_TC_HAS_ASTCENC
    GTEST_SKIP() << "ASTC encoder (ARM astc-encoder) not compiled in";
#else
    const auto img = make_gradient_image(8U, 8U);
    const EncodeOptions opts{ .target = Format::kAstc8x8, .quality = 64U, .generate_mips = false };
    const auto result = encode(std::span{ img }, 8U, 8U, opts);

    ASSERT_TRUE(result.has_value()) << "encode_astc 8x8 returned nullopt";
    EXPECT_EQ(result->format, Format::kAstc8x8);
    EXPECT_EQ(result->width,  8U);
    EXPECT_EQ(result->height, 8U);
    // ASTC 8×8: ceil(8/8) * ceil(8/8) * 16 = 1 block = 16 bytes
    EXPECT_EQ(result->blob.size(), 16UZ);
    EXPECT_FALSE(result->blob.empty());
#endif
}

// ============================================================================
// BAND-3 edge tests — lock untested real branches
// ============================================================================

// ---- T15: BC3 / BC5 are not implemented yet → encode returns nullopt -------
// Exercises the final fall-through in encode() (the "BC3 / BC5 not yet
// implemented" path) which no prior test reached — every other test targets
// BC1 / BC7 / ASTC.

TEST(TextureCompress, T15_Bc3Bc5ReturnNullopt)
{
    const auto img = make_gradient_image(4U, 4U);

    const EncodeOptions bc3{ .target = Format::kBC3, .quality = 128U, .generate_mips = false };
    const EncodeOptions bc5{ .target = Format::kBC5, .quality = 128U, .generate_mips = false };

    EXPECT_FALSE(encode(std::span{ img }, 4U, 4U, bc3).has_value());
    EXPECT_FALSE(encode(std::span{ img }, 4U, 4U, bc5).has_value());
}

// ---- T16: analyze() only supports BC1 → non-BC1 format returns nullopt -----
// Exercises the `compressed.format != Format::kBC1` guard at the top of
// analyze(); prior analyze tests only ever passed BC1 textures.

TEST(TextureCompress, T16_AnalyzeRejectsNonBc1Format)
{
    const auto img = make_gradient_image(4U, 4U);

    CompressedTexture fake_bc7;
    fake_bc7.format = Format::kBC7;
    fake_bc7.width  = 4U;
    fake_bc7.height = 4U;
    fake_bc7.blob.assign(16U, 0U);  // BC7 block size, contents irrelevant

    const auto stats = analyze(std::span{ img }, fake_bc7);
    EXPECT_FALSE(stats.has_value());
}

// ---- T17: analyze() rejects a blob shorter than one mip0 → nullopt ----------
// Exercises the `compressed.blob.size() < mip0_bytes` guard. A 4×4 BC1 mip0 is
// 8 bytes; a 7-byte blob must be rejected rather than read out of bounds.

TEST(TextureCompress, T17_AnalyzeRejectsTruncatedBlob)
{
    const auto img = make_gradient_image(4U, 4U);

    CompressedTexture truncated;
    truncated.format = Format::kBC1;
    truncated.width  = 4U;
    truncated.height = 4U;
    truncated.blob.assign(7U, 0U);  // one byte short of a single 8-byte block

    const auto stats = analyze(std::span{ img }, truncated);
    EXPECT_FALSE(stats.has_value());
}

// ---- T18: solid-WHITE block — BC1 is lossless, RMSE == 0 -------------------
// The prior solid-colour test (T10) only covered solid BLACK (0,0,0), which is
// also palette[3] in the transparent branch. Solid WHITE (255,255,255) drives a
// distinct endpoint (max==min==white) — locking that the single-colour fast
// path reconstructs exactly for a non-black colour too.

TEST(TextureCompress, T18_AnalyzeRmseZeroForSolidWhite)
{
    constexpr std::uint32_t kW = 4U;
    constexpr std::uint32_t kH = 4U;

    const auto img = make_solid_image(kW, kH, 255U, 255U, 255U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, kW, kH, opts);
    ASSERT_TRUE(compressed.has_value());

    const auto stats = analyze(std::span{ img }, *compressed);
    ASSERT_TRUE(stats.has_value());

    EXPECT_TRUE(std::isfinite(stats->rmse));
    EXPECT_NEAR(stats->rmse, 0.0, 1e-6);
}

// ---- T19: decode 3-colour + transparent BC1 mode (c0 <= c1) ----------------
// The encoder always normalises endpoints to c0 >= c1, so the decoder's
// c0 <= c1 branch (palette[2] = (p0+p1)/2, palette[3] = transparent black) is
// never reached through encode(). A hand-crafted blob with c0 < c1, decoded via
// analyze(), is the only way to exercise it. We pick c0 = RGB565(black) = 0x0000
// and c1 = RGB565(white) = 0xFFFF so c0 < c1, with all 16 texel indices = 0
// (→ palette[0] = black) — a fully reconstructible solid-black image. This both
// reaches the c0<=c1 decode branch AND verifies it decodes index 0 correctly.

TEST(TextureCompress, T19_DecodeThreeColorTransparentModeBranch)
{
    // 4×4 solid-black original — the reference we compare the decode against.
    const auto img = make_solid_image(4U, 4U, 0U, 0U, 0U);

    CompressedTexture handcrafted;
    handcrafted.format = Format::kBC1;
    handcrafted.width  = 4U;
    handcrafted.height = 4U;
    handcrafted.blob.assign(8U, 0U);
    // color0 = 0x0000 (black, low) ; color1 = 0xFFFF (white, high) → c0 < c1.
    handcrafted.blob[0] = 0x00U;  handcrafted.blob[1] = 0x00U;  // c0 LE = 0x0000
    handcrafted.blob[2] = 0xFFU;  handcrafted.blob[3] = 0xFFU;  // c1 LE = 0xFFFF
    // indices [4..7] all zero → every texel selects palette[0] = p0 = black.

    const auto stats = analyze(std::span{ img }, handcrafted);
    ASSERT_TRUE(stats.has_value());

    // palette[0] == black == the original image → exact reconstruction.
    EXPECT_TRUE(std::isfinite(stats->rmse));
    EXPECT_NEAR(stats->rmse, 0.0, 1e-6);
}

}  // namespace
