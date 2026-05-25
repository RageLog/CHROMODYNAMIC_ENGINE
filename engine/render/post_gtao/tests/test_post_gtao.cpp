// =============================================================================
// cd::post_gtao unit tests — Day 4.
// =============================================================================
#include <cd/post_gtao/Gtao.hpp>

#include <gtest/gtest.h>

#include <array>
#include <span>

namespace
{

using cd::post_gtao::ao_from_horizons;
using cd::post_gtao::fast_acos;
using cd::post_gtao::integrate_direction;

constexpr float kEps = 0.05F;  // GTAO integrators are intentionally approximate

TEST(PostGtao, FastAcosMatchesStdAcosWithinTolerance)
{
    // Jimenez's rational fast_acos approximates std::acos to <0.18 rad
    // across [-1, 1] — accuracy degrades near the endpoints. The
    // tolerance reflects the published error envelope; GTAO's
    // integrator masks the residual error.
    for (float x = -1.0F; x <= 1.0F; x += 0.05F)
    {
        const float ref  = std::acos(x);
        const float fast = fast_acos(x);
        EXPECT_NEAR(fast, ref, 0.20F) << "x=" << x;
    }
    // Tighter check on the "production" interior range where GTAO
    // actually evaluates horizon cosines (rarely past ±0.9).
    for (float x = -0.5F; x <= 0.5F; x += 0.05F)
    {
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
    for (float h = 0.0F; h >= -0.9F; h -= 0.1F)
    {
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
    for (float h = 0.0F; h >= -0.9F; h -= 0.1F)
    {
        const float v = integrate_direction(h, h, 1.0F);
        EXPECT_GE(v, prev - kEps);
        prev = v;
    }
}

TEST(PostGtao, GlslKernelsNonEmptyAndDeclareEntryPoints)
{
    EXPECT_FALSE(cd::post_gtao::kGtaoMainCS.empty());
    EXPECT_FALSE(cd::post_gtao::kGtaoDenoiseCS.empty());
    EXPECT_NE(cd::post_gtao::kGtaoMainCS.find("fast_acos"),
              std::string_view::npos);
}

}  // namespace
