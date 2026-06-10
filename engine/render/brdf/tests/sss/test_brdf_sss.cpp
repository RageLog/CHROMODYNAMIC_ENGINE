#include <cd/brdf/sss/Sss.hpp>

#include <gtest/gtest.h>

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

}  // namespace
