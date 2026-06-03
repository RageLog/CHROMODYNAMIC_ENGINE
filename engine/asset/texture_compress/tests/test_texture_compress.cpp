// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_compress/tests/test_texture_compress.cpp
// Phase 650 — cd::asset::texture_compress unit tests (Sprint-1)
//
// Tests:
//   T1  encode() round-trip: 4×4 synthetic image → BC1 → non-empty blob
//   T2  BC1 compression ratio ≈ 8:1 for RGB (6 bpp → 0.5 bpp, i.e. 8× fewer bytes vs RGBA8)
//       Wait — BC1 = 0.5 bpp = 4 bits/pixel.  RGBA8 = 32 bpp.  Ratio = 32/4 = 8.
//       blob_bytes = (width * height) / 2.  ratio = (w*h*4) / (w*h/2) = 8.0
//   T3  bad input rejected: wrong pixel count returns nullopt
//   T4  bad input rejected: dimensions not multiples of 4 returns nullopt
//   T5  bad input rejected: unimplemented format (kBC7) returns nullopt
//   T6  mip generation: generate_mips=true produces more bytes than mip 0 alone
//   T7  mip count: 8×8 image with generate_mips produces 3 mip levels
//       (8→4→2→1 : 4 levels for max(8,8)=8, but BC1 needs 4-multiple dims;
//        mip levels: 8×8 (1), 4×4 (2), 2×2 padded (3), 1×1 padded (4) = 4 mips)
//   T8  analyze() returns ratio ≈ 8.0 for a 4×4 BC1-encoded image
//   T9  analyze() returns nullopt for bad input (mismatched pixel count)
//   T10 analyze() RMSE is finite and non-negative
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

using namespace cd::asset::texture_compress;

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
    // BC1 for an N×M image:
    //   input  = N*M*4 bytes
    //   output = (N/4)*(M/4)*8 bytes = N*M/2 bytes
    //   ratio  = (N*M*4) / (N*M/2) = 8.0
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
    // Provide only 3 bytes for a 4×4 image (needs 64).
    const std::array<std::uint8_t, 3> tiny{ 0U, 0U, 0U };
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span<const std::uint8_t>{ tiny }, 4U, 4U, opts);
    EXPECT_FALSE(result.has_value());
}

// ---- T4: bad input — dimensions not multiples of 4 returns nullopt ---------

TEST(TextureCompress, T4_NonMultipleDimensionsReturnsNullopt)
{
    // 5×5 image — neither dimension is a multiple of 4 for BC1.
    const std::vector<std::uint8_t> img(5U * 5U * 4U, 128U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span{ img }, 5U, 5U, opts);
    EXPECT_FALSE(result.has_value());
}

// ---- T5: unimplemented format returns nullopt (kBC7 = Sprint-2) ------------

TEST(TextureCompress, T5_UnimplementedFormatReturnsNullopt)
{
    const auto img = make_gradient_image(4U, 4U);
    const EncodeOptions opts{ .target = Format::kBC7, .quality = 128U, .generate_mips = false };
    const auto result = encode(std::span{ img }, 4U, 4U, opts);
    EXPECT_FALSE(result.has_value());
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

// ---- T7: mip count — 16×16 with generate_mips produces 4 levels ------------
// Mip chain for 16×16: 16→8→4→2→1 = 5 mips in terms of sizes,
// but each mip must be >= 1×1; BC1 encodes them with edge-pad.
// Expected blob = sum of bc1_mip_bytes(16,16)+bc1_mip_bytes(8,8)+...+bc1_mip_bytes(1,1)
// = 128 + 32 + 8 + 8 + 8 = 184 bytes  (1×1 and 2×2 both round up to one 4×4 block = 8 bytes)

TEST(TextureCompress, T7_MipChainByteCount)
{
    constexpr std::uint32_t kW = 16U;
    constexpr std::uint32_t kH = 16U;

    const auto img = make_gradient_image(kW, kH);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = true };
    const auto result = encode(std::span{ img }, kW, kH, opts);

    ASSERT_TRUE(result.has_value());

    // Compute expected bytes for each mip level manually.
    // BC1: ceil(w/4) * ceil(h/4) * 8 bytes.
    // Mips: 16x16, 8x8, 4x4, 2x2, 1x1
    //   16x16: (4*4*8) = 128
    //    8x8:  (2*2*8) =  32
    //    4x4:  (1*1*8) =   8
    //    2x2:  (1*1*8) =   8  (2×2 rounds up to one 4×4 block)
    //    1x1:  (1*1*8) =   8
    // Total: 184
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
    EXPECT_EQ(stats->output_bytes, 8ULL);  // 1 BC1 block
    EXPECT_NEAR(stats->ratio, 8.0, 1e-6);
}

// ---- T9: analyze() returns nullopt for mismatched pixel count ---------------

TEST(TextureCompress, T9_AnalyzeRejectsWrongPixelCount)
{
    const auto img = make_gradient_image(4U, 4U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, 4U, 4U, opts);
    ASSERT_TRUE(compressed.has_value());

    // Provide wrong-size pixel data (too small).
    const std::array<std::uint8_t, 4U> wrong{ 0U, 0U, 0U, 255U };
    const auto stats = analyze(std::span<const std::uint8_t>{ wrong }, *compressed);
    EXPECT_FALSE(stats.has_value());
}

// ---- T10: analyze() RMSE == 0 for solid-colour input (lossless for solid blocks) --

TEST(TextureCompress, T10_AnalyzeRmseZeroForSolidColor)
{
    // A solid-colour block has both endpoints equal → BC1 encodes it losslessly
    // (all texels map to index 0 = c0 = the single colour). RMSE must be 0.
    constexpr std::uint32_t kW = 4U;
    constexpr std::uint32_t kH = 4U;

    // Use a colour that survives RGB565 round-trip without loss: R=248, G=252, B=248
    // R=248 → R5=31 → R=255? No. Use exact 565-representable values:
    // R5=15 → R=(15<<3)|(15>>2)=120+3=123? Let's use R=0,G=0,B=0 (trivially lossless).
    const auto img = make_solid_image(kW, kH, 0U, 0U, 0U);
    const EncodeOptions opts{ .target = Format::kBC1, .quality = 128U, .generate_mips = false };
    const auto compressed = encode(std::span{ img }, kW, kH, opts);
    ASSERT_TRUE(compressed.has_value());

    const auto stats = analyze(std::span{ img }, *compressed);
    ASSERT_TRUE(stats.has_value());

    EXPECT_GE(stats->rmse, 0.0);
    EXPECT_TRUE(std::isfinite(stats->rmse));
    EXPECT_NEAR(stats->rmse, 0.0, 1e-6);  // solid black encodes exactly
}

}  // namespace
