#include <cd/atmosphere/Atmosphere.hpp>

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
        const float theta = 3.14159265F * (static_cast<float>(i) + 0.5F) /
                            static_cast<float>(kN);
        const float cos_t = std::cos(theta);
        sum += henyey_greenstein(cos_t, g) * std::sin(theta);
    }
    sum *= 2.0F * 3.14159265F * (3.14159265F / static_cast<float>(kN));
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

TEST(Atmosphere, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::atmosphere::kTransmittanceCS.empty());
    EXPECT_NE(cd::atmosphere::kTransmittanceCS.find("imageStore"),
              std::string_view::npos);
}

}  // namespace
