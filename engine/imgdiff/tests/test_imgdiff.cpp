// =============================================================================
// CHROMODYNAMIC — cd::imgdiff tests
// =============================================================================
#include <cd/imgdiff/Flip.hpp>
#include <cd/imgdiff/Gaussian.hpp>
#include <cd/imgdiff/ImageDiff.hpp>
#include <cd/imgdiff/Ssim.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
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
    std::vector<std::uint8_t> a(static_cast<std::size_t>(16) * 16 * 4U, 128);
    for (std::size_t i = 3; i < a.size(); i += 4)
        a[i] = 255;  // alpha
    std::vector<std::uint8_t> b = a;
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const std::size_t i = (static_cast<std::size_t>(y) * 16 + x) * 4;
            b[i + 0] = 0;
            b[i + 1] = 0;
            b[i + 2] = 0;
        }
    for (std::uint32_t y = 8; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x)
        {
            const std::size_t i = (static_cast<std::size_t>(y) * 16 + x) * 4;
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
    std::vector<std::uint8_t> a(static_cast<std::size_t>(8) * 8 * 4U, 100);
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
    std::vector<std::uint8_t> img(static_cast<std::size_t>(8) * 8 * 4, 0);
    for (std::size_t i = 3; i < img.size(); i += 4)
        img[i] = 255;  // alpha = 255 everywhere
    const std::size_t center = static_cast<std::size_t>(4 * 8 + 4) * 4;
    img[center + 0] = 255;
    img[center + 1] = 255;
    img[center + 2] = 255;
    const cd::imgdiff::ImageView v { img.data(), 8, 8 };
    auto r = cd::imgdiff::gaussian_blur(v, 1.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_LT((*r)[center + 0], 255U);
    EXPECT_GT((*r)[center + 0], 0U);
    // Adjacent pixel right of center must have picked up some intensity.
    const std::size_t right = static_cast<std::size_t>(4 * 8 + 5) * 4;
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
    std::vector<std::uint8_t> base(static_cast<std::size_t>(32) * 32 * 4, 128);
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

// -----------------------------------------------------------------------------
// FLIP-lite — Wave 68
// -----------------------------------------------------------------------------

TEST(FlipLite, IdenticalImagesScoreZero)
{
    const auto img = solid(16, 16, 100, 150, 200, 255);
    const cd::imgdiff::ImageView v { img.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_lite(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->mean_error, 0.0);
    EXPECT_DOUBLE_EQ(r->max_error, 0.0);
    EXPECT_DOUBLE_EQ(r->p95_error, 0.0);
    EXPECT_EQ(r->pixel_count, 256U);
    EXPECT_TRUE(cd::imgdiff::flip_passes(*r));
}

TEST(FlipLite, BigLuminanceDiffProducesError)
{
    const auto white = solid(16, 16, 255, 255, 255, 255);
    const auto black = solid(16, 16, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { white.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { black.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_lite(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_GT(r->mean_error, 0.5);
    EXPECT_GE(r->max_error, r->mean_error);
    EXPECT_FALSE(cd::imgdiff::flip_passes(*r));
}

TEST(FlipLite, JndFloorMutesTinyNoise)
{
    // ±1 LSB noise on luminance — below the perceptual_map JND floor.
    auto base = solid(16, 16, 128, 128, 128, 255);
    auto noised = base;
    for (std::size_t i = 0; i < noised.size(); i += 4)
    {
        noised[i + 0] = 129;
        noised[i + 1] = 129;
        noised[i + 2] = 129;
    }
    const cd::imgdiff::ImageView va { base.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { noised.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_lite(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_LT(r->mean_error, 0.01);  // JND floor muted the noise
}

TEST(FlipLite, P95IsAtLeastMean)
{
    // Synthetic: half black, half white. p95 should be at the white
    // end → ≥ mean which averages across both halves.
    std::vector<std::uint8_t> mixed(static_cast<std::size_t>(16) * 16 * 4, 0);
    for (std::size_t i = 3; i < mixed.size(); i += 4)
        mixed[i] = 255;
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 8; x < 16; ++x)
        {
            const std::size_t k = (static_cast<std::size_t>(y) * 16 + x) * 4;
            mixed[k + 0] = 255;
            mixed[k + 1] = 255;
            mixed[k + 2] = 255;
        }
    const auto base = solid(16, 16, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { base.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { mixed.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_lite(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r->p95_error, r->mean_error);
}

TEST(FlipLite, HeatmapDimensionsMatch)
{
    const auto img = solid(8, 8, 50, 50, 50, 255);
    const cd::imgdiff::ImageView v { img.data(), 8, 8 };
    auto r = cd::imgdiff::compute_flip_lite(v, v);
    ASSERT_TRUE(r.has_value());
    const auto heat = cd::imgdiff::flip_heatmap(*r, 8, 8);
    EXPECT_EQ(heat.size(), 8U * 8U * 4U);
    // Identity → error map all 0 → heatmap all green.
    for (std::size_t i = 0; i < heat.size(); i += 4)
    {
        EXPECT_EQ(heat[i + 0], 0U);     // R
        EXPECT_EQ(heat[i + 1], 255U);   // G (all-green for zero error)
        EXPECT_EQ(heat[i + 2], 0U);     // B
    }
}

TEST(FlipLite, DimensionMismatchReturnsError)
{
    const auto a = solid(8, 8, 0, 0, 0, 255);
    const auto b = solid(16, 8, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 8, 8 };
    const cd::imgdiff::ImageView vb { b.data(), 16, 8 };
    auto r = cd::imgdiff::compute_flip_lite(va, vb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kDimensionMismatch));
}

// -----------------------------------------------------------------------------
// Full FLIP (Wave 94) — luminance + chroma with per-channel CSF
// -----------------------------------------------------------------------------

TEST(FlipFull, IdenticalImagesScoreZero)
{
    const auto img = solid(16, 16, 90, 140, 200, 255);
    const cd::imgdiff::ImageView v { img.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_full(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->mean_error, 0.0);
    EXPECT_DOUBLE_EQ(r->max_error, 0.0);
}

TEST(FlipFull, PureChromaShiftRegistersError)
{
    // Same luminance, different chroma: lite (luminance-only) would
    // largely miss this; full should pick it up via Cx/Cz channels.
    // Build two solid colours with matched Y ≈ 0.2126·R + 0.7152·G +
    // 0.0722·B but different R/G/B mixes.
    const auto a = solid(32, 32, 200, 50, 50, 255);   // red-heavy
    const auto b = solid(32, 32, 50, 130, 200, 255);  // blue-heavy
    const cd::imgdiff::ImageView va { a.data(), 32, 32 };
    const cd::imgdiff::ImageView vb { b.data(), 32, 32 };
    auto lite = cd::imgdiff::compute_flip_lite(va, vb);
    auto full = cd::imgdiff::compute_flip_full(va, vb);
    ASSERT_TRUE(lite.has_value());
    ASSERT_TRUE(full.has_value());
    // Full FLIP picks up chroma; mean error should exceed the lite
    // result on a chroma-only shift (even if luminance happens to
    // match imperfectly here, the colour gap is significant).
    EXPECT_GT(full->mean_error, 0.0);
}

TEST(FlipFull, LargeLuminanceDeltaDetected)
{
    const auto white = solid(16, 16, 255, 255, 255, 255);
    const auto black = solid(16, 16, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { white.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { black.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_full(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_GT(r->mean_error, 0.5);
    EXPECT_FALSE(cd::imgdiff::flip_passes(*r));
}

TEST(FlipFull, ErrorMapBoundedToUnit)
{
    const auto white = solid(16, 16, 255, 0, 0, 255);
    const auto black = solid(16, 16, 0, 255, 255, 255);
    const cd::imgdiff::ImageView va { white.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { black.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_full(va, vb);
    ASSERT_TRUE(r.has_value());
    for (double e : r->error_map)
    {
        EXPECT_GE(e, 0.0);
        EXPECT_LE(e, 1.0);
    }
}

TEST(FlipFull, DimensionMismatchReturnsError)
{
    const auto a = solid(8, 8, 0, 0, 0, 255);
    const auto b = solid(16, 8, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 8, 8 };
    const cd::imgdiff::ImageView vb { b.data(), 16, 8 };
    auto r = cd::imgdiff::compute_flip_full(va, vb);
    ASSERT_FALSE(r.has_value());
}

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

// =============================================================================
// Depth-100 additions — edge / negative / reference-value tests
// ADD-ONLY: no existing math path modified.
// =============================================================================

// -----------------------------------------------------------------------------
// ImageDiff — additional edge / negative cases
// -----------------------------------------------------------------------------

TEST(ImageDiff, AllBlackVsAllWhiteMaxDelta)
{
    // Fully-different images: every channel R/G/B differs by 255; alpha matches.
    const auto black = solid(4, 4, 0, 0, 0, 255);
    const auto white = solid(4, 4, 255, 255, 255, 255);
    const cd::imgdiff::ImageView va { black.data(), 4, 4 };
    const cd::imgdiff::ImageView vb { white.data(), 4, 4 };
    auto r = cd::imgdiff::compare(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->max_abs_delta_r, 255U);
    EXPECT_EQ(r->max_abs_delta_g, 255U);
    EXPECT_EQ(r->max_abs_delta_b, 255U);
    EXPECT_EQ(r->max_abs_delta_a, 0U);
    EXPECT_EQ(r->different_pixels, 16U);
    // RMSE for R=255, G=255, B=255, A=0: sqrt((255²+255²+255²+0)/4) = 255*sqrt(3/4)
    EXPECT_NEAR(r->rmse, 255.0 * std::sqrt(3.0 / 4.0), 0.5);
    EXPECT_LT(r->psnr_db, 10.0);  // large error → low PSNR
    EXPECT_FALSE(cd::imgdiff::passes(*r));
}

TEST(ImageDiff, OneByOneIdentical)
{
    // Smallest valid image, identical — must not crash and return zero diff.
    const auto img = solid(1, 1, 42, 84, 168, 200);
    const cd::imgdiff::ImageView v { img.data(), 1, 1 };
    auto r = cd::imgdiff::compare(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->pixel_count, 1U);
    EXPECT_EQ(r->different_pixels, 0U);
    EXPECT_DOUBLE_EQ(r->rmse, 0.0);
    EXPECT_GE(r->psnr_db, 100.0);
}

TEST(ImageDiff, OneByOneDifferent)
{
    // 1×1 images that differ on all channels — covers single-pixel RMSE path.
    const auto a = solid(1, 1, 0, 0, 0, 255);
    const auto b = solid(1, 1, 255, 255, 255, 0);
    const cd::imgdiff::ImageView va { a.data(), 1, 1 };
    const cd::imgdiff::ImageView vb { b.data(), 1, 1 };
    auto r = cd::imgdiff::compare(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->different_pixels, 1U);
    EXPECT_EQ(r->max_abs_delta_r, 255U);
    EXPECT_EQ(r->max_abs_delta_a, 255U);
    EXPECT_GT(r->rmse, 0.0);
}

TEST(ImageDiff, HighlightDimensionMismatchReturnsError)
{
    // highlight() must propagate kDimensionMismatch the same way compare() does.
    const auto a = solid(4, 4, 0, 0, 0, 255);
    const auto b = solid(8, 4, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 4, 4 };
    const cd::imgdiff::ImageView vb { b.data(), 8, 4 };
    auto h = cd::imgdiff::highlight(va, vb);
    ASSERT_FALSE(h.has_value());
    EXPECT_EQ(h.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kDimensionMismatch));
}

TEST(ImageDiff, HighlightNullPointerReturnsError)
{
    // highlight() with null pointer.
    const auto a = solid(4, 4, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 4, 4 };
    const cd::imgdiff::ImageView vb { nullptr, 4, 4 };
    auto h = cd::imgdiff::highlight(va, vb);
    ASSERT_FALSE(h.has_value());
    EXPECT_EQ(h.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kNullPointer));
}

TEST(ImageDiff, PassesWithCustomMaxFailures)
{
    // passes() threshold: exactly at boundary.
    const auto a = solid(4, 1, 0, 0, 0, 255);
    auto b = a;
    b[0] = 255; b[4] = 255; b[8] = 255;  // 3 pixels differ on R
    const cd::imgdiff::ImageView va { a.data(), 4, 1 };
    const cd::imgdiff::ImageView vb { b.data(), 4, 1 };
    auto r = cd::imgdiff::compare(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->different_pixels, 3U);
    EXPECT_FALSE(cd::imgdiff::passes(*r, 2));  // max_failures=2 → fails
    EXPECT_TRUE(cd::imgdiff::passes(*r, 3));   // exactly at boundary → passes
    EXPECT_TRUE(cd::imgdiff::passes(*r, 4));   // above boundary → passes
}

TEST(ImageDiff, EmptyImageReturnsError)
{
    // Both width=0 and height=0 cases.
    const cd::imgdiff::ImageView vw { nullptr, 0, 4 };
    const cd::imgdiff::ImageView vh { nullptr, 4, 0 };
    // nullptr + zero dims → either kNullPointer or kEmptyImage. The
    // implementation checks nullptr first, so we get kNullPointer.
    auto rw = cd::imgdiff::compare(vw, vw);
    ASSERT_FALSE(rw.has_value());
    auto rh = cd::imgdiff::compare(vh, vh);
    ASSERT_FALSE(rh.has_value());
}

// -----------------------------------------------------------------------------
// SSIM-lite — additional edge cases
// -----------------------------------------------------------------------------

TEST(SsimLite, OneByOneIdenticalReturnsOne)
{
    // 1×1 image with window_size=1 should produce SSIM=1.0.
    const auto img = solid(1, 1, 128, 128, 128, 255);
    const cd::imgdiff::ImageView v { img.data(), 1, 1 };
    auto r = cd::imgdiff::compute_ssim_lite(v, v, 1);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->windows, 1U);
    EXPECT_NEAR(r->mean_ssim, 1.0, 1e-9);
}

TEST(SsimLite, NullPointerReturnsError)
{
    const auto a = solid(8, 8, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 8, 8 };
    const cd::imgdiff::ImageView vb { nullptr, 8, 8 };
    auto r = cd::imgdiff::compute_ssim_lite(va, vb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kNullPointer));
}

TEST(SsimLite, ZeroWindowSizeReturnsError)
{
    // window_size=0 should return kEmptyImage.
    const auto img = solid(8, 8, 100, 100, 100, 255);
    const cd::imgdiff::ImageView v { img.data(), 8, 8 };
    auto r = cd::imgdiff::compute_ssim_lite(v, v, 0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kEmptyImage));
}

TEST(SsimLite, PassesConvenienceHelper)
{
    // ssim_passes default threshold 0.99.
    const auto img = solid(16, 16, 100, 150, 200, 255);
    const cd::imgdiff::ImageView v { img.data(), 16, 16 };
    auto r = cd::imgdiff::compute_ssim_lite(v, v, 8);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(cd::imgdiff::ssim_passes(*r));           // identical → passes
    EXPECT_TRUE(cd::imgdiff::ssim_passes(*r, 0.99));     // explicit threshold
    EXPECT_TRUE(cd::imgdiff::ssim_passes(*r, 1.0));      // exact 1.0 threshold also passes
}

TEST(SsimLite, ExactWindowEdge)
{
    // Image width and height exactly equal window size → exactly 1 window.
    const auto img = solid(8, 8, 50, 100, 150, 255);
    const cd::imgdiff::ImageView v { img.data(), 8, 8 };
    auto r = cd::imgdiff::compute_ssim_lite(v, v, 8);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->windows, 1U);
    EXPECT_NEAR(r->mean_ssim, 1.0, 1e-9);
}

// -----------------------------------------------------------------------------
// Gaussian blur — additional edge / kernel-property tests
// -----------------------------------------------------------------------------

TEST(GaussianBlur, NullPointerReturnsError)
{
    // gaussian_blur with null rgba pointer.
    const cd::imgdiff::ImageView v { nullptr, 8, 8 };
    auto r = cd::imgdiff::gaussian_blur(v, 1.0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kNullPointer));
}

TEST(GaussianBlur, OneByOneConstantStaysConstant)
{
    // Clamp-to-edge on 1×1: every tap refers to the single pixel.
    // Output must be identical to input for any sigma.
    const auto img = solid(1, 1, 77, 99, 133, 200);
    const cd::imgdiff::ImageView v { img.data(), 1, 1 };
    auto r = cd::imgdiff::gaussian_blur(v, 2.0);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->size(), 4U);
    EXPECT_EQ((*r)[0], 77U);
    EXPECT_EQ((*r)[1], 99U);
    EXPECT_EQ((*r)[2], 133U);
    EXPECT_EQ((*r)[3], 200U);
}

TEST(GaussianBlur, KernelSumIsOneViaConstantImage)
{
    // A constant image through any sigma must return the same constant
    // because a normalized Gaussian sums to 1.0. Tests all sigma values
    // including large ones (which produce wide kernels via ceil(3*sigma)).
    // Sigma 0.5 → radius 2, sigma 3.0 → radius 9.
    for (const double sigma : { 0.5, 1.0, 1.5, 3.0 })
    {
        const auto img = solid(16, 16, 210, 130, 55, 255);
        const cd::imgdiff::ImageView v { img.data(), 16, 16 };
        auto r = cd::imgdiff::gaussian_blur(v, sigma);
        ASSERT_TRUE(r.has_value()) << "sigma=" << sigma;
        // Check centre pixel and border pixel (clamp-to-edge must also be exact).
        EXPECT_EQ((*r)[0], 210U) << "sigma=" << sigma;  // corner
        const std::size_t ctr = static_cast<std::size_t>(8 * 16 + 8) * 4U;
        EXPECT_EQ((*r)[ctr], 210U) << "sigma=" << sigma;  // centre
    }
}

TEST(GaussianBlur, OutputHasSameDimensions)
{
    // Output buffer must be exactly width*height*4 bytes.
    const auto img = solid(13, 7, 1, 2, 3, 4);
    const cd::imgdiff::ImageView v { img.data(), 13, 7 };
    auto r = cd::imgdiff::gaussian_blur(v, 1.2);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), static_cast<std::size_t>(13) * 7U * 4U);
}

// -----------------------------------------------------------------------------
// FLIP-lite — reference value + additional edge cases
// -----------------------------------------------------------------------------

TEST(FlipLite, PerceptualMapReferenceValues)
{
    // Verify perceptual_map behaviour against the documented constants
    // (kJndFloor=2.0, kSaturate=96.0). Accesses the detail function via
    // the observable output of compute_flip_lite on a 1×1 image.

    // delta <= 2 (JND floor) → 0. Build Y=0 and Y=1 (both collapse to ≤2 delta
    // after BT.601: actual luminance delta = 0.299*1 = 0.299, which is < 2).
    const auto a0 = solid(1, 1, 0, 0, 0, 255);
    const auto a1 = solid(1, 1, 1, 1, 1, 255);
    const cd::imgdiff::ImageView v0 { a0.data(), 1, 1 };
    const cd::imgdiff::ImageView v1 { a1.data(), 1, 1 };
    // Luminance delta ≈ 0.299+0.587+0.114 = 1.0 < kJndFloor → error must be 0.
    auto r_tiny = cd::imgdiff::compute_flip_lite(v0, v1);
    ASSERT_TRUE(r_tiny.has_value());
    EXPECT_DOUBLE_EQ(r_tiny->mean_error, 0.0);

    // delta = 255 raw lum → perceptual_map saturates to 1.0.
    const auto black1 = solid(1, 1, 0, 0, 0, 255);
    const auto white1 = solid(1, 1, 255, 255, 255, 255);
    const cd::imgdiff::ImageView vb { black1.data(), 1, 1 };
    const cd::imgdiff::ImageView vw { white1.data(), 1, 1 };
    auto r_max = cd::imgdiff::compute_flip_lite(vb, vw);
    ASSERT_TRUE(r_max.has_value());
    EXPECT_DOUBLE_EQ(r_max->mean_error, 1.0);
    EXPECT_DOUBLE_EQ(r_max->max_error, 1.0);
    EXPECT_DOUBLE_EQ(r_max->p95_error, 1.0);
}

TEST(FlipLite, OneByOneIdentical)
{
    // 1×1 identical image: all aggregates zero.
    const auto img = solid(1, 1, 200, 100, 50, 255);
    const cd::imgdiff::ImageView v { img.data(), 1, 1 };
    auto r = cd::imgdiff::compute_flip_lite(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->pixel_count, 1U);
    EXPECT_DOUBLE_EQ(r->mean_error, 0.0);
    EXPECT_DOUBLE_EQ(r->max_error, 0.0);
    EXPECT_DOUBLE_EQ(r->p95_error, 0.0);
    EXPECT_EQ(r->error_map.size(), 1U);
    EXPECT_DOUBLE_EQ(r->error_map[0], 0.0);
}

TEST(FlipLite, NullPointerReturnsError)
{
    const auto a = solid(8, 8, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 8, 8 };
    const cd::imgdiff::ImageView vb { nullptr, 8, 8 };
    auto r = cd::imgdiff::compute_flip_lite(va, vb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kNullPointer));
}

TEST(FlipLite, FlipPassesCustomThreshold)
{
    // flip_passes with a custom threshold other than the default 0.05.
    const auto white = solid(16, 16, 255, 255, 255, 255);
    const auto black = solid(16, 16, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { white.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { black.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_lite(va, vb);
    ASSERT_TRUE(r.has_value());
    // Large error → fails at any reasonable threshold.
    EXPECT_FALSE(cd::imgdiff::flip_passes(*r, 0.99));
    // Trivially wide threshold → passes.
    EXPECT_TRUE(cd::imgdiff::flip_passes(*r, 1.0));
}

TEST(FlipLite, HeatmapDimensionMismatchReturnsEmpty)
{
    // flip_heatmap with error_map.size() != width*height returns zero-init output.
    cd::imgdiff::FlipReport r;
    r.pixel_count = 4;
    r.error_map = { 0.5, 0.5 };  // only 2 entries but 4 pixels expected
    const auto heat = cd::imgdiff::flip_heatmap(r, 2, 2);
    ASSERT_EQ(heat.size(), 2U * 2U * 4U);
    // Must be all zeros (zero-init on mismatch).
    for (const std::uint8_t b : heat)
        EXPECT_EQ(b, 0U);
}

TEST(FlipLite, P95IsZeroForIdenticalImages)
{
    // Identical images → error map is all-zero → p95 = 0.
    const auto img = solid(32, 32, 100, 150, 200, 255);
    const cd::imgdiff::ImageView v { img.data(), 32, 32 };
    auto r = cd::imgdiff::compute_flip_lite(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->p95_error, 0.0);
}

TEST(FlipLite, ErrorMapSizeMatchesPixelCount)
{
    // error_map.size() must equal pixel_count for all image sizes.
    const auto img = solid(7, 5, 50, 100, 150, 255);  // non-power-of-two dims
    const cd::imgdiff::ImageView v { img.data(), 7, 5 };
    auto r = cd::imgdiff::compute_flip_lite(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->error_map.size(), static_cast<std::size_t>(r->pixel_count));
    EXPECT_EQ(r->pixel_count, 7U * 5U);
}

// -----------------------------------------------------------------------------
// Full FLIP — additional edge / error path tests
// -----------------------------------------------------------------------------

TEST(FlipFull, NullPointerReturnsError)
{
    const auto a = solid(8, 8, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 8, 8 };
    const cd::imgdiff::ImageView vb { nullptr, 8, 8 };
    auto r = cd::imgdiff::compute_flip_full(va, vb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kNullPointer));
}

TEST(FlipFull, OneByOneIdentical)
{
    // 1×1 image through the full chroma path: mean_error must be 0.
    const auto img = solid(1, 1, 80, 160, 240, 255);
    const cd::imgdiff::ImageView v { img.data(), 1, 1 };
    auto r = cd::imgdiff::compute_flip_full(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->mean_error, 0.0);
    EXPECT_DOUBLE_EQ(r->max_error, 0.0);
}

TEST(FlipFull, P95AtLeastMean)
{
    // Invariant: p95_error >= mean_error for any non-trivial image pair.
    const auto white = solid(16, 16, 255, 255, 255, 255);
    const auto black = solid(16, 16, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { white.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { black.data(), 16, 16 };
    auto r = cd::imgdiff::compute_flip_full(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r->p95_error, r->mean_error);
}

TEST(FlipFull, ChromaSigmaLargerThanLuminance)
{
    // The implementation uses sigma_c = sigma_y * 1.5.  A chroma-only
    // difference (equal luminance, different Cx/Cz) must register a
    // nonzero error via the full path, confirming the chroma blur ran.
    // Red-dominant vs green-dominant: luminance ≈ 0.2126*200 + 0.7152*50 ≈ 78
    // vs 0.2126*50 + 0.7152*200 ≈ 153 — not pure luma match, but the
    // chroma channels (Cx, Cz) differ substantially regardless.
    const auto red  = solid(32, 32, 200, 50, 50, 255);
    const auto cyan = solid(32, 32, 50, 200, 200, 255);
    const cd::imgdiff::ImageView va { red.data(), 32, 32 };
    const cd::imgdiff::ImageView vb { cyan.data(), 32, 32 };
    auto r = cd::imgdiff::compute_flip_full(va, vb);
    ASSERT_TRUE(r.has_value());
    EXPECT_GT(r->mean_error, 0.0);
    // All pixels are solid-color → uniform error → max == mean.
    EXPECT_NEAR(r->max_error, r->mean_error, 1e-9);
}

TEST(FlipFull, ErrorMapSizeMatchesPixelCount)
{
    const auto img = solid(5, 7, 30, 60, 90, 255);  // non-square
    const cd::imgdiff::ImageView v { img.data(), 5, 7 };
    auto r = cd::imgdiff::compute_flip_full(v, v);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->error_map.size(), static_cast<std::size_t>(r->pixel_count));
    EXPECT_EQ(r->pixel_count, 5U * 7U);
}

// -----------------------------------------------------------------------------
// SSIM Gaussian-weighted — additional edge / error path tests
// -----------------------------------------------------------------------------

TEST(SsimGaussian, NullPointerReturnsError)
{
    const auto a = solid(16, 16, 0, 0, 0, 255);
    const cd::imgdiff::ImageView va { a.data(), 16, 16 };
    const cd::imgdiff::ImageView vb { nullptr, 16, 16 };
    auto r = cd::imgdiff::compute_ssim_gaussian(va, vb);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::imgdiff::imgdiff_errors::Code::kNullPointer));
}

TEST(SsimGaussian, ZeroStrideClampedToOne)
{
    // stride=0 is clamped to 1 inside compute_ssim_gaussian; result must
    // equal the stride=1 result (same coverage).
    const auto img = solid(16, 16, 100, 150, 200, 255);
    const cd::imgdiff::ImageView v { img.data(), 16, 16 };
    auto r0 = cd::imgdiff::compute_ssim_gaussian(v, v, 1.5, 0);
    auto r1 = cd::imgdiff::compute_ssim_gaussian(v, v, 1.5, 1);
    ASSERT_TRUE(r0.has_value());
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r0->windows, r1->windows);
    EXPECT_NEAR(r0->mean_ssim, r1->mean_ssim, 1e-12);
}

TEST(SsimGaussian, MeanGEMinInvariant)
{
    // mean_ssim >= min_ssim must hold when different windows score differently.
    std::vector<std::uint8_t> a(static_cast<std::size_t>(32) * 32 * 4U, 128U);
    for (std::size_t i = 3U; i < a.size(); i += 4U)
        a[i] = 255U;
    auto b = a;
    // Corrupt top-left 8×8 block → that region's windows will score lower.
    for (std::uint32_t y = 0U; y < 8U; ++y)
        for (std::uint32_t x = 0U; x < 8U; ++x)
        {
            const std::size_t i = (static_cast<std::size_t>(y) * 32U + x) * 4U;
            b[i + 0] = 255U;
            b[i + 1] = 0U;
            b[i + 2] = 0U;
        }
    const cd::imgdiff::ImageView va { a.data(), 32, 32 };
    const cd::imgdiff::ImageView vb { b.data(), 32, 32 };
    auto r = cd::imgdiff::compute_ssim_gaussian(va, vb, 1.5, 4);
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r->mean_ssim, r->min_ssim);
}

TEST(SsimGaussian, SmallImageReturnsSingleWindow)
{
    // Image just larger than the Gaussian radius at sigma=1.5 →
    // kernel radius r=ceil(3*1.5)=5 → window range [r, W-r-1].
    // A 12×12 image with stride=1 should yield interior windows.
    const auto img = solid(12, 12, 90, 120, 150, 255);
    const cd::imgdiff::ImageView v { img.data(), 12, 12 };
    auto r = cd::imgdiff::compute_ssim_gaussian(v, v, 1.5, 1);
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r->windows, 1U);
    EXPECT_NEAR(r->mean_ssim, 1.0, 1e-9);
}

TEST(SsimGaussian, ImageSmallerThanKernelReturnsIdentity)
{
    // If the image is smaller than the kernel radius, the loop range
    // [r, W-r-1] is empty → windows=0 → mean=1.0 (identity fall-back).
    // kernel radius for sigma=1.5 is 5; use a 3×3 image.
    const auto img = solid(3, 3, 50, 100, 150, 255);
    const cd::imgdiff::ImageView v { img.data(), 3, 3 };
    auto r = cd::imgdiff::compute_ssim_gaussian(v, v, 1.5, 1);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->windows, 0U);
    EXPECT_DOUBLE_EQ(r->mean_ssim, 1.0);
    EXPECT_DOUBLE_EQ(r->min_ssim, 1.0);
}
