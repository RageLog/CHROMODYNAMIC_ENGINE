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
    for (int ri = 0; ri <= 20; ++ri)
    {
        const float r = 0.05F * static_cast<float>(ri);
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

// =============================================================================
// Edge / negative coverage (≥80→100).
// =============================================================================

TEST(PostSsr, DefaultSettingsAreStachowiak)
{
    const Settings s {};
    EXPECT_EQ(s.max_steps, 64U);
    EXPECT_FLOAT_EQ(s.thickness, 0.5F);
    EXPECT_FLOAT_EQ(s.roughness_max, 0.6F);
    EXPECT_EQ(s.rays_per_pixel, 1U);
    EXPECT_FLOAT_EQ(s.min_f0, 0.02F);
}

TEST(PostSsr, SchlickFresnelFullMirrorIsOneEverywhere)
{
    // F0 = 1 (perfect mirror): Schlick collapses to 1 at every angle.
    EXPECT_NEAR(schlick_fresnel(1.0F, 1.0F), 1.0F, kEps);
    EXPECT_NEAR(schlick_fresnel(1.0F, 0.0F), 1.0F, kEps);
}

TEST(PostSsr, SchlickFresnelMonotonicTowardGrazing)
{
    // F increases as cos_theta drops from 1 (normal) to 0 (grazing).
    const float f0 = 0.04F;
    float prev = -1.0F;
    for (int ci = 0; ci <= 20; ++ci)
    {
        const float c = 1.0F - 0.05F * static_cast<float>(ci);
        const float f = schlick_fresnel(f0, c);
        EXPECT_GE(f + kEps, prev) << "cos=" << c;
        prev = f;
    }
}

TEST(PostSsr, RoughnessFadeAtExactStartIsOne)
{
    // roughness == kStart (0.1) is the inclusive top of the "no fade" band.
    Settings s {};
    EXPECT_NEAR(roughness_fade(0.1F, s), 1.0F, kEps);
}

TEST(PostSsr, RoughnessFadeMidRangeIsBetweenZeroAndOne)
{
    // A roughness halfway through the [0.1, max] ramp must be a partial fade.
    Settings s {};
    s.roughness_max = 0.6F;
    const float mid = roughness_fade(0.35F, s);  // halfway from 0.1 to 0.6
    EXPECT_GT(mid, 0.0F);
    EXPECT_LT(mid, 1.0F);
    EXPECT_NEAR(mid, 0.5F, kEps);
}

TEST(PostSsr, MetallicGateSmoothstepMidpointIsHalf)
{
    // metallic at the exact midpoint of [0.05, 0.30] = 0.175 -> smoothstep
    // returns 0.5 (Hermite symmetry).
    EXPECT_NEAR(compute_ssr_weight(0.175F), 0.5F, kEps);
}

TEST(PostSsr, MetallicGateIsMonotonic)
{
    float prev = -1.0F;
    for (int mi = 0; mi <= 50; ++mi)
    {
        const float m = 0.02F * static_cast<float>(mi);
        const float w = compute_ssr_weight(m);
        EXPECT_GE(w + kEps, prev) << "metallic=" << m;
        EXPECT_GE(w, 0.0F);
        EXPECT_LE(w, 1.0F);
        prev = w;
    }
}

}  // namespace
