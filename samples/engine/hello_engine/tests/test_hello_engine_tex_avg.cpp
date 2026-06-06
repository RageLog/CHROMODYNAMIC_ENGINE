// =============================================================================
// CHROMODYNAMIC -- samples/engine/hello_engine/tests/test_hello_engine_tex_avg.cpp
//
// phase822-rt-chrome-sponza-tex-avg-extract: unit tests for the
// `compute_texture_average_alpha_weighted` helper extracted from
// HelloGltf.hpp into a free function.
//
// The helper drives phase796's per-prim base_color_factor fold
// (see ADR W8-BD). Wrong output here = chrome reflections paint
// the wrong colour for every Sponza prim — the same user-visible
// bug class that drove Marathon Run 16.
// =============================================================================

#include "../HelloTextureAverage.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{

// Fill a width x height RGBA8 buffer with a single colour + alpha.
[[nodiscard]] std::vector<std::uint8_t>
solid_rgba(std::uint32_t w, std::uint32_t h,
           std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4U);
    for (std::size_t i = 0; i < px.size(); i += 4)
    {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    return px;
}

}  // namespace

// ===========================================================================
// Empty / zero-size guard cases.
// ===========================================================================

TEST(HelloGltfTexAvg, EmptyBufferReturnsNullopt)
{
    auto result =
        cd_sample::compute_texture_average_alpha_weighted({}, 4U, 4U);
    EXPECT_FALSE(result.has_value());
}

TEST(HelloGltfTexAvg, ZeroWidthOrHeightReturnsNullopt)
{
    const auto px = solid_rgba(4U, 4U, 200, 100, 50, 255);
    auto r0 = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() }, 0U, 4U);
    EXPECT_FALSE(r0.has_value());

    auto r1 = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() }, 4U, 0U);
    EXPECT_FALSE(r1.has_value());
}

TEST(HelloGltfTexAvg, ZeroStrideReturnsNullopt)
{
    const auto px = solid_rgba(4U, 4U, 200, 100, 50, 255);
    auto result = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() }, 4U, 4U, 0U);
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// Fully transparent → sum_alpha == 0 → nullopt.
// ===========================================================================

TEST(HelloGltfTexAvg, AllZeroAlphaReturnsNullopt)
{
    // 32x32 (>= one full stride step) of "red" with zero alpha.
    const auto px = solid_rgba(32U, 32U, 255, 0, 0, 0);
    auto result = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() },
        32U, 32U, 16U);
    EXPECT_FALSE(result.has_value())
        << "fully-transparent buffer should not produce an average";
}

// ===========================================================================
// Solid colours: the average must equal the input (with alpha=1).
// ===========================================================================

TEST(HelloGltfTexAvg, SolidRedFullAlphaReturnsRed)
{
    const auto px = solid_rgba(32U, 32U, 217, 31, 20, 255);   // ~(0.85, 0.12, 0.08)
    auto result = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() },
        32U, 32U, 16U);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR((*result)[0], 217.0F / 255.0F, 1e-4F);
    EXPECT_NEAR((*result)[1],  31.0F / 255.0F, 1e-4F);
    EXPECT_NEAR((*result)[2],  20.0F / 255.0F, 1e-4F);
    EXPECT_FLOAT_EQ((*result)[3], 1.0F);
}

TEST(HelloGltfTexAvg, SolidGreenFullAlphaReturnsGreen)
{
    const auto px = solid_rgba(32U, 32U, 38, 153, 25, 255);   // ~(0.15, 0.60, 0.10)
    auto result = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() },
        32U, 32U, 16U);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR((*result)[0],  38.0F / 255.0F, 1e-4F);
    EXPECT_NEAR((*result)[1], 153.0F / 255.0F, 1e-4F);
    EXPECT_NEAR((*result)[2],  25.0F / 255.0F, 1e-4F);
}

TEST(HelloGltfTexAvg, SolidBlueFullAlphaReturnsBlue)
{
    const auto px = solid_rgba(32U, 32U, 20, 64, 200, 255);
    auto result = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() },
        32U, 32U, 16U);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR((*result)[0],  20.0F / 255.0F, 1e-4F);
    EXPECT_NEAR((*result)[1],  64.0F / 255.0F, 1e-4F);
    EXPECT_NEAR((*result)[2], 200.0F / 255.0F, 1e-4F);
}

// ===========================================================================
// Alpha-weighted mean: half-opaque red + half-fully-transparent must
// produce red — the transparent pixels carry zero weight.
// ===========================================================================

TEST(HelloGltfTexAvg, HalfTransparentSinglesPixelDownToRedOnly)
{
    // 32x32 buffer: top half opaque red (alpha=255), bottom half fully
    // transparent (alpha=0). Stride 16 lands on rows 0 and 16 — one
    // row each from the opaque + transparent halves.
    std::vector<std::uint8_t> px(static_cast<std::size_t>(32U) * 32U * 4U);
    for (std::uint32_t y = 0; y < 32U; ++y)
    {
        const bool   top      = (y < 16U);
        const std::uint8_t a  = top ? 255U : 0U;
        for (std::uint32_t x = 0; x < 32U; ++x)
        {
            const std::size_t off = (static_cast<std::size_t>(y) * 32U + x) * 4U;
            px[off + 0] = 255U;
            px[off + 1] = 0U;
            px[off + 2] = 0U;
            px[off + 3] = a;
        }
    }
    auto result = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() },
        32U, 32U, 16U);
    ASSERT_TRUE(result.has_value());
    // Opaque half contributes 100% of the alpha-weight; transparent
    // half contributes 0. So the mean is pure red.
    EXPECT_NEAR((*result)[0], 1.0F, 1e-4F);
    EXPECT_NEAR((*result)[1], 0.0F, 1e-4F);
    EXPECT_NEAR((*result)[2], 0.0F, 1e-4F);
}

// ===========================================================================
// Stride sanity: a 1x1 buffer with stride=16 still samples its single
// pixel because (0, 0) hits regardless of stride.
// ===========================================================================

TEST(HelloGltfTexAvg, OnePixelBufferWithLargeStrideStillSamples)
{
    const auto px = solid_rgba(1U, 1U, 100, 150, 200, 255);
    auto result = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() }, 1U, 1U, 16U);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR((*result)[0], 100.0F / 255.0F, 1e-4F);
    EXPECT_NEAR((*result)[1], 150.0F / 255.0F, 1e-4F);
    EXPECT_NEAR((*result)[2], 200.0F / 255.0F, 1e-4F);
}

// ===========================================================================
// Smaller stride samples more pixels → finer detail. With a 2-colour
// checkerboard, stride 1 must average exactly to the mid-grey midpoint.
// ===========================================================================

// ===========================================================================
// fold_texture_avg_into_factor — the second-half of the chain that puts
// (factor * texture-average) into the SSBO. Test the composition itself.
// ===========================================================================

TEST(HelloGltfTexAvg_Fold, NulloptAvgReturnsFactorUnchanged)
{
    const std::array<float, 4> factor { 0.5F, 0.7F, 0.9F, 1.0F };
    const auto result =
        cd_sample::fold_texture_avg_into_factor(factor, std::nullopt);
    EXPECT_FLOAT_EQ(result[0], 0.5F);
    EXPECT_FLOAT_EQ(result[1], 0.7F);
    EXPECT_FLOAT_EQ(result[2], 0.9F);
    EXPECT_FLOAT_EQ(result[3], 1.0F);
}

TEST(HelloGltfTexAvg_Fold, IdentityFactorReturnsTextureAvg)
{
    // The canonical Sponza case: factor=(1,1,1) with the real colour in
    // the texture. Folding must return the texture-average verbatim.
    const std::array<float, 4>                factor { 1.0F, 1.0F, 1.0F, 1.0F };
    const std::optional<std::array<float, 4>> avg {
        std::array<float, 4> { 0.85F, 0.12F, 0.08F, 1.0F }   // red curtain
    };
    const auto result =
        cd_sample::fold_texture_avg_into_factor(factor, avg);
    EXPECT_NEAR(result[0], 0.85F, 1e-6F);
    EXPECT_NEAR(result[1], 0.12F, 1e-6F);
    EXPECT_NEAR(result[2], 0.08F, 1e-6F);
    EXPECT_FLOAT_EQ(result[3], 1.0F);
}

TEST(HelloGltfTexAvg_Fold, NonIdentityFactorMultipliesChannelWise)
{
    // Factor (0.5, 1.0, 2.0) applied to texture (0.6, 0.4, 0.2) →
    // (0.30, 0.40, 0.40). Alpha pass-through stays at factor's alpha.
    const std::array<float, 4>                factor { 0.5F, 1.0F, 2.0F, 0.75F };
    const std::optional<std::array<float, 4>> avg {
        std::array<float, 4> { 0.6F, 0.4F, 0.2F, 1.0F }
    };
    const auto result =
        cd_sample::fold_texture_avg_into_factor(factor, avg);
    EXPECT_NEAR(result[0], 0.30F, 1e-6F);
    EXPECT_NEAR(result[1], 0.40F, 1e-6F);
    EXPECT_NEAR(result[2], 0.40F, 1e-6F);
    EXPECT_FLOAT_EQ(result[3], 0.75F);
}

TEST(HelloGltfTexAvg_Fold, AlphaChannelComesFromFactorNotTexture)
{
    // Lock the alpha-pass-through contract — caller's factor.a is the
    // only alpha that lands in the SSBO; the texture average's alpha
    // is collapsed to 1.0 at compute time anyway, but a future change
    // shouldn't silently start propagating it.
    const std::array<float, 4>                factor { 1.0F, 1.0F, 1.0F, 0.4F };
    const std::optional<std::array<float, 4>> avg {
        std::array<float, 4> { 0.5F, 0.5F, 0.5F, 1.0F }
    };
    const auto result =
        cd_sample::fold_texture_avg_into_factor(factor, avg);
    EXPECT_FLOAT_EQ(result[3], 0.4F);
}

TEST(HelloGltfTexAvg, CheckerboardStrideOneAveragesToMidpoint)
{
    // 4x4 RGB checkerboard, even cells = (255, 0, 0), odd cells =
    // (0, 0, 255), all opaque.
    std::vector<std::uint8_t> px(static_cast<std::size_t>(4U) * 4U * 4U);
    for (std::uint32_t y = 0; y < 4U; ++y)
    {
        for (std::uint32_t x = 0; x < 4U; ++x)
        {
            const std::size_t off = (static_cast<std::size_t>(y) * 4U + x) * 4U;
            const bool red = ((x + y) % 2U) == 0U;
            px[off + 0] = red ? 255U : 0U;
            px[off + 1] = 0U;
            px[off + 2] = red ? 0U   : 255U;
            px[off + 3] = 255U;
        }
    }
    auto result = cd_sample::compute_texture_average_alpha_weighted(
        std::span<const std::uint8_t> { px.data(), px.size() }, 4U, 4U, 1U);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR((*result)[0], 127.5F / 255.0F, 1e-3F);  // 8 red / 16 total
    EXPECT_NEAR((*result)[1],   0.0F,          1e-4F);
    EXPECT_NEAR((*result)[2], 127.5F / 255.0F, 1e-3F);
}
