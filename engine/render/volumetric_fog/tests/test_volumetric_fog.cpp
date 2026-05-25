#include <cd/volumetric_fog/Fog.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::volumetric_fog::beer_lambert;
using cd::volumetric_fog::FroxelGrid;
using cd::volumetric_fog::GridConfig;
using cd::volumetric_fog::integrate_view_ray;
using cd::volumetric_fog::slice_to_view_z;
using cd::volumetric_fog::view_z_to_slice;

constexpr float kEps = 1e-3F;

TEST(VolumetricFog, BeerLambertZeroSigmaIsUnity)
{
    EXPECT_NEAR(beer_lambert(0.0F, 5.0F), 1.0F, kEps);
}

TEST(VolumetricFog, BeerLambertDecaysWithDistance)
{
    float prev = 1.0F + kEps;
    for (float dt = 0.0F; dt < 5.0F; dt += 0.25F)
    {
        const float t = beer_lambert(0.5F, dt);
        EXPECT_LE(t, prev + kEps);
        EXPECT_GE(t, 0.0F);
        prev = t;
    }
}

TEST(VolumetricFog, SliceQuadraticDistributionRoundTrip)
{
    GridConfig g {};
    g.near_z = 0.1F;
    g.far_z  = 64.0F;
    for (float s = 0.0F; s <= 1.0F; s += 0.1F)
    {
        const float vz = slice_to_view_z(s, g);
        const float ss = view_z_to_slice(vz, g);
        EXPECT_NEAR(ss, s, kEps);
    }
}

TEST(VolumetricFog, SliceDistributionDensePerNearCamera)
{
    GridConfig g {};
    g.near_z = 0.1F; g.far_z = 64.0F;
    // Slices 0..0.5 cover 25% of the depth range; slices 0.5..1.0
    // cover 75%. Quadratic distribution.
    const float near_half = slice_to_view_z(0.5F, g) - g.near_z;
    const float far_half  = g.far_z - slice_to_view_z(0.5F, g);
    EXPECT_LT(near_half, far_half);
}

TEST(VolumetricFog, IntegrationTransmittanceMonotonicallyDrops)
{
    FroxelGrid grid;
    grid.config = { 4, 4, 8, 0.1F, 8.0F };
    grid.resize();
    // Fill every cell with mild extinction; no scattering.
    for (auto& c : grid.cells) c = { 0, 0, 0, 0.5F };
    std::vector<cd::math::Vec4f> out(grid.config.z);
    integrate_view_ray(grid, 0, 0, out);
    float prev = 1.0F + kEps;
    for (const auto& a : out)
    {
        EXPECT_LE(a.w, prev + kEps);
        EXPECT_GE(a.w, 0.0F);
        prev = a.w;
    }
}

TEST(VolumetricFog, GlslKernelsNonEmpty)
{
    EXPECT_FALSE(cd::volumetric_fog::kFogInjectionCS.empty());
    EXPECT_FALSE(cd::volumetric_fog::kFogIntegrationCS.empty());
    EXPECT_NE(cd::volumetric_fog::kFogInjectionCS.find("image3D"),
              std::string_view::npos);
}

}  // namespace
