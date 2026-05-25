// =============================================================================
// CHROMODYNAMIC — cd::light tests (Phase 165)
// =============================================================================
#include <cd/light/Attenuation.hpp>
#include <cd/light/CascadedShadow.hpp>
#include <cd/light/ClusterGrid.hpp>
#include <cd/light/ColorTemperature.hpp>
#include <cd/light/Light.hpp>
#include <gtest/gtest.h>

#include <cmath>

// ---- Light struct layout ---------------------------------------------------

TEST(Light, PackedLayoutIs112Bytes)
{
    static_assert(sizeof(cd::light::Light) == 112);
    EXPECT_EQ(sizeof(cd::light::Light), 112U);
}

TEST(Light, DirectionalNormalizesDirection)
{
    auto l = cd::light::directional({ 0.0F, -10.0F, 0.0F });
    const float len = std::sqrt(l.direction.x*l.direction.x
                              + l.direction.y*l.direction.y
                              + l.direction.z*l.direction.z);
    EXPECT_NEAR(len, 1.0F, 1e-5F);
    EXPECT_FLOAT_EQ(l.direction.y, -1.0F);
    EXPECT_EQ(l.type, cd::light::LightType::kDirectional);
    EXPECT_FLOAT_EQ(l.range, 0.0F);
}

TEST(Light, PointHasInverseSquareDefaults)
{
    auto l = cd::light::point({ 1.0F, 2.0F, 3.0F });
    EXPECT_EQ(l.type, cd::light::LightType::kPoint);
    EXPECT_FLOAT_EQ(l.position.x, 1.0F);
    EXPECT_FLOAT_EQ(l.range, 10.0F);
}

TEST(Light, SpotPrecomputesConeAttenuation)
{
    auto l = cd::light::spot({ 0,0,0 }, { 0,-1,0 }, { 1,1,1 }, 800.0F,
                             10.0F, 0.5F, 0.8F);
    EXPECT_NEAR(l.cos_inner_cone, std::cos(0.5F), 1e-5F);
    EXPECT_NEAR(l.cos_outer_cone, std::cos(0.8F), 1e-5F);
    const float expected = 1.0F / (std::cos(0.5F) - std::cos(0.8F));
    EXPECT_NEAR(l.inv_cone_range, expected, 1e-3F);
}

TEST(Light, RectAreaComputesOrthonormalBasis)
{
    auto l = cd::light::rect_area({ 0,0,0 }, { 0,1,0 }, { 1,0,0 },
                                  2.0F, 1.5F);
    EXPECT_EQ(l.type, cd::light::LightType::kRectArea);
    // tangent and bitangent must be perpendicular to direction.
    const float td = l.direction.x*l.area_tangent.x + l.direction.y*l.area_tangent.y + l.direction.z*l.area_tangent.z;
    const float bd = l.direction.x*l.area_bitangent.x + l.direction.y*l.area_bitangent.y + l.direction.z*l.area_bitangent.z;
    EXPECT_NEAR(td, 0.0F, 1e-5F);
    EXPECT_NEAR(bd, 0.0F, 1e-5F);
    EXPECT_FLOAT_EQ(l.area_width, 2.0F);
    EXPECT_FLOAT_EQ(l.area_height, 1.5F);
}

// ---- ColorTemperature ------------------------------------------------------

TEST(ColorTemperature, D65WhiteIsNearWhite)
{
    auto c = cd::light::cct_to_linear_rgb(6500.0F);
    // Should be roughly equal channels around 1.0.
    EXPECT_GT(c.x, 0.5F);
    EXPECT_GT(c.y, 0.5F);
    EXPECT_GT(c.z, 0.5F);
    // R and G similar, B not too far off.
    EXPECT_NEAR(c.x, c.y, 0.5F);
}

TEST(ColorTemperature, WarmIsRedDominant)
{
    auto c = cd::light::cct_to_linear_rgb(2000.0F);
    EXPECT_GT(c.x, c.z);  // R > B for warm temperatures
}

TEST(ColorTemperature, CoolIsBlueDominant)
{
    auto c = cd::light::cct_to_linear_rgb(10000.0F);
    EXPECT_GT(c.z, c.x);  // B > R for cool temperatures
}

// ---- Attenuation -----------------------------------------------------------

TEST(Attenuation, DistanceZeroAtRange)
{
    EXPECT_NEAR(cd::light::distance_attenuation(10.0F, 10.0F), 0.0F, 1e-3F);
}

TEST(Attenuation, DistancePositiveAtHalfRange)
{
    EXPECT_GT(cd::light::distance_attenuation(5.0F, 10.0F), 0.0F);
}

TEST(Attenuation, ConeOneInsideInner)
{
    const float ci = std::cos(0.3F), co = std::cos(0.6F);
    const float ct = std::cos(0.1F);  // inside inner cone
    EXPECT_FLOAT_EQ(cd::light::cone_attenuation(ct, ci, co), 1.0F);
}

TEST(Attenuation, ConeZeroOutsideOuter)
{
    const float ci = std::cos(0.3F), co = std::cos(0.6F);
    const float ct = std::cos(0.9F);  // outside outer cone
    EXPECT_FLOAT_EQ(cd::light::cone_attenuation(ct, ci, co), 0.0F);
}

TEST(Attenuation, LumensToPointMatchesFrostbiteFormula)
{
    // 1257 lumens ≈ 100 intensity (1257 / 4π).
    const float i = cd::light::lumens_to_point_intensity(1257.0F);
    EXPECT_NEAR(i, 100.0F, 0.5F);
}

// ---- ClusterGrid -----------------------------------------------------------

TEST(ClusterGrid, ConfigureSizesCells)
{
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d;
    d.tiles_x = 4; d.tiles_y = 2; d.slices_z = 4;
    g.configure(d);
    EXPECT_EQ(g.cluster_count(), 32u);
    EXPECT_EQ(g.cells().size(), 32u);
}

TEST(ClusterGrid, DirectionalAssignsToEveryCluster)
{
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d; d.tiles_x = 2; d.tiles_y = 2; d.slices_z = 2;
    g.configure(d);
    g.clear();
    const auto hits = g.assign(0, cd::light::directional({ 0, -1, 0 }), { 0,0,0 });
    EXPECT_EQ(hits, g.cluster_count());
    for (const auto& c : g.cells())
        EXPECT_EQ(c.light_count, 1u);
}

TEST(ClusterGrid, SliceMapsLogarithmically)
{
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d;
    d.near_z = 1.0F; d.far_z = 100.0F; d.slices_z = 4;
    g.configure(d);
    EXPECT_LE(g.slice_of(1.0F), 0u);
    EXPECT_LT(g.slice_of(5.0F), g.slice_of(20.0F));
    EXPECT_EQ(g.slice_of(200.0F), d.slices_z - 1);
}

// ---- CascadedShadow --------------------------------------------------------

TEST(CascadedShadow, PracticalSplitsAreMonotonic)
{
    auto splits = cd::light::practical_split_distances(1.0F, 100.0F, 4, 0.75F);
    for (std::size_t i = 1; i < splits.size(); ++i)
        EXPECT_GT(splits[i], splits[i-1]);
    EXPECT_FLOAT_EQ(splits[0], 1.0F);
    EXPECT_FLOAT_EQ(splits[4], 100.0F);
}

TEST(CascadedShadow, BuildCascadesReturnsExpectedCount)
{
    const auto ivp = cd::math::Mat4f::identity();
    auto cascades = cd::light::build_cascades(ivp, 1.0F, 100.0F,
                                              { 0.0F, -1.0F, 0.0F }, 4);
    EXPECT_EQ(cascades.size(), 4u);
    EXPECT_FLOAT_EQ(cascades[0].near_distance, 1.0F);
    EXPECT_FLOAT_EQ(cascades.back().far_distance, 100.0F);
}
