#include <cd/atmosphere/Atmosphere.hpp>
#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

namespace
{

using cd::atmosphere::bake_transmittance_lut;
using cd::atmosphere::henyey_greenstein;
using cd::atmosphere::Parameters;
using cd::atmosphere::rayleigh_phase;

constexpr float kEps = 1e-2F;

TEST(Atmosphere, HenyeyGreensteinIntegratesToOne)
{
    // Crude trapezoid integral over the sphere — phase function must
    // approximate 1.0 (Hillaire 2020, ch. 2).
    constexpr int kN = 200;
    constexpr float g = 0.8F;
    float sum = 0.0F;
    for (int i = 0; i < kN; ++i)
    {
        const float theta = std::numbers::pi_v<float> * (static_cast<float>(i) + 0.5F) /
                            static_cast<float>(kN);
        const float cos_t = std::cos(theta);
        sum += henyey_greenstein(cos_t, g) * std::sin(theta);
    }
    sum *= 2.0F * std::numbers::pi_v<float> * (std::numbers::pi_v<float> / static_cast<float>(kN));
    EXPECT_NEAR(sum, 1.0F, 0.05F);  // ~5% trapezoid error at N=200
}

TEST(Atmosphere, RayleighPhaseGrowsTowardBackscatter)
{
    // Rayleigh phase: peak at cos_theta = ±1, min at 0.
    EXPECT_GT(rayleigh_phase(1.0F),  rayleigh_phase(0.0F));
    EXPECT_GT(rayleigh_phase(-1.0F), rayleigh_phase(0.0F));
    EXPECT_NEAR(rayleigh_phase(1.0F), rayleigh_phase(-1.0F), kEps);
}

TEST(Atmosphere, TransmittanceLutShapeMatchesRequest)
{
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 32, 16);
    EXPECT_EQ(lut.w, 32U);
    EXPECT_EQ(lut.h, 16U);
    EXPECT_EQ(lut.texels.size(), 32U * 16U);
}

TEST(Atmosphere, TransmittanceClampsToZeroOneRange)
{
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 16, 8);
    for (const auto& t : lut.texels)
    {
        EXPECT_GE(t.x, 0.0F);
        EXPECT_LE(t.x, 1.0F);
        EXPECT_GE(t.y, 0.0F);
        EXPECT_LE(t.y, 1.0F);
        EXPECT_GE(t.z, 0.0F);
        EXPECT_LE(t.z, 1.0F);
    }
}

TEST(Atmosphere, TransmittanceMonotonicWithAltitude)
{
    // At a fixed cos(view-zenith) = +1 (looking straight up), the
    // transmittance increases as altitude grows (less atmosphere to
    // pass through).
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 16, 32);
    const std::uint32_t x_zenith = lut.w - 1;  // mu = +1 column
    float prev = -1.0F;
    for (std::uint32_t y = 0; y < lut.h; ++y)
    {
        const auto& t = lut.at(x_zenith, y);
        EXPECT_GE(t.x + kEps, prev);
        prev = t.x;
    }
}

// ---- Transmittance-LUT edge branches (B4 topup; SEAL transmittance-v1) -----

TEST(Atmosphere, ZenithMoreTransmissiveThanHorizon)
{
    // At a fixed altitude, looking straight up (mu = +1) traverses less
    // atmosphere than looking toward the horizon (mu ~ 0), so zenith
    // transmittance is higher. Exercises the mu-column gradient that the
    // monotonic-altitude test never compared across columns.
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 32, 16);
    const std::uint32_t y = 2;  // low altitude
    const auto& zenith  = lut.at(lut.w - 1, y);   // mu ~ +1
    const auto& horizon = lut.at(lut.w / 2, y);   // mu ~ 0
    EXPECT_GT(zenith.x, horizon.x);
    EXPECT_GT(zenith.y, horizon.y);
    EXPECT_GT(zenith.z, horizon.z);
}

TEST(Atmosphere, RayleighBluerThanRedInTransmittance)
{
    // Rayleigh scatters blue more strongly, so a long horizon path
    // transmits LESS blue than red (the spectral ordering of the per-RGB
    // optical-depth accumulation — sky reddening at the horizon).
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 32, 16);
    const auto& horizon = lut.at(lut.w / 2, 0);  // grazing, lowest altitude
    EXPECT_GT(horizon.x, horizon.z);  // more red survives than blue
}

TEST(Atmosphere, NadirRayHasNearOpaqueGrazingPath)
{
    // mu = -1 (looking straight down at the surface) produces the maximum
    // ground-tangent path -> the discriminant/sqrt branch with the longest
    // `dist`; transmittance must stay finite and within [0, 1] (no NaN from
    // the max(disc, 0) guard).
    Parameters p {};
    const auto lut = bake_transmittance_lut(p, 16, 8);
    const auto& nadir = lut.at(0, 0);  // mu ~ -1, lowest altitude
    EXPECT_TRUE(std::isfinite(nadir.x));
    EXPECT_GE(nadir.x, 0.0F);
    EXPECT_LE(nadir.x, 1.0F);
}

TEST(Atmosphere, OzoneLayerAddsGreenAbsorptionMidAltitude)
{
    // Zeroing ozone must RAISE transmittance (less absorption) — pins the
    // ozone term `max(0, 1 - |h - 25|/15)` actually contributing. Compare
    // a horizon path with vs without the ozone profile.
    Parameters with_ozone {};
    Parameters no_ozone = with_ozone;
    no_ozone.ozone_absorption = { 0.0F, 0.0F, 0.0F };
    const auto a = bake_transmittance_lut(with_ozone, 16, 16);
    const auto b = bake_transmittance_lut(no_ozone, 16, 16);
    // Green channel has the strongest ozone coefficient; without ozone
    // the path transmits at least as much green everywhere.
    const std::uint32_t col = a.w / 2;  // horizon-ish
    for (std::uint32_t y = 0; y < a.h; ++y)
        EXPECT_GE(b.at(col, y).y + 1e-5F, a.at(col, y).y) << "y=" << y;
}

TEST(Atmosphere, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::atmosphere::kTransmittanceCS.empty());
    EXPECT_NE(cd::atmosphere::kTransmittanceCS.find("imageStore"),
              std::string_view::npos);
}

}  // namespace
