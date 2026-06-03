// =============================================================================
// CHROMODYNAMIC — test_lighting_clusters.cpp
// phase660 — cd::render::lighting_clusters unit tests.
//
// Pattern: Arrange / Act / Assert. Fully isolated. No sleep_for.
// =============================================================================
#include <cd/render/lighting_clusters/LightingClusters.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numeric>

using namespace cd::render::lighting_clusters;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

/// Identity view-proj matrix (column-major, world == clip for these tests).
/// For simple cluster-math tests, we want a canonical "look down -Z" camera
/// where the view-proj roughly maps:
///   view_z → clip_w (for depth)  via the w row = [0, 0, -1, 0]
/// We use a simplified orthographic-like matrix where w = -z_world so that
/// depth_view = clip.w = -z_world.  A point at (0, 0, -d) yields depth d.
///
/// Column-major layout:  m[col * 4 + row]
///   col0 = (1, 0, 0, 0)
///   col1 = (0, 1, 0, 0)
///   col2 = (0, 0, 0,-1)   ← -z_world goes into w
///   col3 = (0, 0, 0, 0)
constexpr std::array<float, 16> make_identity_view_proj() noexcept
{
    std::array<float, 16> m{};
    // col 0 — maps x
    m[0]  = 1.0F;
    // col 1 — maps y
    m[5]  = 1.0F;
    // col 2 — maps -z_world into w (row 3 of col 2 = index 11)
    m[11] = -1.0F;
    // col 3 — all zeros (no translation contribution to w)
    return m;
}

/// Build a PointLight centred at (x, y, z) with given radius (world space).
PointLight make_light(float x, float y, float z, float radius = 1.0F) noexcept
{
    PointLight l;
    l.position  = { x, y, z };
    l.radius    = radius;
    l.color     = { 1.0F, 1.0F, 1.0F };
    l.intensity = 1.0F;
    return l;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: Empty light list → empty / zeroed assignment
// ---------------------------------------------------------------------------
TEST(LightingClusters, EmptyLightList)
{
    Clusterer clusterer;
    clusterer.configure({ 8, 4, 8, 0.1F, 100.0F });

    const auto vp = make_identity_view_proj();
    const LightAssignment result = clusterer.assign({}, vp);

    // offset array must be sized total_clusters + 1, all zero.
    const std::uint32_t total = clusterer.total_cluster_count();  // 8*4*8 = 256
    EXPECT_EQ(result.offset_per_cluster.size(), static_cast<std::size_t>(total) + 1U);
    EXPECT_TRUE(result.light_indices_per_cluster.empty());

    for (const auto v : result.offset_per_cluster)
        EXPECT_EQ(v, 0U);
}

// ---------------------------------------------------------------------------
// Test 2: Single light at origin (0, 0, -5) with radius 1 populates clusters
// ---------------------------------------------------------------------------
TEST(LightingClusters, SingleLightAtOriginPopulatesClusters)
{
    // Grid: 16 x 9 x 24, near=0.1, far=1000.
    Clusterer clusterer;
    clusterer.configure({ 16, 9, 24, 0.1F, 1000.0F });

    const auto vp = make_identity_view_proj();
    // Light at (0, 0, -5) → depth_view = 5.0 (clip.w = -z_world = 5).
    const PointLight light = make_light(0.0F, 0.0F, -5.0F, 1.0F);
    const std::span<const PointLight> lights_span{ &light, 1U };

    const LightAssignment result = clusterer.assign(lights_span, vp);

    // Offset array must be correct size.
    const std::uint32_t total = clusterer.total_cluster_count();
    EXPECT_EQ(result.offset_per_cluster.size(), static_cast<std::size_t>(total) + 1U);

    // At least one cluster must contain the light.
    bool any_assigned = false;
    for (std::uint32_t idx = 0U; idx < total; ++idx)
    {
        const auto lit = clusterer.lights_in_cluster(idx, result);
        if (!lit.empty())
        {
            EXPECT_EQ(lit[0], 0U);  // light index 0
            any_assigned = true;
        }
    }
    EXPECT_TRUE(any_assigned) << "Light at (0,0,-5) should map to at least one cluster";
}

// ---------------------------------------------------------------------------
// Test 3: Light at near-plane boundary spans multiple depth slices
// ---------------------------------------------------------------------------
TEST(LightingClusters, LightAtNearPlaneBoundarySpansMultipleSlices)
{
    // Use a very small grid (4x4x4) for easy reasoning.
    Clusterer clusterer;
    clusterer.configure({ 4, 4, 4, 1.0F, 100.0F });

    const auto vp = make_identity_view_proj();
    // Light centred exactly at near_plane (depth=1.0), radius=2.0 → spans [near, 3.0].
    // This sphere straddles the near plane boundary so multiple z-slices are expected.
    const PointLight light = make_light(0.0F, 0.0F, -1.0F, 2.0F);
    const std::span<const PointLight> lights_span{ &light, 1U };

    const LightAssignment result = clusterer.assign(lights_span, vp);

    // Count clusters with at least one assigned light.
    const std::uint32_t total = clusterer.total_cluster_count();
    std::uint32_t cluster_count = 0U;
    for (std::uint32_t idx = 0U; idx < total; ++idx)
        if (!clusterer.lights_in_cluster(idx, result).empty())
            ++cluster_count;

    // Sphere radius 2 at near=1 covers z-slices 0 and 1 at minimum.
    EXPECT_GE(cluster_count, 2U)
        << "Sphere crossing near-plane should span multiple clusters; got " << cluster_count;
}

// ---------------------------------------------------------------------------
// Test 4: Offset + index arrays are consistent (prefix-sum invariant)
// ---------------------------------------------------------------------------
TEST(LightingClusters, OffsetAndIndexArraysAreConsistent)
{
    Clusterer clusterer;
    clusterer.configure({ 8, 4, 8, 0.1F, 200.0F });

    const auto vp = make_identity_view_proj();

    // Place 5 lights at different depths spread across the frustum.
    std::array<PointLight, 5> lights = {
        make_light( 0.0F,  0.0F, -10.0F,  2.0F),
        make_light(-3.0F,  1.0F, -30.0F,  5.0F),
        make_light( 5.0F, -2.0F, -80.0F, 10.0F),
        make_light( 1.0F,  1.0F, -50.0F,  3.0F),
        make_light(-1.0F, -1.0F, -15.0F,  4.0F),
    };
    const std::span<const PointLight> lights_span{ lights.data(), lights.size() };

    const LightAssignment result = clusterer.assign(lights_span, vp);

    const std::uint32_t total = clusterer.total_cluster_count();
    ASSERT_EQ(result.offset_per_cluster.size(), static_cast<std::size_t>(total) + 1U);

    // Prefix-sum invariant: offset[k+1] >= offset[k] for all k.
    for (std::uint32_t k = 0U; k < total; ++k)
        EXPECT_GE(result.offset_per_cluster[k + 1U], result.offset_per_cluster[k])
            << "offset_per_cluster is not monotone at k=" << k;

    // Total count matches size of packed index array.
    const std::uint32_t total_assignments = result.offset_per_cluster[total];
    EXPECT_EQ(total_assignments, static_cast<std::uint32_t>(result.light_indices_per_cluster.size()));

    // Every stored light index must be in [0, num_lights).
    for (const auto idx : result.light_indices_per_cluster)
        EXPECT_LT(idx, static_cast<std::uint32_t>(lights.size()))
            << "Light index " << idx << " out of range";
}

// ---------------------------------------------------------------------------
// Test 5: configure() round-trip — grid parameters are preserved
// ---------------------------------------------------------------------------
TEST(LightingClusters, ConfigureRoundTrip)
{
    Clusterer clusterer;
    const ClusterGrid cfg{ 32, 18, 48, 0.05F, 500.0F };
    clusterer.configure(cfg);

    const ClusterGrid& stored = clusterer.grid();
    EXPECT_EQ(stored.x_tiles,    cfg.x_tiles);
    EXPECT_EQ(stored.y_tiles,    cfg.y_tiles);
    EXPECT_EQ(stored.z_slices,   cfg.z_slices);
    EXPECT_FLOAT_EQ(stored.near_plane, cfg.near_plane);
    EXPECT_FLOAT_EQ(stored.far_plane,  cfg.far_plane);

    EXPECT_EQ(clusterer.total_cluster_count(), 32U * 18U * 48U);
}

// ---------------------------------------------------------------------------
// Test 6: cluster_index() linearises correctly
// ---------------------------------------------------------------------------
TEST(LightingClusters, ClusterIndexLinearisation)
{
    Clusterer clusterer;
    clusterer.configure({ 4, 3, 2, 0.1F, 100.0F });

    // Index formula: (z * y_tiles + ty) * x_tiles + tx
    EXPECT_EQ(clusterer.cluster_index(0, 0, 0), 0U);
    EXPECT_EQ(clusterer.cluster_index(1, 0, 0), 1U);
    EXPECT_EQ(clusterer.cluster_index(3, 2, 1), 1U * 3U * 4U + 2U * 4U + 3U); // 4+8+3=23? let's compute
    // (1 * 3 + 2) * 4 + 3 = 5 * 4 + 3 = 23
    EXPECT_EQ(clusterer.cluster_index(3, 2, 1), 23U);
    EXPECT_EQ(clusterer.total_cluster_count(), 4U * 3U * 2U); // 24
}

// ---------------------------------------------------------------------------
// Test 7: Light entirely outside frustum is not assigned to any cluster
// ---------------------------------------------------------------------------
TEST(LightingClusters, LightBeyondFarPlaneNotAssigned)
{
    Clusterer clusterer;
    clusterer.configure({ 8, 4, 8, 0.1F, 50.0F });

    const auto vp = make_identity_view_proj();
    // Light at depth 200 with radius 1 — entirely beyond far_plane=50.
    const PointLight light = make_light(0.0F, 0.0F, -200.0F, 1.0F);
    const std::span<const PointLight> lights_span{ &light, 1U };

    const LightAssignment result = clusterer.assign(lights_span, vp);

    EXPECT_TRUE(result.light_indices_per_cluster.empty())
        << "Light beyond far plane should not be assigned to any cluster";
}

// ---------------------------------------------------------------------------
// Test 8: Many lights — moment test: 200 lights, total assignments << 200 × total
// ---------------------------------------------------------------------------
TEST(LightingClusters, ManyLightsAssignmentsAreSublinear)
{
    // This test validates the core Forward+ promise:
    // With 200 lights spread through the scene, each cluster only sees a
    // small fraction of the total lights (not all 200).
    Clusterer clusterer;
    clusterer.configure({ 16, 9, 24, 0.1F, 1000.0F });

    const auto vp = make_identity_view_proj();

    // Create 200 lights at varying positions and depths.
    std::vector<PointLight> lights;
    lights.reserve(200U);
    for (std::uint32_t i = 0U; i < 200U; ++i)
    {
        const float fi     = static_cast<float>(i);
        const float depth  = 5.0F + (fi * 4.5F); // depths 5 to ~900
        const float offset = std::sin(fi * 0.3F) * 2.0F;
        lights.push_back(make_light(offset, offset * 0.5F, -depth, 2.5F));
    }

    const LightAssignment result = clusterer.assign(
        std::span<const PointLight>{ lights.data(), lights.size() }, vp);

    const std::uint32_t total = clusterer.total_cluster_count();
    ASSERT_EQ(result.offset_per_cluster.size(), static_cast<std::size_t>(total) + 1U);

    // Compute max lights in any single cluster.
    std::uint32_t max_per_cluster = 0U;
    std::uint32_t non_empty_clusters = 0U;
    for (std::uint32_t idx = 0U; idx < total; ++idx)
    {
        const auto lit = clusterer.lights_in_cluster(idx, result);
        if (!lit.empty())
        {
            ++non_empty_clusters;
            max_per_cluster = std::max(max_per_cluster,
                                       static_cast<std::uint32_t>(lit.size()));
        }
    }

    // Sanity: at least some clusters are populated.
    EXPECT_GT(non_empty_clusters, 0U);

    // Forward+ promise: no cluster sees all 200 lights.
    // With 200 depth-spread lights, each cluster should see a fraction of the total.
    // We allow up to 75 (37.5% of 200) as an upper bound — the key insight is that
    // this is far less than 200 (the brute-force cost), not that it must be tiny.
    EXPECT_LE(max_per_cluster, 75U)
        << "max lights in a single cluster = " << max_per_cluster
        << " — Forward+ promise broken (expected << 200)";

    // Average lights per non-empty cluster must also be well below 200.
    const std::uint64_t total_assignments = result.light_indices_per_cluster.size();
    const float avg_per_cluster = (non_empty_clusters > 0U)
        ? static_cast<float>(total_assignments) / static_cast<float>(non_empty_clusters)
        : 0.0F;
    EXPECT_LT(avg_per_cluster, 50.0F)
        << "average lights per non-empty cluster = " << avg_per_cluster
        << " — Forward+ savings not achieved";
}
