#include <cd/post_ssr/Ssr.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::post_ssr::roughness_fade;
using cd::post_ssr::schlick_fresnel;
using cd::post_ssr::Settings;

constexpr float kEps = 1e-3F;

TEST(PostSsr, SchlickFresnelGrowsAtGrazing)
{
    // Schlick: F = F0 at normal incidence, F -> 1 at grazing.
    const float f0 = 0.04F;
    const float f_normal  = schlick_fresnel(f0, 1.0F);     // cos = 1
    const float f_grazing = schlick_fresnel(f0, 0.05F);    // near edge-on
    EXPECT_NEAR(f_normal, 0.04F, kEps);
    EXPECT_GT(f_grazing, 0.5F);
}

TEST(PostSsr, RoughnessFadeBelowStartIsOne)
{
    Settings s {};
    EXPECT_NEAR(roughness_fade(0.0F,  s), 1.0F, kEps);
    EXPECT_NEAR(roughness_fade(0.05F, s), 1.0F, kEps);
}

TEST(PostSsr, RoughnessFadeAboveMaxIsZero)
{
    Settings s {};
    s.roughness_max = 0.5F;
    EXPECT_NEAR(roughness_fade(0.5F, s), 0.0F, kEps);
    EXPECT_NEAR(roughness_fade(0.9F, s), 0.0F, kEps);
}

TEST(PostSsr, RoughnessFadeIsMonotonicallyDecreasing)
{
    Settings s {};
    float prev = 1.0F + kEps;
    for (float r = 0.0F; r <= 1.0F; r += 0.05F)
    {
        const float v = roughness_fade(r, s);
        EXPECT_LE(v, prev + kEps);
        prev = v;
    }
}

TEST(PostSsr, GlslKernelNonEmptyAndUsesReflectAndProj)
{
    EXPECT_FALSE(cd::post_ssr::kSsrTraceCS.empty());
    EXPECT_NE(cd::post_ssr::kSsrTraceCS.find("reflect"), std::string_view::npos);
    EXPECT_NE(cd::post_ssr::kSsrTraceCS.find("unproject"), std::string_view::npos);
}

}  // namespace
