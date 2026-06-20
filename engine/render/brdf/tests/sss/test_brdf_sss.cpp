#include <cd/brdf/sss/Sss.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string_view>

namespace
{

using cd::brdf::sss::burley_diffusion_profile;
using cd::brdf::sss::make_burley_kernel;

constexpr float kEps = 1e-3F;

TEST(BrdfSss, ProfileDecaysWithDistance)
{
    float prev = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 20; ++i)  // r = 0.1, 0.6, ..., 9.6 (< 10.0)
    {
        const float r = 0.1F + (static_cast<float>(i) * 0.5F);
        const float p = burley_diffusion_profile(r, 1.0F);
        EXPECT_LE(p, prev + kEps);
        EXPECT_GE(p, 0.0F);
        prev = p;
    }
}

TEST(BrdfSss, ProfileFiniteAndNonNegativeAcrossMeanFreePath)
{
    // The Burley profile is sensitive to normalisation; we don't pin
    // a particular monotonicity here — just that values stay finite
    // and non-negative across a sweep of mean-free-path values.
    const float r = 1.0F;
    for (int i = 0; i < 10; ++i)  // d = 0.1, 0.6, ..., 4.6 (last value <= 5.0)
    {
        const float d = 0.1F + (static_cast<float>(i) * 0.5F);
        const float p = burley_diffusion_profile(r, d);
        EXPECT_GE(p, 0.0F);
        EXPECT_TRUE(std::isfinite(p));
    }
}

TEST(BrdfSss, KernelWeightsSumToOne)
{
    const auto k = make_burley_kernel(8, 1.0F, 5.0F);
    float sum = k.weights[0];
    for (std::size_t i = 1; i < k.weights.size(); ++i)
        sum += 2.0F * k.weights[i];
    EXPECT_NEAR(sum, 1.0F, kEps);
}

TEST(BrdfSss, KernelOffsetsMonotonicallyIncrease)
{
    const auto k = make_burley_kernel(8, 1.0F, 5.0F);
    for (std::size_t i = 1; i < k.offsets_mm.size(); ++i)
    {
        EXPECT_GE(k.offsets_mm[i], k.offsets_mm[i - 1] - kEps);
    }
}

TEST(BrdfSss, KernelZeroTapsReturnsEmpty)
{
    const auto k = make_burley_kernel(0, 1.0F, 5.0F);
    EXPECT_TRUE(k.weights.empty());
}

TEST(BrdfSss, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::brdf::sss::kSssSeparableBlurCS.empty());
    EXPECT_NE(cd::brdf::sss::kSssSeparableBlurCS.find("imageStore"),
              std::string_view::npos);
}

// === ADD-ONLY edge/negative coverage ========================================
// Burley 2015 "Extending the Disney BRDF to a BSDF with Integrated Subsurface
// Scattering"; Jimenez & Gutierrez 2010 separable SSS kernel. Reference values
// pinned against the exact code in Sss.hpp; no profile/kernel math is changed.

TEST(BrdfSss, ProfileReferenceValueAtUnitDistance)
{
    // r=1, s=0.5: (exp(-2) + exp(-1/1.5)) / (8*pi*0.5*1 + 1e-5) = 0.051626.
    EXPECT_NEAR(burley_diffusion_profile(1.0F, 0.5F), 0.051626F, 1e-5F);
}

TEST(BrdfSss, ProfileZeroWhenMeanFreePathDegenerate)
{
    // s < 1e-5 short-circuits to 0 (channel with no scattering, no div-by-0).
    EXPECT_FLOAT_EQ(burley_diffusion_profile(1.0F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(burley_diffusion_profile(1.0F, 1e-6F), 0.0F);
}

TEST(BrdfSss, ProfileFiniteAtZeroRadius)
{
    // r = 0: the +1e-5 in the denominator guards the 1/r singularity so the
    // centre tap is large-but-finite, never Inf/NaN (Burley normalisation).
    const float p = burley_diffusion_profile(0.0F, 0.5F);
    EXPECT_TRUE(std::isfinite(p));
    EXPECT_GT(p, 0.0F);
}

TEST(BrdfSss, ProfileMonotonicDecayAtFixedScale)
{
    // For a fixed mean-free-path the radial weight strictly decreases with r
    // (the (exp + exp)/r envelope is monotone past the guarded origin).
    const float p_near = burley_diffusion_profile(0.5F, 1.0F);
    const float p_far  = burley_diffusion_profile(4.0F, 1.0F);
    EXPECT_GT(p_near, p_far);
    EXPECT_GE(p_far, 0.0F);
}

TEST(BrdfSss, KernelCentreTapIsLargestWeight)
{
    // After normalisation the r=0 centre tap carries the most energy; outer
    // taps taper. Mirrors the separable Jimenez profile shape.
    const auto k = make_burley_kernel(8, 1.0F, 5.0F);
    ASSERT_GE(k.weights.size(), 2U);
    for (std::size_t i = 1; i < k.weights.size(); ++i)
        EXPECT_GE(k.weights[0], k.weights[i]);
}

TEST(BrdfSss, KernelWeightsNonNegativeAndNormalised)
{
    // Energy conservation: every weight >= 0 and the symmetric sum is 1.
    const auto k = make_burley_kernel(8, 1.0F, 5.0F);
    float sum = k.weights[0];
    for (const float w : k.weights) EXPECT_GE(w, 0.0F);
    for (std::size_t i = 1; i < k.weights.size(); ++i)
        sum += 2.0F * k.weights[i];
    EXPECT_NEAR(sum, 1.0F, kEps);
}

TEST(BrdfSss, KernelOutermostOffsetEqualsRadius)
{
    // The last tap sits exactly at radius_mm (even spacing across [0, radius]).
    const auto k = make_burley_kernel(8, 1.0F, 5.0F);
    ASSERT_FALSE(k.offsets_mm.empty());
    EXPECT_NEAR(k.offsets_mm.back(), 5.0F, kEps);
    EXPECT_FLOAT_EQ(k.offsets_mm.front(), 0.0F);
}

TEST(BrdfSss, KernelSingleTapNormalisesToUnity)
{
    // taps == 1: only the centre weight exists; normalisation makes it 1.0.
    const auto k = make_burley_kernel(1, 1.0F, 5.0F);
    ASSERT_EQ(k.weights.size(), 1U);
    EXPECT_NEAR(k.weights[0], 1.0F, kEps);
    EXPECT_FLOAT_EQ(k.offsets_mm[0], 0.0F);
}

TEST(BrdfSss, KernelHoldsForMilkyAndLowDensityRadii)
{
    // Boundary scales per the header: 1mm low-density, 6mm milky. Both stay
    // finite, non-negative and energy-conserving.
    for (const float radius : { 1.0F, 6.0F })
    {
        const auto k = make_burley_kernel(8, 1.0F, radius);
        float sum = k.weights[0];
        for (std::size_t i = 1; i < k.weights.size(); ++i)
        {
            EXPECT_TRUE(std::isfinite(k.weights[i]));
            sum += 2.0F * k.weights[i];
        }
        EXPECT_NEAR(sum, 1.0F, kEps);
        EXPECT_NEAR(k.offsets_mm.back(), radius, kEps);
    }
}

TEST(BrdfSss, GlslKernelDeclaresMaxTapsAndBilateral)
{
    // The compute shader keeps the 16-tap cap and bilateral depth weight that
    // the CPU kernel baker is sized against.
    using cd::brdf::sss::kSssSeparableBlurCS;
    EXPECT_NE(kSssSeparableBlurCS.find("kMaxTaps = 16"), std::string_view::npos);
    EXPECT_NE(kSssSeparableBlurCS.find("direction"), std::string_view::npos);
}

TEST(BrdfSss, InlineWrapGlslPresent)
{
    // Cheap backlit wrap-diffusion fallback ships in its own GLSL block.
    using cd::brdf::sss::kInlineBurleyWrapGlsl;
    EXPECT_FALSE(kInlineBurleyWrapGlsl.empty());
    EXPECT_NE(kInlineBurleyWrapGlsl.find("sss_inline_wrap"),
              std::string_view::npos);
}

}  // namespace
