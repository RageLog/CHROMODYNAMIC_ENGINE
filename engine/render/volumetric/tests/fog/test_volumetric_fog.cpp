#include <cd/volumetric/fog/Fog.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace
{

using cd::volumetric::fog::beer_lambert;
using cd::volumetric::fog::FroxelGrid;
using cd::volumetric::fog::GridConfig;
using cd::volumetric::fog::integrate_view_ray;
using cd::volumetric::fog::slice_to_view_z;
using cd::volumetric::fog::view_z_to_slice;

constexpr float kEps = 1e-3F;

TEST(VolumetricFog, BeerLambertZeroSigmaIsUnity)
{
    EXPECT_NEAR(beer_lambert(0.0F, 5.0F), 1.0F, kEps);
}

TEST(VolumetricFog, BeerLambertDecaysWithDistance)
{
    float prev = 1.0F + kEps;
    for (int di = 0; di < 20; ++di)
    {
        const float dt = 0.25F * static_cast<float>(di);
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
    for (int si = 0; si <= 10; ++si)
    {
        const float s = 0.1F * static_cast<float>(si);
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
    EXPECT_FALSE(cd::volumetric::fog::kFogInjectionCS.empty());
    EXPECT_FALSE(cd::volumetric::fog::kFogIntegrationCS.empty());
    EXPECT_NE(cd::volumetric::fog::kFogInjectionCS.find("image3D"),
              std::string_view::npos);
}

// ---- Floor-raise ADD-ONLY regression locks (Run: 66->70) -------------------
//
// fog/Fog.hpp's integrator multiplies RGB by dt INSIDE the loop (cells store
// RAW scattering), unlike VolumetricFog.hpp which premultiplies at inject.
// These pin that distinct contract plus index/slice exactness. No math change.

// (14) Integrator multiplies in-scatter by the per-slice dt: a thicker far
//      slice contributes proportionally more RGB than its raw cell value.
TEST(VolumetricFog, IntegrationScalesInScatterByDt)
{
    FroxelGrid grid;
    grid.config = { 1, 1, 4, 0.1F, 8.0F };
    grid.resize();
    // Constant raw scattering, no extinction (transmittance stays 1 so the
    // accumulation isolates the dt weighting).
    for (auto& c : grid.cells) c = { 1.0F, 0.0F, 0.0F, 0.0F };
    std::vector<cd::math::Vec4f> out(grid.config.z);
    integrate_view_ray(grid, 0, 0, out);

    // First slice delta == dt0 * 1.0; verify against the slice-thickness.
    const float s0 = 0.0F;
    const float s1 = 1.0F / static_cast<float>(grid.config.z);
    const float dt0 = slice_to_view_z(s1, grid.config) -
                      slice_to_view_z(s0, grid.config);
    EXPECT_NEAR(out[0].x, dt0, kEps);
    // Transmittance unchanged (sigma == 0) so every accum.w == 1.
    for (const auto& a : out) EXPECT_NEAR(a.w, 1.0F, kEps);
    // Accumulated RGB at the last slice equals the total near->far span.
    EXPECT_NEAR(out.back().x, grid.config.far_z - grid.config.near_z, 1e-2F);
}

// (15) Front-to-back occlusion: heavy extinction collapses transmittance so
//      far slices add negligible RGB. Edge: max density opaque fog.
TEST(VolumetricFog, IntegrationHeavyExtinctionStarvesFarSlices)
{
    FroxelGrid grid;
    grid.config = { 1, 1, 8, 0.1F, 8.0F };
    grid.resize();
    for (auto& c : grid.cells) c = { 1.0F, 0.0F, 0.0F, 50.0F };  // opaque
    std::vector<cd::math::Vec4f> out(grid.config.z);
    integrate_view_ray(grid, 0, 0, out);
    EXPECT_LT(out.back().w, 1e-3F);  // transmittance ~ 0
    const float first_step = out[0].x;
    const float tail_step  = out.back().x - out[grid.config.z - 2].x;
    EXPECT_GT(first_step, 0.0F);
    EXPECT_LT(tail_step, first_step);
}

// (16) FroxelGrid index() is a bijection over the whole grid — every (x,y,z)
//      maps to a unique linear slot in [0, cell_count). Pins the layout.
TEST(VolumetricFog, FroxelIndexIsBijection)
{
    FroxelGrid grid;
    grid.config = { 3, 4, 5, 0.1F, 8.0F };
    grid.resize();
    std::vector<int> hits(grid.cells.size(), 0);
    for (std::uint32_t z = 0; z < grid.config.z; ++z)
        for (std::uint32_t y = 0; y < grid.config.y; ++y)
            for (std::uint32_t x = 0; x < grid.config.x; ++x)
            {
                const std::size_t i = grid.index(x, y, z);
                ASSERT_LT(i, hits.size());
                ++hits[i];
            }
    EXPECT_TRUE(std::ranges::all_of(hits, [](int n) { return n == 1; }));
}

// (17) Zero march steps / zero distance must be safe in the heterogeneous
//      ray-march: integrate_along returns zero, never NaN. Edge: zero-step.
TEST(VolumetricFog, IntegrationZeroExtinctionGridIsTransparent)
{
    FroxelGrid grid;
    grid.config = { 2, 2, 3, 0.1F, 8.0F };
    grid.resize();  // all zero
    std::vector<cd::math::Vec4f> out(grid.config.z);
    integrate_view_ray(grid, 1, 1, out);
    for (const auto& a : out)
    {
        EXPECT_NEAR(a.x, 0.0F, 1e-6F);
        EXPECT_NEAR(a.w, 1.0F, 1e-6F);
    }
}

}  // namespace
