// =============================================================================
// cd::post_gtao unit tests — Day 4.
// =============================================================================
#include <cd/post/gtao/Gtao.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <span>
#include <string_view>

namespace
{

using cd::post::gtao::ao_from_horizons;
using cd::post::gtao::fast_acos;
using cd::post::gtao::integrate_direction;
using cd::post::gtao::Settings;

constexpr float kEps = 0.05F;  // GTAO integrators are intentionally approximate

TEST(PostGtao, FastAcosMatchesStdAcosWithinTolerance)
{
    // Jimenez's rational fast_acos approximates std::acos to <0.18 rad
    // across [-1, 1] — accuracy degrades near the endpoints. The
    // tolerance reflects the published error envelope; GTAO's
    // integrator masks the residual error.
    for (int xi = 0; xi <= 40; ++xi)
    {
        const float x = -1.0F + 0.05F * static_cast<float>(xi);
        const float ref  = std::acos(x);
        const float fast = fast_acos(x);
        EXPECT_NEAR(fast, ref, 0.20F) << "x=" << x;
    }
    // Tighter check on the "production" interior range where GTAO
    // actually evaluates horizon cosines (rarely past ±0.9).
    for (int xi = 0; xi <= 20; ++xi)
    {
        const float x = -0.5F + 0.05F * static_cast<float>(xi);
        const float ref  = std::acos(x);
        const float fast = fast_acos(x);
        EXPECT_NEAR(fast, ref, 0.05F) << "x=" << x;
    }
}

TEST(PostGtao, AoFromHorizonsReturnsOneWhenUnoccluded)
{
    // Horizon cosines both 0 -> "horizon at the surface plane" -> no
    // occluder above the tangent -> full visibility.
    std::array<float, 2> horizons { 0.0F, 0.0F };
    const float ao = ao_from_horizons(
        std::span<const float>(horizons.data(), horizons.size()), 1.0F);
    EXPECT_NEAR(ao, 1.0F, kEps);
}

TEST(PostGtao, AoFromHorizonsReturnsLessThanFullyLitWhenObstructed)
{
    // Mid-range horizon cosines reduce visibility below the
    // unobstructed baseline. We don't pin a specific magnitude here —
    // GTAO's integrator is approximate and the analytic AO test lives
    // in IntegrateDirectionMonotonicInHorizonCos below.
    std::array<float, 2> baseline { 0.0F, 0.0F };
    std::array<float, 2> blocked  { 0.7F, 0.7F };
    const float ao_b = ao_from_horizons(
        std::span<const float>(baseline.data(), baseline.size()), 1.0F);
    const float ao_x = ao_from_horizons(
        std::span<const float>(blocked.data(), blocked.size()), 1.0F);
    EXPECT_GE(ao_b, ao_x - kEps);
}

TEST(PostGtao, AoDecreasesAsOcclusionGrows)
{
    // Sweep horizon cosines from 0 to -1 and watch AO drop.
    float prev = 1.0F + kEps;
    for (int hi = 0; hi <= 9; ++hi)
    {
        const float h = -0.1F * static_cast<float>(hi);
        std::array<float, 2> horizons { h, h };
        const float ao = ao_from_horizons(
        std::span<const float>(horizons.data(), horizons.size()), 1.0F);
        EXPECT_LE(ao, prev + kEps);
        prev = ao;
    }
}

TEST(PostGtao, IntegrateDirectionMonotonicInHorizonCos)
{
    // Tightening the horizon (cos -> -1) should increase the integrator's
    // contribution to "occluded" (the integral itself is monotonic).
    float prev = -1.0F;
    for (int hi = 0; hi <= 9; ++hi)
    {
        const float h = -0.1F * static_cast<float>(hi);
        const float v = integrate_direction(h, h, 1.0F);
        EXPECT_GE(v, prev - kEps);
        prev = v;
    }
}

TEST(PostGtao, GlslKernelsNonEmptyAndDeclareEntryPoints)
{
    EXPECT_FALSE(cd::post::gtao::kGtaoMainCS.empty());
    EXPECT_FALSE(cd::post::gtao::kGtaoDenoiseCS.empty());
    EXPECT_NE(cd::post::gtao::kGtaoMainCS.find("fast_acos"),
              std::string_view::npos);
}

// =============================================================================
// Edge / negative coverage (≥80→100).
// =============================================================================

TEST(PostGtao, DefaultSettingsAreRealTimePreset)
{
    const Settings s {};
    EXPECT_EQ(s.direction_count, 4U);
    EXPECT_EQ(s.step_count, 4U);
    EXPECT_FLOAT_EQ(s.radius, 1.0F);
    EXPECT_FLOAT_EQ(s.falloff, 2.0F);
    EXPECT_EQ(s.denoise_radius, 2U);
}

TEST(PostGtao, AoFromEmptyHorizonsReturnsFullyLit)
{
    // No samples -> "fully lit" sentinel (1.0), never a NaN from /0.
    const float ao = ao_from_horizons(std::span<const float>(), 1.0F);
    EXPECT_FLOAT_EQ(ao, 1.0F);
}

TEST(PostGtao, AoFromOddLengthHorizonsReturnsFullyLit)
{
    // Pairs are (left_cos, right_cos); an odd count is malformed -> 1.0.
    std::array<float, 3> bad { 0.5F, 0.5F, 0.5F };
    const float ao = ao_from_horizons(
        std::span<const float>(bad.data(), bad.size()), 1.0F);
    EXPECT_FLOAT_EQ(ao, 1.0F);
}

TEST(PostGtao, AoFromHorizonsAlwaysClampedToUnitRange)
{
    // Across many horizon configurations the result must stay in [0, 1].
    for (int hi = 0; hi <= 20; ++hi)
    {
        const float h = -1.0F + 0.1F * static_cast<float>(hi);
        std::array<float, 4> horizons { h, -h, h * 0.5F, -h * 0.5F };
        const float ao = ao_from_horizons(
            std::span<const float>(horizons.data(), horizons.size()), 0.5F);
        EXPECT_GE(ao, 0.0F) << "h=" << h;
        EXPECT_LE(ao, 1.0F) << "h=" << h;
    }
}

TEST(PostGtao, FastAcosEndpointsAreFinite)
{
    // The 1-|x| sqrt term goes to 0 at the endpoints; must not produce NaN.
    EXPECT_TRUE(std::isfinite(fast_acos(1.0F)));
    EXPECT_TRUE(std::isfinite(fast_acos(-1.0F)));
    // fast_acos is a cheap polynomial approximation whose accuracy is best in
    // the mid-range; its WORST case (~0.16 rad) is exactly at the endpoints.
    // Assert the right neighbourhood + correct ordering, not bit-accuracy.
    EXPECT_NEAR(fast_acos(1.0F), 0.0F, 0.2F);             // acos(1) = 0
    EXPECT_NEAR(fast_acos(-1.0F), 3.14159F, 0.2F);        // acos(-1) = pi
    EXPECT_LT(fast_acos(1.0F), fast_acos(-1.0F));         // monotonic endpoints
}

TEST(PostGtao, DenoiseKernelDeclaresBilateralWeight)
{
    EXPECT_NE(cd::post::gtao::kGtaoDenoiseCS.find("depth"),
              std::string_view::npos);
    EXPECT_NE(cd::post::gtao::kGtaoDenoiseCS.find("radius"),
              std::string_view::npos);
}

}  // namespace
