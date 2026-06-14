// =============================================================================
// CHROMODYNAMIC — cd::render::volumetric tests (Phase 8 Sprint 11 Wave 91)
// =============================================================================
#include <cd/render/volumetric/Fog.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <algorithm>
#include <numbers>

namespace
{

TEST(VolumetricFog, ZeroDistanceTransmittsFully)
{
    EXPECT_NEAR(cd::render::volumetric::transmittance(0.5F, 0.0F), 1.0F, 1e-6F);
}

TEST(VolumetricFog, BeerLambertHalfLifeAtDensity)
{
    // T = exp(-σ_t · d). With σ_t = 1.0 and d = ln(2), T should be 0.5.
    const float d = std::numbers::ln2_v<float>;
    EXPECT_NEAR(cd::render::volumetric::transmittance(1.0F, d), 0.5F, 1e-5F);
}

TEST(VolumetricFog, TransmittanceMonotonicallyDecreases)
{
    const float a = cd::render::volumetric::transmittance(0.1F, 5.0F);
    const float b = cd::render::volumetric::transmittance(0.1F, 10.0F);
    const float c = cd::render::volumetric::transmittance(0.1F, 20.0F);
    EXPECT_GT(a, b);
    EXPECT_GT(b, c);
    EXPECT_GT(a, 0.0F);
    EXPECT_LE(a, 1.0F);
}

TEST(VolumetricFog, IsotropicPhaseIs1Over4Pi)
{
    // g = 0 → isotropic; phase value should be 1 / (4π) for any cos_theta.
    constexpr float kExpected = 1.0F / (4.0F * cd::math::pi);
    EXPECT_NEAR(cd::render::volumetric::henyey_greenstein(0.0F, 0.0F), kExpected, 1e-5F);
    EXPECT_NEAR(cd::render::volumetric::henyey_greenstein(1.0F, 0.0F), kExpected, 1e-5F);
    EXPECT_NEAR(cd::render::volumetric::henyey_greenstein(-1.0F, 0.0F), kExpected, 1e-5F);
}

TEST(VolumetricFog, ForwardScatterPeaksAhead)
{
    // g > 0 → forward-scattering. Phase at cos_theta = +1 (forward)
    // should exceed phase at cos_theta = -1 (back).
    const float fwd = cd::render::volumetric::henyey_greenstein(1.0F, 0.8F);
    const float back = cd::render::volumetric::henyey_greenstein(-1.0F, 0.8F);
    EXPECT_GT(fwd, back);
}

TEST(VolumetricFog, BackScatterPeaksBehind)
{
    const float fwd = cd::render::volumetric::henyey_greenstein(1.0F, -0.5F);
    const float back = cd::render::volumetric::henyey_greenstein(-1.0F, -0.5F);
    EXPECT_LT(fwd, back);
}

TEST(VolumetricFog, InScatteringIsZeroForZeroLight)
{
    cd::render::volumetric::FogParams p {};
    const cd::math::Vec3f view { 0.0F, 0.0F, -1.0F };
    const cd::math::Vec3f light { 0.0F, 1.0F, 0.0F };
    const cd::math::Vec3f zero { 0.0F, 0.0F, 0.0F };
    auto r = cd::render::volumetric::in_scattering(p, view, light, zero, 100.0F);
    EXPECT_FLOAT_EQ(r.x, 0.0F);
    EXPECT_FLOAT_EQ(r.y, 0.0F);
    EXPECT_FLOAT_EQ(r.z, 0.0F);
}

TEST(VolumetricFog, InScatteringIncreasesWithDistance)
{
    cd::render::volumetric::FogParams p {};
    p.extinction = 0.05F;
    p.scattering = 0.04F;
    p.albedo = { 1.0F, 1.0F, 1.0F };
    p.anisotropy = 0.0F;  // isotropic
    const cd::math::Vec3f view { 0.0F, 0.0F, -1.0F };
    const cd::math::Vec3f light { 0.0F, 1.0F, 0.0F };
    const cd::math::Vec3f sun { 1.0F, 1.0F, 1.0F };
    auto near = cd::render::volumetric::in_scattering(p, view, light, sun, 1.0F);
    auto far = cd::render::volumetric::in_scattering(p, view, light, sun, 100.0F);
    EXPECT_GT(far.x, near.x);  // more fog accumulates over longer rays
    // Asymptotes at L_max = σ_s / σ_t · phase · L (for very long rays).
    const float L_max = (p.scattering / p.extinction)
                       * (1.0F / (4.0F * cd::math::pi));
    EXPECT_LT(far.x, L_max * 1.01F + 1e-3F);  // can't exceed the limit
}

TEST(VolumetricFog, IntegrateAlongHomogeneousMatchesClosedForm)
{
    cd::render::volumetric::FogParams p {};
    p.extinction = 0.1F;
    p.scattering = 0.08F;
    p.albedo = { 1.0F, 1.0F, 1.0F };
    p.anisotropy = 0.3F;
    const cd::math::Vec3f view { 0.0F, 0.0F, -1.0F };
    const cd::math::Vec3f light { 0.0F, 1.0F, 0.0F };
    const cd::math::Vec3f sun { 1.0F, 1.0F, 1.0F };
    const float distance = 50.0F;

    auto closed = cd::render::volumetric::in_scattering(p, view, light, sun, distance);
    // Numerical integration with constant σ_t — should converge to the
    // closed-form value within ~1 % at 64 steps.
    auto integrated = cd::render::volumetric::integrate_along(
        p, view, light, sun, distance,
        [&](float) { return p.extinction; },
        64);
    EXPECT_NEAR(integrated.x, closed.x, std::max(0.005F, closed.x * 0.02F));
    EXPECT_NEAR(integrated.y, closed.y, std::max(0.005F, closed.y * 0.02F));
    EXPECT_NEAR(integrated.z, closed.z, std::max(0.005F, closed.z * 0.02F));
}

TEST(VolumetricFog, IntegrateAlongHeterogeneousSamplesCallback)
{
    cd::render::volumetric::FogParams p {};
    p.extinction = 0.1F;
    p.scattering = 0.08F;
    p.albedo = { 1.0F, 1.0F, 1.0F };
    const cd::math::Vec3f view { 0.0F, 0.0F, -1.0F };
    const cd::math::Vec3f light { 0.0F, 1.0F, 0.0F };
    const cd::math::Vec3f sun { 1.0F, 1.0F, 1.0F };

    int samples = 0;
    auto result = cd::render::volumetric::integrate_along(
        p, view, light, sun, 20.0F,
        [&](float t) {
            ++samples;
            // Linear ramp from 0 at near to 2x extinction at far.
            return p.extinction * (t / 20.0F) * 2.0F;
        },
        32);
    EXPECT_EQ(samples, 32);
    EXPECT_GT(result.x, 0.0F);
    EXPECT_LT(result.x, 1.0F);  // bounded
}

}  // namespace
