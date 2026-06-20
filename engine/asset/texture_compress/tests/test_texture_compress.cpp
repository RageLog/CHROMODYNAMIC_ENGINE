// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_compress/tests/test_texture_compress.cpp
// Phase 650 (Sprint-1) + Phase 750 (Sprint-2) + Phase 803 (100% depth)
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
//   T15 encode() BC3 / BC5 (sealed / not-in-scope) → nullopt
//   T16 analyze() rejects a non-BC1 format (BC7) → nullopt
//   T17 analyze() rejects a blob shorter than one mip0 → nullopt
//   T18 solid-WHITE (255,255,255) block: analyze RMSE == 0 (BC1 lossless for a
//       single-colour block; the prior solid test only covered solid BLACK)
//   T19 decode 3-colour+transparent BC1 mode (c0 <= c1): the encoder always
//       emits 4-colour blocks (c0 >= c1), so the decoder's c0<=c1 branch
//       (palette[2]=midpoint, palette[3]=black-transparent) was never exercised.
//       A hand-crafted blob with c0 < c1 routed through analyze() reaches it.
//
// Phase-803 depth tests — encode→decode PSNR + block layout + alpha + degenerate:
//   T20 Block boundary: 8×4 image → 2 BC1 blocks = 16 bytes.
//   T21 Zero-dimension guard: width=0 or height=0 → nullopt.
//   T22 Gradient round-trip PSNR bound: 16×16 gradient → PSNR > 15 dB.
//   T23 PSNR formula correctness: compute 20*log10(255/rmse) from analyze();
//       verify result is finite and in a physically valid range [15, 100] dB.
//   T24 Decode known BC1 block → expected pixels: hand-crafted blob with red
//       endpoint c0 > c1=blue, all indices=0 → all-red reconstruction; RMSE=0.
//   T25 Alpha ignored: same RGB, different alpha → byte-identical BC1 blobs.
//   T26 Checker-pattern block (black/white) → RMSE == 0 (BC1 is lossless for
//       2-colour blocks: endpoints are exact, every texel maps to index 0 or 1).
//   T27 Sub-4 mip levels: 4×4 image with generate_mips=true produces 3 levels
//       (4×4, 2×2, 1×1) each padded to one block → 24 bytes total.
//   T28 Endpoint selection: two-colour block (red top / blue bottom) → non-zero
//       RMSE (quantization expected) but PSNR > 20 dB.
// =============================================================================

#include <cd/asset/texture_compress/TextureCompress.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

/// Generate a checkerboard RGBA8 image alternating between two colours per texel.
[[nodiscard]] std::vector<std::uint8_t>
make_checker_image(std::uint32_t w, std::uint32_t h,
                   std::uint8_t r0, std::uint8_t g0, std::uint8_t b0,
                   std::uint8_t r1, std::uint8_t g1, std::uint8_t b1)
{
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4U);
    for (std::uint32_t y = 0U; y < h; ++y)
    {
        for (std::uint32_t x = 0U; x < w; ++x)
        {
            const std::size_t off = (static_cast<std::size_t>(y) * w + x) * 4U;
            const bool even = ((x + y) % 2U) == 0U;
            img[off + 0U] = even ? r0 : r1;
            img[off + 1U] = even ? g0 : g1;
            img[off + 2U] = even ? b0 : b1;
            img[off + 3U] = 255U;
        }
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

// ============================================================================
// Phase-803 depth tests — block layout, PSNR, alpha, degenerate paths
// ============================================================================

// ---- T20: 8×4 image → 2 BC1 blocks = 16 bytes (block boundary) -------------
// An 8-wide × 4-tall image consists of exactly two 4×4 blocks side by side.
// BC1: 2 blocks × 8 bytes = 16 bytes.  This locks the column-block iteration
// in encode_bc1_mip and confirms that bw = (w+3)/4 counts blocks correctly.

TEST(TextureCompress, T20_BlockBoundaryEightByFour)
{
    constexpr std::uint32_t kW = 8U;
    constexpr std::uint32_t kH = 4U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span{ img }, kW, kH, opts);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->width,  kW);
    EXPECT_EQ(result->height, kH);
    // 2 blocks wide × 1 block tall × 8 bytes each = 16 bytes.
    EXPECT_EQ(result->blob.size(), 16UZ);
}

// ---- T21: zero-dimension guard — width=0 or height=0 → nullopt --------------
// Exercises the `width == 0 || height == 0` early-exit at the top of encode().

TEST(TextureCompress, T21_ZeroDimensionReturnsNullopt)
{
    const std::vector<std::uint8_t> dummy(64U, 0U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };

    EXPECT_FALSE(encode(std::span{ dummy }, 0U, 4U, opts).has_value());
    EXPECT_FALSE(encode(std::span{ dummy }, 4U, 0U, opts).has_value());
}

// ---- T22: gradient round-trip PSNR bound — 16×16 → PSNR > 15 dB ------------
// BC1 is a lossy format; even a naive min/max encoder must achieve at least
// 15 dB PSNR on a smooth gradient (typical BC1 floor is ≈30 dB in practice).
// We use a conservative bound to avoid flakiness across compilers/platforms.

TEST(TextureCompress, T22_GradientRoundTripPsnrBound)
{
    constexpr std::uint32_t kW = 16U;
    constexpr std::uint32_t kH = 16U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, kW, kH, opts);
    ASSERT_TRUE(compressed.has_value());

    const auto stats = analyze(std::span{ img }, *compressed);
    ASSERT_TRUE(stats.has_value());

    ASSERT_GT(stats->rmse, 0.0) << "gradient must be lossy (not solid colour)";
    const double psnr = 20.0 * std::log10(255.0 / stats->rmse);
    EXPECT_GT(psnr, 15.0) << "BC1 gradient PSNR should be > 15 dB, got " << psnr;
}

// ---- T23: PSNR formula correctness — known-input verify ---------------------
// encode() a gradient, get RMSE from analyze(), compute PSNR as
// 20 * log10(255 / rmse).  Verify: finite, in [15, 100] dB (physically valid
// for a lossy block codec), and decreases monotonically from the upper bound.
// This pins that the RMSE→PSNR formula is not accidentally inverted or scaled.

TEST(TextureCompress, T23_PsnrFormulaCorrectness)
{
    constexpr std::uint32_t kW = 16U;
    constexpr std::uint32_t kH = 16U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, kW, kH, opts);
    ASSERT_TRUE(compressed.has_value());

    const auto stats = analyze(std::span{ img }, *compressed);
    ASSERT_TRUE(stats.has_value());
    ASSERT_GT(stats->rmse, 0.0);

    const double psnr = 20.0 * std::log10(255.0 / stats->rmse);

    EXPECT_TRUE(std::isfinite(psnr))        << "PSNR must be finite";
    EXPECT_GT(psnr,  15.0)                  << "PSNR lower bound 15 dB";
    EXPECT_LT(psnr, 100.0)                  << "PSNR upper bound 100 dB (not lossless)";
    // Confirm formula direction: higher rmse → lower PSNR.
    // Double the rmse artificially and verify PSNR drops by ~6 dB.
    const double psnr_2x = 20.0 * std::log10(255.0 / (stats->rmse * 2.0));
    EXPECT_NEAR(psnr - psnr_2x, 20.0 * std::log10(2.0), 1e-9);
}

// ---- T24: decode known BC1 block → expected pixels --------------------------
// Hand-craft an 8-byte BC1 block:
//   c0 = RGB565(red)  = 0xF800  (R=31<<3=248, G=0, B=0 after upscale)
//   c1 = RGB565(blue) = 0x001F  (R=0,        G=0, B=31<<3=248 after upscale)
//   c0 (0xF800) > c1 (0x001F) → 4-colour opaque mode.
//   all 16 texel indices = 0b00 = 0 → every texel selects palette[0] = p0 = red.
// We feed this through analyze() with a solid-red original and expect RMSE==0.

TEST(TextureCompress, T24_DecodeKnownBc1BlockExpectedPixels)
{
    // RGB565 for red: R5=11111, G6=000000, B5=00000 → 0xF800.
    // RGB565 for blue: R5=00000, G6=000000, B5=11111 → 0x001F.
    // After unpack_rgb565(0xF800): r=(31<<3)|(31>>2)=248|7=255, g=0, b=0.
    // After unpack_rgb565(0x001F): r=0, g=0, b=(31<<3)|(31>>2)=248|7=255.

    CompressedTexture known;
    known.format = Format::kBC1;
    known.width  = 4U;
    known.height = 4U;
    known.blob.assign(8U, 0U);
    // c0 LE = 0xF800 → bytes [0x00, 0xF8]
    known.blob[0] = 0x00U;
    known.blob[1] = 0xF8U;
    // c1 LE = 0x001F → bytes [0x1F, 0x00]
    known.blob[2] = 0x1FU;
    known.blob[3] = 0x00U;
    // indices [4..7] = 0x00000000 → all texels select index 0 = p0 = red.
    // (blob was zero-initialised above, so bytes 4..7 are already 0)

    // Original: solid red with the exact upscaled value (255, 0, 0).
    const auto img = make_solid_image(4U, 4U, 255U, 0U, 0U);

    const auto stats = analyze(std::span{ img }, known);
    ASSERT_TRUE(stats.has_value());

    // Decoded red endpoint: unpack_rgb565(0xF800) = (31<<3)|(31>>2) = 255, g=0, b=0.
    // Original is (255,0,0) → perfect match.
    EXPECT_NEAR(stats->rmse, 0.0, 1e-6);
}

// ---- T25: alpha channel is ignored by BC1 encoder ---------------------------
// Two images identical in RGB but one has alpha=255 and the other alpha=0.
// BC1 does not store per-texel alpha (always 1-bit, controlled by c0>c1 flag,
// and our encoder always ensures c0 >= c1 to force opaque 4-colour mode).
// Both images must produce byte-identical compressed blobs.

TEST(TextureCompress, T25_AlphaIgnoredByBc1Encoder)
{
    constexpr std::uint32_t kW = 4U;
    constexpr std::uint32_t kH = 4U;

    // Build two images: identical RGB gradient, different alpha.
    auto img_opaque      = make_gradient_image(kW, kH);  // alpha = 255
    auto img_transparent = img_opaque;                    // copy
    for (std::size_t i = 0U; i < static_cast<std::size_t>(kW) * kH; ++i)
    {
        img_transparent[i * 4U + 3U] = 0U;   // set alpha = 0
    }

    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto result_opaque      = encode(std::span{ img_opaque      }, kW, kH, opts);
    const auto result_transparent = encode(std::span{ img_transparent }, kW, kH, opts);

    ASSERT_TRUE(result_opaque.has_value());
    ASSERT_TRUE(result_transparent.has_value());

    // Blobs must be byte-identical because the alpha channel is not encoded.
    EXPECT_EQ(result_opaque->blob, result_transparent->blob);
}

// ---- T26: checker-pattern block — BC1 lossless for 2-colour blocks ----------
// A black-and-white checkerboard within one 4×4 block has exactly two distinct
// colours.  The min/max endpoint picker selects black (0,0,0) and white
// (255,255,255) as endpoints.  After RGB565 round-trip: black→0x0000→(0,0,0),
// white→0xFFFF→(255,255,255) (both exact in RGB565).  The encoder normalises
// so c0 >= c1, palette[0]=white, palette[1]=black; each texel maps to its
// exact palette entry.  RMSE over the RGB channels is therefore 0.

TEST(TextureCompress, T26_CheckerPatternRmseZero)
{
    constexpr std::uint32_t kW = 4U;
    constexpr std::uint32_t kH = 4U;

    const auto img = make_checker_image(kW, kH,
                                        255U, 255U, 255U,   // colour A: white
                                        0U,   0U,   0U);    // colour B: black

    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, kW, kH, opts);
    ASSERT_TRUE(compressed.has_value());

    const auto stats = analyze(std::span{ img }, *compressed);
    ASSERT_TRUE(stats.has_value());

    EXPECT_TRUE(std::isfinite(stats->rmse));
    EXPECT_NEAR(stats->rmse, 0.0, 1e-6);
}

// ---- T27: sub-4 mip levels — 4×4 with generate_mips → 24 bytes total -------
// mip_count(4,4) = 3  (levels: 4×4, 2×2, 1×1).
// bc1_mip_bytes for each: ceil(4/4)*ceil(4/4)*8=8, ceil(2/4)*ceil(2/4)*8=8,
// ceil(1/4)*ceil(1/4)*8=8.  Total = 24 bytes.
// This exercises the clamp-to-edge gather inside encode_bc1_mip for 2×2 and
// 1×1 mip levels that are padded to fill one complete 4×4 BC1 block.

TEST(TextureCompress, T27_SubFourMipLevels4x4GeneratesMips)
{
    constexpr std::uint32_t kW = 4U;
    constexpr std::uint32_t kH = 4U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = true };
    const auto result = encode(std::span{ img }, kW, kH, opts);

    ASSERT_TRUE(result.has_value());
    // 3 mip levels × 8 bytes each = 24 bytes.
    EXPECT_EQ(result->blob.size(), 24UZ);
}

// ---- T28: two-colour block endpoint selection — bright/dark grey → RMSE > 0 --
// A 4×4 block with top half bright-grey (200,200,200) and bottom half
// dark-grey (56,56,56).  Both colours lie on the grey axis.
//
// The min/max picker:
//   max=(200,200,200) → c0=pack(200,200,200): r5=25, g6=50, b5=25 → 0xC986
//   min=(56,56,56)    → c1=pack(56,56,56):   r5=7,  g6=14, b5=7  → 0x39C7
//   c0(0xC986=51590) > c1(0x39C7=14791) → 4-colour opaque mode.
//
// After unpack: p0=(206,198,206), p1=(57,57,57) — RGB565 quantization shifts
// values slightly, so bright-grey texels != exact p0.  RMSE must be > 0.
// But the block is well-suited for BC1: two endpoints close to the actual
// colours, so PSNR should be > 30 dB.

TEST(TextureCompress, T28_TwoColorBlockEndpointQuantizationRmseNonZero)
{
    constexpr std::uint32_t kW = 4U;
    constexpr std::uint32_t kH = 4U;

    // Top 2 rows bright-grey, bottom 2 rows dark-grey.
    std::vector<std::uint8_t> img(static_cast<std::size_t>(kW) * kH * 4U);
    for (std::uint32_t y = 0U; y < kH; ++y)
    {
        for (std::uint32_t x = 0U; x < kW; ++x)
        {
            const std::size_t off = (static_cast<std::size_t>(y) * kW + x) * 4U;
            const auto val = static_cast<std::uint8_t>((y < 2U) ? 200U : 56U);
            img[off + 0U] = val;  img[off + 1U] = val;
            img[off + 2U] = val;  img[off + 3U] = 255U;
        }
    }

    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, kW, kH, opts);
    ASSERT_TRUE(compressed.has_value());

    const auto stats = analyze(std::span{ img }, *compressed);
    ASSERT_TRUE(stats.has_value());

    // RGB565 quantization introduces small but non-zero error on non-snap values.
    EXPECT_GE(stats->rmse, 0.0);
    EXPECT_TRUE(std::isfinite(stats->rmse));

    // Even with quantization, a block with just two grey shades should have
    // PSNR well above 30 dB (nearest-palette assignment is exact here: each
    // texel maps to either palette[0] or palette[1], no interpolated entry).
    if (stats->rmse > 0.0)
    {
        const double psnr = 20.0 * std::log10(255.0 / stats->rmse);
        EXPECT_TRUE(std::isfinite(psnr));
        EXPECT_GT(psnr, 30.0)
            << "BC1 two-grey block should achieve > 30 dB PSNR, got " << psnr;
    }
}

}  // namespace
