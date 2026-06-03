#include <cd/post/ssr/Ssr.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::post::ssr::compute_ssr_weight;
using cd::post::ssr::roughness_fade;
using cd::post::ssr::schlick_fresnel;
using cd::post::ssr::Settings;

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
    EXPECT_FALSE(cd::post::ssr::kSsrTraceCS.empty());
    EXPECT_NE(cd::post::ssr::kSsrTraceCS.find("reflect"), std::string_view::npos);
    EXPECT_NE(cd::post::ssr::kSsrTraceCS.find("unproject"), std::string_view::npos);
}

// -----------------------------------------------------------------------------
// M9 W3A — T1.8 metallic-driven SSR gate (curtain-reflection lesson, phase629).
// SSR must NOT be gated on material *kind*; it must be gated on the per-pixel
// metallic G-buffer channel. Pure dielectrics (cloth, plaster) skip SSR
// entirely; pure metals receive full SSR; smoothstep between 0.05 and 0.30.
// -----------------------------------------------------------------------------

TEST(PostSsrMetallicGate, DielectricGetsZeroWeight)
{
    // metallic = 0 → pure dielectric (cloth curtain) → SKIP SSR entirely.
    EXPECT_NEAR(compute_ssr_weight(0.0F), 0.0F, kEps);
}

TEST(PostSsrMetallicGate, AtLowerThresholdStillZero)
{
    // metallic = 0.05 sits exactly on the ramp's lower clamp.
    EXPECT_NEAR(compute_ssr_weight(0.05F), 0.0F, kEps);
}

TEST(PostSsrMetallicGate, AtUpperThresholdReachesOne)
{
    // metallic = 0.30 sits exactly on the ramp's upper clamp → full SSR.
    EXPECT_NEAR(compute_ssr_weight(0.30F), 1.0F, kEps);
}

TEST(PostSsrMetallicGate, FullyMetallicRegionSaturatesAtOne)
{
    // metallic = 0.5 (and any value >= 0.30) → full SSR weight.
    EXPECT_NEAR(compute_ssr_weight(0.5F), 1.0F, kEps);
}

}  // namespace
