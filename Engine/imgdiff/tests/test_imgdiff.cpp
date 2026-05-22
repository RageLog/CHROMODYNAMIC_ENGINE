// =============================================================================
// CHROMODYNAMIC — cd::imgdiff tests
// =============================================================================
#include <cd/imgdiff/Gaussian.hpp>
#include <cd/imgdiff/ImageDiff.hpp>
#include <cd/imgdiff/Ssim.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{

[[nodiscard]] std::vector<std::uint8_t> solid(std::uint32_t w, std::uint32_t h, std::uint8_t r,
                                              std::uint8_t g, std::uint8_t b, std::uint8_t a)
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

TEST(ImageDiff, IdenticalImagesReturnZeroDifferences)
{
    const auto img = solid(4, 4, 128, 64, 200, 255);
    const cd::imgdiff::ImageView v { img.data(), 4, 4 };
    auto r = cd::imgdiff::compare(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->pixel_count, 16U);
    EXPECT_EQ(r->different_pixels, 0U);
    EXPECT_EQ(r->max_abs_delta_r, 0U);
    EXPECT_DOUBLE_EQ(r->rmse, 0.0);
    EXPECT_DOUBLE_EQ(r->mean_abs_delta, 0.0);
    EXPECT_GE(r->psnr_db, 100.0);  // sentinel high value for identical images
    EXPECT_TRUE(cd::imgdiff::passes(*r));
}

TEST(ImageDiff, SinglePixelDifferenceIsDetected)
{
    auto a = solid(2, 2, 0, 0, 0, 255);
    auto b = a;
    b[0] = 255;  // bump R channel of pixel (0,0)
    const cd::imgdiff::ImageView va { a.data(), 2, 2 };
    const cd::imgdiff::ImageView vb { b.data(), 2, 2 };
    auto r = cd::imgdiff::compare(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->pixel_count, 4U);
    EXPECT_EQ(r->different_pixels, 1U);
    EXPECT_EQ(r->max_abs_delta_r, 255U);
    EXPECT_EQ(r->max_abs_delta_g, 0U);
    EXPECT_FALSE(cd::imgdiff::passes(*r));
    EXPECT_TRUE(cd::imgdiff::passes(*r, /*max_failures=*/1));
}

TEST(ImageDiff, ToleranceMasksSmallNoise)
{
    auto a = solid(8, 8, 100, 100, 100, 255);
    auto b = solid(8, 8, 102, 99, 100, 255);  // ±2/±1 LSB roundoff
    const cd::imgdiff::ImageView va { a.data(), 8, 8 };
    const cd::imgdiff::ImageView vb { b.data(), 8, 8 };

    // Without tolerance every pixel counts as a difference.
    auto strict = cd::imgdiff::compare(va, vb, 0);
    ASSERT_TRUE(strict.has_value());
    EXPECT_EQ(strict->different_pixels, 64U);

    // With a 2-LSB tolerance, none of the noise crosses the threshold.
    auto loose = cd::imgdiff::compare(va, vb, 2);
    ASSERT_TRUE(loose.has_value());
    EXPECT_EQ(loose->different_pixels, 0U);
    EXPECT_TRUE(cd::imgdiff::passes(*loose));
}

TEST(ImageDiff, DimensionMismatchReturnsError)
{
    const auto a = solid(4, 4, 0, 0, 0, 255);
    const auto b = solid(8, 4, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 4, 4 };
    const cd::imgdiff::ImageView vb { b.data(), 8, 4 };
    auto r = cd::imgdiff::compare(va, vb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kDimensionMismatch));
}

TEST(ImageDiff, NullPointerReturnsError)
{
    const auto a = solid(4, 4, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 4, 4 };
    const cd::imgdiff::ImageView vb { nullptr, 4, 4 };
    auto r = cd::imgdiff::compare(va, vb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kNullPointer));
}

TEST(ImageDiff, HighlightPaintsDifferingPixelsRed)
{
    auto a = solid(2, 1, 128, 128, 128, 255);
    auto b = a;
    b[0] = 0;
    b[1] = 0;
    b[2] = 0;  // pixel (0,0) becomes black — clearly differs
    const cd::imgdiff::ImageView va { a.data(), 2, 1 };
    const cd::imgdiff::ImageView vb { b.data(), 2, 1 };
    auto h = cd::imgdiff::highlight(va, vb);
    ASSERT_TRUE(h.has_value());
    ASSERT_EQ(h->size(), 8U);  // 2 pixels * 4 channels
    // Pixel 0 differs → opaque red.
    EXPECT_EQ((*h)[0], 255U);
    EXPECT_EQ((*h)[1], 0U);
    EXPECT_EQ((*h)[2], 0U);
    EXPECT_EQ((*h)[3], 255U);
    // Pixel 1 matches → darkened (128/2 = 64).
    EXPECT_EQ((*h)[4], 64U);
    EXPECT_EQ((*h)[5], 64U);
    EXPECT_EQ((*h)[6], 64U);
    EXPECT_EQ((*h)[7], 255U);
}

TEST(ImageDiff, PsnrIncreasesAsImagesConverge)
{
    const auto a = solid(4, 4, 100, 100, 100, 255);
    auto b_small = a;
    b_small[0] = 101;  // single LSB diff
    auto b_big = a;
    for (std::size_t i = 0; i < b_big.size(); i += 4)
        b_big[i] = 200;  // half-range deviation in R channel

    const cd::imgdiff::ImageView va { a.data(), 4, 4 };
    const cd::imgdiff::ImageView vbs { b_small.data(), 4, 4 };
    const cd::imgdiff::ImageView vbb { b_big.data(), 4, 4 };

    auto small_diff = cd::imgdiff::compare(va, vbs);
    auto big_diff = cd::imgdiff::compare(va, vbb);
    ASSERT_TRUE(small_diff.has_value());
    ASSERT_TRUE(big_diff.has_value());
    EXPECT_GT(small_diff->psnr_db, big_diff->psnr_db);  // less error = higher PSNR
}

// -----------------------------------------------------------------------------
// SSIM-lite — Wave 39
// -----------------------------------------------------------------------------

TEST(SsimLite, IdenticalImagesScoreOne)
{
    const auto img = solid(16, 16, 100, 150, 200, 255);
    const cd::imgdiff::ImageView v { img.data(), 16, 16 };
    auto r = cd::imgdiff::compute_ssim_lite(v, v, 8);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->windows, 4U);  // 2x2 windows of 8x8 in a 16x16 image
    EXPECT_NEAR(r->mean_ssim, 1.0, 1e-9);
    EXPECT_NEAR(r->min_ssim, 1.0, 1e-9);
    EXPECT_TRUE(cd::imgdiff::ssim_passes(*r));
}

TEST(SsimLite, BigDifferenceDropsScore)
{
    // Half-image swap: top half black, bottom half white in B; A is
    // mid-grey everywhere. Luminance differs strongly.
    std::vector<std::uint8_t> a(16 * 16 * 4U, 128);
    for (std::size_t i = 3; i < a.size(); i += 4)
        a[i] = 255;  // alpha
    std::vector<std::uint8_t> b = a;
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const std::size_t i = (y * 16 + x) * 4;
            b[i + 0] = 0;
            b[i + 1] = 0;
            b[i + 2] = 0;
        }
    for (std::uint32_t y = 8; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const std::size_t i = (y * 16 + x) * 4;
            b[i + 0] = 255;
            b[i + 1] = 255;
            b[i + 2] = 255;
        }

    const cd::imgdiff::ImageView va { a.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { b.data(), 16, 16 };
    auto r = cd::imgdiff::compute_ssim_lite(va, vb, 8);
    ASSERT_TRUE(r.has_value());
    EXPECT_LT(r->mean_ssim, 0.9);  // very degraded → low score
    EXPECT_FALSE(cd::imgdiff::ssim_passes(*r));
}

TEST(SsimLite, MeanGreaterOrEqualMin)
{
    // Aggregate invariant: mean across windows >= worst window.
    std::vector<std::uint8_t> a(8 * 8 * 4U, 100);
    for (std::size_t i = 3; i < a.size(); i += 4)
        a[i] = 255;
    auto b = a;
    b[0] = 200;  // perturb one pixel
    const cd::imgdiff::ImageView va { a.data(), 8, 8 };
    const cd::imgdiff::ImageView vb { b.data(), 8, 8 };
    auto r = cd::imgdiff::compute_ssim_lite(va, vb, 4);  // 4x4 windows → 2x2 grid
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r->mean_ssim, r->min_ssim);
    EXPECT_LT(r->mean_ssim, 1.0);
}

TEST(SsimLite, ImageSmallerThanWindowReturnsIdentity)
{
    const auto img = solid(4, 4, 1, 2, 3, 255);
    const cd::imgdiff::ImageView v { img.data(), 4, 4 };
    auto r = cd::imgdiff::compute_ssim_lite(v, v, 8);  // window > image
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->windows, 0U);
    EXPECT_DOUBLE_EQ(r->mean_ssim, 1.0);
    EXPECT_DOUBLE_EQ(r->min_ssim, 1.0);
}

// -----------------------------------------------------------------------------
// Gaussian blur — Wave 54
// -----------------------------------------------------------------------------

TEST(GaussianBlur, IdentityKernelPassesThrough)
{
    // sigma <= 0 → 1-tap kernel = identity.
    const auto img = solid(8, 8, 100, 150, 200, 255);
    const cd::imgdiff::ImageView v { img.data(), 8, 8 };
    auto r = cd::imgdiff::gaussian_blur(v, 0.0);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->size(), img.size());
    // Every pixel must match the source (no kernel taps).
    for (std::size_t i = 0; i < img.size(); ++i)
        EXPECT_EQ((*r)[i], img[i]);
}

TEST(GaussianBlur, ConstantImageStaysConstant)
{
    // A solid color through any Gaussian must equal itself (separable
    // weighted average of the same value is the value).
    const auto img = solid(16, 16, 200, 50, 25, 255);
    const cd::imgdiff::ImageView v { img.data(), 16, 16 };
    auto r = cd::imgdiff::gaussian_blur(v, 2.0);
    ASSERT_TRUE(r.has_value());
    for (std::size_t i = 0; i < img.size(); i += 4)
    {
        EXPECT_EQ((*r)[i + 0], 200U);
        EXPECT_EQ((*r)[i + 1], 50U);
        EXPECT_EQ((*r)[i + 2], 25U);
        EXPECT_EQ((*r)[i + 3], 255U);
    }
}

TEST(GaussianBlur, SinglePixelSpikeSpreads)
{
    // Black image, one white pixel in the center → after blur, the
    // center is lower than 255 and adjacent pixels are above 0.
    std::vector<std::uint8_t> img(8 * 8 * 4, 0);
    for (std::size_t i = 3; i < img.size(); i += 4)
        img[i] = 255;  // alpha = 255 everywhere
    const std::size_t center = (4 * 8 + 4) * 4;
    img[center + 0] = 255;
    img[center + 1] = 255;
    img[center + 2] = 255;
    const cd::imgdiff::ImageView v { img.data(), 8, 8 };
    auto r = cd::imgdiff::gaussian_blur(v, 1.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_LT((*r)[center + 0], 255U);
    EXPECT_GT((*r)[center + 0], 0U);
    // Adjacent pixel right of center must have picked up some intensity.
    const std::size_t right = (4 * 8 + 5) * 4;
    EXPECT_GT((*r)[right + 0], 0U);
}

TEST(GaussianBlur, EmptyImageReturnsError)
{
    const cd::imgdiff::ImageView v { nullptr, 0, 0 };
    auto r = cd::imgdiff::gaussian_blur(v, 1.0);
    ASSERT_FALSE(r.has_value());
}

TEST(GaussianBlur, ImprovesSsimOnNoisyBaseline)
{
    // A baseline with a tiny salt-and-pepper noise pattern should
    // score higher SSIM against itself-blurred than against the noisy
    // original — i.e., the blur produces a perceptually similar
    // image, not a different scene.
    std::vector<std::uint8_t> base(32 * 32 * 4, 128);
    for (std::size_t i = 3; i < base.size(); i += 4)
        base[i] = 255;
    // Pepper 8 pixels with white.
    for (int k = 0; k < 8; ++k)
    {
        const std::size_t i = static_cast<std::size_t>(k * 41 + 7) * 4 % base.size();
        base[i + 0] = 255;
        base[i + 1] = 255;
        base[i + 2] = 255;
    }
    const cd::imgdiff::ImageView v { base.data(), 32, 32 };
    auto blurred = cd::imgdiff::gaussian_blur(v, 1.5);
    ASSERT_TRUE(blurred.has_value());
    const cd::imgdiff::ImageView v2 { blurred->data(), 32, 32 };
    auto s = cd::imgdiff::compute_ssim_lite(v, v2, 8);
    ASSERT_TRUE(s.has_value());
    // Reasonable similarity — not a regression-tight bound, just
    // "blur didn't destroy structure".
    EXPECT_GT(s->mean_ssim, 0.5);
}

// -----------------------------------------------------------------------------
// SSIM Gaussian-weighted (FLIP-lite) — Wave 61
// -----------------------------------------------------------------------------

TEST(SsimGaussian, IdenticalImagesScoreOne)
{
    const auto img = solid(32, 32, 90, 140, 210, 255);
    const cd::imgdiff::ImageView v { img.data(), 32, 32 };
    auto r = cd::imgdiff::compute_ssim_gaussian(v, v, 1.5, 8);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->mean_ssim, 1.0, 1e-9);
    EXPECT_GE(r->windows, 1U);
}

TEST(SsimGaussian, BigDifferenceDropsScoreBelowOne)
{
    auto a = solid(32, 32, 128, 128, 128, 255);
    auto b = a;
    for (std::size_t y = 0; y < 16; ++y)
        for (std::size_t x = 0; x < 32; ++x)
        {
            const std::size_t i = (y * 32 + x) * 4;
            b[i + 0] = 0;
            b[i + 1] = 0;
            b[i + 2] = 0;
        }
    const cd::imgdiff::ImageView va { a.data(), 32, 32 };
    const cd::imgdiff::ImageView vb { b.data(), 32, 32 };
    auto r = cd::imgdiff::compute_ssim_gaussian(va, vb, 1.5, 4);
    ASSERT_TRUE(r.has_value());
    EXPECT_LT(r->mean_ssim, 0.99);
}

TEST(SsimGaussian, StrideAffectsWindowCount)
{
    const auto img = solid(32, 32, 100, 100, 100, 255);
    const cd::imgdiff::ImageView v { img.data(), 32, 32 };
    auto fine = cd::imgdiff::compute_ssim_gaussian(v, v, 1.5, 1);
    auto coarse = cd::imgdiff::compute_ssim_gaussian(v, v, 1.5, 8);
    ASSERT_TRUE(fine.has_value());
    ASSERT_TRUE(coarse.has_value());
    EXPECT_GT(fine->windows, coarse->windows);
    EXPECT_NEAR(fine->mean_ssim, 1.0, 1e-9);
    EXPECT_NEAR(coarse->mean_ssim, 1.0, 1e-9);
}

TEST(SsimGaussian, DimensionMismatchReturnsError)
{
    const auto a = solid(16, 16, 0, 0, 0, 255);
    const auto b = solid(32, 16, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { b.data(), 32, 16 };
    auto r = cd::imgdiff::compute_ssim_gaussian(va, vb);
    ASSERT_FALSE(r.has_value());
}

TEST(SsimLite, DimensionMismatchReturnsError)
{
    const auto a = solid(8, 8, 0, 0, 0, 255);
    const auto b = solid(16, 8, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 8, 8 };
    const cd::imgdiff::ImageView vb { b.data(), 16, 8 };
    auto r = cd::imgdiff::compute_ssim_lite(va, vb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kDimensionMismatch));
}
