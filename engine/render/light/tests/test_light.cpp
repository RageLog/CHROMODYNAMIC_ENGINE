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

// ---- ColorTemperature boundary branches (B4 topup) -------------------------

TEST(ColorTemperature, ClampsBelowAndAboveValidRange)
{
    // Inputs outside [1000, 15000] K clamp to the endpoints, so a
    // sub-1000 K request equals the 1000 K result and a >15000 K request
    // equals the 15000 K result (the std::clamp guard branch).
    const auto cold = cd::light::cct_to_linear_rgb(500.0F);
    const auto k1000 = cd::light::cct_to_linear_rgb(1000.0F);
    EXPECT_FLOAT_EQ(cold.x, k1000.x);
    EXPECT_FLOAT_EQ(cold.z, k1000.z);

    const auto hot = cd::light::cct_to_linear_rgb(30000.0F);
    const auto k15000 = cd::light::cct_to_linear_rgb(15000.0F);
    EXPECT_FLOAT_EQ(hot.x, k15000.x);
    EXPECT_FLOAT_EQ(hot.z, k15000.z);
}

TEST(ColorTemperature, AllChannelsNonNegativeAcrossRange)
{
    // The final std::max(0, .) gamut clamp must hold for every CCT — the
    // y-branch boundaries (2222 K, 4000 K) and the x-branch boundary
    // (4000 K) are all crossed by this sweep.
    for (const float k : { 1000.0F, 2222.0F, 2223.0F, 4000.0F, 4001.0F,
                           6500.0F, 15000.0F })
    {
        const auto c = cd::light::cct_to_linear_rgb(k);
        EXPECT_GE(c.x, 0.0F) << "k=" << k;
        EXPECT_GE(c.y, 0.0F) << "k=" << k;
        EXPECT_GE(c.z, 0.0F) << "k=" << k;
    }
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
    const float ci = std::cos(0.3F);
    const float co = std::cos(0.6F);
    const float ct = std::cos(0.1F);  // inside inner cone
    EXPECT_FLOAT_EQ(cd::light::cone_attenuation(ct, ci, co), 1.0F);
}

TEST(Attenuation, ConeZeroOutsideOuter)
{
    const float ci = std::cos(0.3F);
    const float co = std::cos(0.6F);
    const float ct = std::cos(0.9F);  // outside outer cone
    EXPECT_FLOAT_EQ(cd::light::cone_attenuation(ct, ci, co), 0.0F);
}

TEST(Attenuation, LumensToPointMatchesFrostbiteFormula)
{
    // 1257 lumens ≈ 100 intensity (1257 / 4π).
    const float i = cd::light::lumens_to_point_intensity(1257.0F);
    EXPECT_NEAR(i, 100.0F, 0.5F);
}

// ---- Attenuation edge branches (B4 topup) ----------------------------------

TEST(Attenuation, DistanceNonPositiveRangeIsZero)
{
    // range <= 0 short-circuits to 0 (the early-out branch the prior
    // tests never exercised — they always passed range = 10).
    EXPECT_FLOAT_EQ(cd::light::distance_attenuation(5.0F, 0.0F), 0.0F);
    EXPECT_FLOAT_EQ(cd::light::distance_attenuation(5.0F, -2.0F), 0.0F);
}

TEST(Attenuation, DistanceClampsWindowBeyondRange)
{
    // For d > range the windowing term saturates to 0 (1 - (d/r)^4 < 0
    // clamped), so the falloff is exactly 0 past the cutoff — no negative
    // "anti-light" leak. d == range is the boundary (already covered);
    // this pins d strictly beyond it.
    EXPECT_FLOAT_EQ(cd::light::distance_attenuation(20.0F, 10.0F), 0.0F);
}

TEST(Attenuation, ConeDegenerateDenomFallsBackToBinary)
{
    // cos_inner == cos_outer makes the smooth band collapse; the helper
    // must not divide by ~0. Inside-or-equal the outer edge -> 1, strictly
    // below -> 0 (the denom <= 1e-5 guard branch).
    const float c = std::cos(0.5F);
    EXPECT_FLOAT_EQ(cd::light::cone_attenuation(c, c, c), 1.0F);
    EXPECT_FLOAT_EQ(cd::light::cone_attenuation(c - 0.1F, c, c), 0.0F);
}

TEST(Attenuation, SpotIntensityMalformedConeFallsBackToPoint)
{
    // cos_outer == 1 makes the spot solid-angle term ~0; the helper must
    // fall back to the point formula instead of dividing by ~0.
    const float lumens = 1257.0F;
    EXPECT_NEAR(cd::light::lumens_to_spot_intensity(lumens, 1.0F),
                cd::light::lumens_to_point_intensity(lumens), 1e-3F);
    // A normal cone (cos_outer < 1) gives a *brighter* per-sr intensity
    // than the omnidirectional point fallback (energy into a narrower cone).
    EXPECT_GT(cd::light::lumens_to_spot_intensity(lumens, 0.7F),
              cd::light::lumens_to_point_intensity(lumens));
}

TEST(Attenuation, LuxToDirectionalIsIdentity)
{
    EXPECT_FLOAT_EQ(cd::light::lux_to_directional_intensity(50000.0F), 50000.0F);
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

// ---- ClusterGrid data-only branches (B4 topup; SEAL data-only-v1) ----------
// NOTE: per ADR-20260616-band4-render-features-scope §1, cd::light::ClusterGrid
// is the data-only froxel copy that migrates to the owner cd::lighting_clusters.
// These tests pin its CURRENT data-only contract (no XY cull, fixed-32 overflow)
// so the future migration is behaviour-anchored, not a regression.

TEST(ClusterGrid, OverflowCountedNotDroppedSilently)
{
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d; d.tiles_x = 1; d.tiles_y = 1; d.slices_z = 1;
    g.configure(d);
    g.clear();
    // A directional light hits the single cell once per assign; push more
    // than kMaxLightsPerCluster to overflow the fixed-size list.
    const auto dir = cd::light::directional({ 0, -1, 0 });
    for (std::uint32_t i = 0; i < cd::light::kMaxLightsPerCluster + 5; ++i)
        g.assign(i, dir, { 0, 0, 0 });
    const auto& cell = g.cells()[0];
    EXPECT_EQ(cell.light_count, cd::light::kMaxLightsPerCluster);
    EXPECT_EQ(cell.overflow, 5u);  // surplus counted, not silently lost
}

TEST(ClusterGrid, PointLightCoversAllXyInItsZSlices)
{
    // Data-only copy: a point light adds itself to every XY tile in its
    // Z-slice range (no XY cull — the documented data-only limitation that
    // the owner cd::lighting_clusters fixes with a tight AABB test).
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d;
    d.tiles_x = 4; d.tiles_y = 4; d.slices_z = 8;
    d.near_z = 0.1F; d.far_z = 100.0F;
    g.configure(d);
    g.clear();
    const auto p = cd::light::point({ 0, 0, 0 }, { 1, 1, 1 }, 800.0F, 2.0F);
    // Place the light at view-Z = 10 with a small range so it spans a
    // narrow Z band; every XY tile inside that band must receive it.
    g.assign(0, p, { 0.0F, 0.0F, 10.0F });
    std::uint32_t touched_slices = 0;
    for (std::uint32_t sz = 0; sz < d.slices_z; ++sz)
    {
        const auto& first = g.cells()[g.index_(0, 0, sz)];
        if (first.light_count == 0) continue;
        ++touched_slices;
        // If any cell in this slice has the light, ALL XY cells in the
        // slice must (no XY culling in the data-only copy).
        for (std::uint32_t ty = 0; ty < d.tiles_y; ++ty)
            for (std::uint32_t tx = 0; tx < d.tiles_x; ++tx)
                EXPECT_EQ(g.cells()[g.index_(tx, ty, sz)].light_count, 1u);
    }
    EXPECT_GT(touched_slices, 0u);
}

TEST(ClusterGrid, SliceZRangeInvertsSliceOf)
{
    // slice_z_range is the GPU reconstruction inverse of slice_of: the
    // midpoint of slice s must map back to s (previously untested).
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d;
    d.near_z = 1.0F; d.far_z = 100.0F; d.slices_z = 8;
    g.configure(d);
    for (std::uint32_t s = 0; s < d.slices_z; ++s)
    {
        const auto [z0, z1] = g.slice_z_range(s);
        EXPECT_LT(z0, z1);
        const float mid = std::sqrt(z0 * z1);  // log-space midpoint
        EXPECT_EQ(g.slice_of(mid), s);
    }
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

// ---- CascadedShadow split branches (B4 topup) ------------------------------

TEST(CascadedShadow, SplitCountClampsToMaxCascades)
{
    // A request beyond kMaxCascades clamps; index kMaxCascades still holds
    // far_z and the entries past the clamp stay default (0) — the
    // std::min(cascade_count, kMaxCascades) guard branch.
    const auto splits = cd::light::practical_split_distances(1.0F, 100.0F, 99, 0.75F);
    EXPECT_FLOAT_EQ(splits[0], 1.0F);
    EXPECT_FLOAT_EQ(splits[cd::light::kMaxCascades], 100.0F);
}

TEST(CascadedShadow, PureUniformVsPureLogBracketBlend)
{
    // lambda = 0 -> pure uniform (evenly spaced); lambda = 1 -> pure log
    // (front-loaded). The default 0.75 blend must sit between the two for
    // an interior split — exercises both ends of the lambda interpolation.
    const auto uni = cd::light::practical_split_distances(1.0F, 100.0F, 4, 0.0F);
    const auto logd = cd::light::practical_split_distances(1.0F, 100.0F, 4, 1.0F);
    const auto mid = cd::light::practical_split_distances(1.0F, 100.0F, 4, 0.75F);
    // Pure uniform first split is the arithmetic step; pure log is smaller
    // (denser near camera). The blend lies strictly between.
    EXPECT_GT(uni[1], logd[1]);
    EXPECT_GT(mid[1], logd[1]);
    EXPECT_LT(mid[1], uni[1]);
    // Endpoints are identical regardless of lambda.
    EXPECT_FLOAT_EQ(uni[0], logd[0]);
    EXPECT_FLOAT_EQ(uni[4], logd[4]);
}

// ---- Attenuation — depth tests (ADD-ONLY, no formula change) ---------------

TEST(Attenuation, DistanceZeroReturnsFiniteValue)
{
    // d == 0: the epsilon guard (d^2 + epsilon) prevents division by zero;
    // result must be finite and large (inverse-square singularity guarded).
    const float val = cd::light::distance_attenuation(0.0F, 10.0F);
    EXPECT_TRUE(std::isfinite(val));
    EXPECT_GT(val, 0.0F);
    // Window at d=0 is 1; result = 1 / epsilon ≈ 1/0.01 = 100.
    constexpr float kEpsilon = 0.01F;
    EXPECT_NEAR(val, 1.0F / kEpsilon, 1.0F);  // tolerance 1.0 to allow small floating drift
}

TEST(Attenuation, DistanceIsMonotonicallyDecreasing)
{
    // Closer distances yield more attenuation (higher value) — the
    // inverse-square kernel must decrease as d grows inside [0, range).
    const float range = 20.0F;
    const float a1 = cd::light::distance_attenuation(1.0F, range);
    const float a5 = cd::light::distance_attenuation(5.0F, range);
    const float a10 = cd::light::distance_attenuation(10.0F, range);
    EXPECT_GT(a1, a5);
    EXPECT_GT(a5, a10);
}

TEST(Attenuation, ConeAttenuation_MidpointIsBetweenZeroAndOne)
{
    // A cos_theta exactly halfway between inner and outer must give
    // a result in (0, 1) — the smooth ramp branch.
    const float ci = std::cos(0.2F);   // inner (smaller angle → higher cos)
    const float co = std::cos(0.6F);   // outer (larger angle → lower cos)
    const float mid_theta = 0.4F;      // between 0.2 and 0.6
    const float ct = std::cos(mid_theta);
    const float att = cd::light::cone_attenuation(ct, ci, co);
    EXPECT_GT(att, 0.0F);
    EXPECT_LT(att, 1.0F);
}

// ---- Planckian — depth tests (ADD-ONLY, no formula change) -----------------

TEST(ColorTemperature, TungstenIsRedDominantWithGreenAboveBlue)
{
    // ~2700 K tungsten: R > G > B is the expected chromaticity ordering for
    // incandescent light (first y-branch of the Krystek polynomial, T<=2222K
    // is NOT exercised here because 2700 > 2222; exercises the 2222<T<=4000 branch).
    const auto c = cd::light::cct_to_linear_rgb(2700.0F);
    EXPECT_GT(c.x, c.y);  // R > G
    EXPECT_GT(c.y, c.z);  // G > B
}

TEST(ColorTemperature, BranchBoundary4001KIsValid)
{
    // 4001 K crosses the x/y polynomial branch boundary (T > 4000 branch).
    // Result must be all non-negative and G should be the dominant channel
    // (neutral daylight approaching white).
    const auto c = cd::light::cct_to_linear_rgb(4001.0F);
    EXPECT_GE(c.x, 0.0F);
    EXPECT_GE(c.y, 0.0F);
    EXPECT_GE(c.z, 0.0F);
    // Near-neutral: no channel dominates dramatically.
    EXPECT_LT(c.x, 2.0F);
    EXPECT_LT(c.z, 2.0F);
}

TEST(ColorTemperature, WarmTooCoolRatioDeclinesMonotonically)
{
    // R/B ratio must decrease as temperature rises: warmer = more red,
    // cooler = more blue. Spot-check a few well-separated temperatures.
    const auto k2000 = cd::light::cct_to_linear_rgb(2000.0F);
    const auto k5500 = cd::light::cct_to_linear_rgb(5500.0F);
    const auto k10000 = cd::light::cct_to_linear_rgb(10000.0F);
    // R/B strictly decreasing; guard against zero B with max(b, 1e-6).
    const float rb2000  = k2000.x  / std::max(k2000.z,  1e-6F);
    const float rb5500  = k5500.x  / std::max(k5500.z,  1e-6F);
    const float rb10000 = k10000.x / std::max(k10000.z, 1e-6F);
    EXPECT_GT(rb2000, rb5500);
    EXPECT_GT(rb5500, rb10000);
}

// ---- CascadedShadow — depth tests (ADD-ONLY, no formula change) ------------

TEST(CascadedShadow, SingleCascadeYieldsTwoSplits)
{
    // cascade_count = 1 → splits[0] = near, splits[1] = far.
    const auto splits = cd::light::practical_split_distances(0.5F, 50.0F, 1, 0.75F);
    EXPECT_FLOAT_EQ(splits[0], 0.5F);
    EXPECT_FLOAT_EQ(splits[1], 50.0F);
}

TEST(CascadedShadow, BuildCascadesDistancesAreMonotone)
{
    // Each cascade's far_distance must equal the next cascade's near_distance,
    // and all distances must be positive and ascending.
    const auto ivp = cd::math::Mat4f::identity();
    auto cascades = cd::light::build_cascades(ivp, 1.0F, 200.0F,
                                              { 0.0F, -1.0F, 0.0F }, 4);
    ASSERT_EQ(cascades.size(), 4u);
    for (std::size_t i = 0; i < cascades.size(); ++i)
    {
        EXPECT_GT(cascades[i].far_distance, cascades[i].near_distance) << "i=" << i;
        EXPECT_GT(cascades[i].near_distance, 0.0F) << "i=" << i;
    }
    for (std::size_t i = 1; i < cascades.size(); ++i)
        EXPECT_FLOAT_EQ(cascades[i].near_distance, cascades[i - 1].far_distance) << "i=" << i;
}

// ---- ClusterGrid — depth tests (ADD-ONLY, no formula change) ---------------

TEST(ClusterGrid, SliceOfNearZIsZero)
{
    // A view_z exactly at near_z maps to slice 0 (the std::max + log(1) == 0 path).
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d;
    d.near_z = 1.0F; d.far_z = 100.0F; d.slices_z = 8;
    g.configure(d);
    EXPECT_EQ(g.slice_of(1.0F), 0u);
}

TEST(ClusterGrid, ClearResetsOverflow)
{
    // After filling a single cell past its capacity, clear() must reset
    // both light_count and overflow to 0.
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d;
    d.tiles_x = 1; d.tiles_y = 1; d.slices_z = 1;
    g.configure(d);
    g.clear();
    const auto dir = cd::light::directional({ 0, -1, 0 });
    for (std::uint32_t i = 0; i <= cd::light::kMaxLightsPerCluster; ++i)
        g.assign(i, dir, { 0, 0, 0 });
    // confirm overflow registered before clear
    EXPECT_GT(g.cells()[0].overflow, 0u);
    g.clear();
    EXPECT_EQ(g.cells()[0].light_count, 0u);
    EXPECT_EQ(g.cells()[0].overflow,    0u);
}

TEST(ClusterGrid, IndexFormulaIsDeterministic)
{
    // index_(x, y, z) = (z * tiles_y + y) * tiles_x + x.
    // Verify specific known triples for a 4×3×2 grid.
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d;
    d.tiles_x = 4; d.tiles_y = 3; d.slices_z = 2;
    g.configure(d);
    EXPECT_EQ(g.index_(0, 0, 0), 0u);
    EXPECT_EQ(g.index_(1, 0, 0), 1u);
    EXPECT_EQ(g.index_(0, 1, 0), 4u);   // y=1 → +tiles_x
    EXPECT_EQ(g.index_(0, 0, 1), 12u);  // z=1 → +tiles_x*tiles_y = 4*3
}

TEST(ClusterGrid, SliceZRangeLowerBoundIsMonotone)
{
    // For consecutive slices, the near bound of slice s+1 equals the far
    // bound of slice s — no gaps or overlaps in the log partition.
    cd::light::ClusterGrid g;
    cd::light::ClusterGridDesc d;
    d.near_z = 0.5F; d.far_z = 200.0F; d.slices_z = 6;
    g.configure(d);
    for (std::uint32_t s = 0; s + 1 < d.slices_z; ++s)
    {
        const auto [z0_curr, z1_curr] = g.slice_z_range(s);
        const auto [z0_next, z1_next] = g.slice_z_range(s + 1);
        EXPECT_NEAR(z1_curr, z0_next, 1e-4F) << "slice boundary gap at s=" << s;
        (void)z1_next;
    }
}
