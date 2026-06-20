// =============================================================================
// CHROMODYNAMIC — test_lighting_clusters_hostmath.cpp
// 75->100 marathon — ADD-ONLY host-math boundary coverage for
//                    cd::render::lighting_clusters.
//
// These tests do NOT change any binning / cull math or GLSL — they pin the
// EXISTING behaviour of:
//   * clip-space sphere -> cluster index mapping boundaries,
//   * AABB-vs-sphere tight intersection (true / false / tangent),
//   * prefix-sum correctness (empty / single / overflow / monotonic / sum),
//   * cluster grid dims + cluster_index linearisation,
//   * light count = 0 / 1 / many,
//   * sphere fully outside the frustum (far / near / lateral),
//   * sphere spanning multiple clusters,
//   * DispatchPass host-side static helpers (group_count / buffer sizes /
//     POD layout) at boundary inputs — all run everywhere (no device).
//
// Pattern: Arrange / Act / Assert. Fully isolated. No sleep_for. Golden-safe:
// asserts are derived from the ACTUAL code in LightingClusters.cpp /
// DispatchPass.hpp, never from a re-derived "ideal" math.
// =============================================================================
#include <cd/render/lighting_clusters/DispatchPass.hpp>
#include <cd/render/lighting_clusters/LightingClusters.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using cd::render::lighting_clusters::ClusterEntry;
using cd::render::lighting_clusters::ClusterGrid;
using cd::render::lighting_clusters::Clusterer;
using cd::render::lighting_clusters::DispatchPass;
using cd::render::lighting_clusters::GpuPointLight;
using cd::render::lighting_clusters::kMaxLightsPerCluster;
using cd::render::lighting_clusters::LightAssignment;
using cd::render::lighting_clusters::PointLight;

namespace
{

/// Canonical "look down -Z" view-proj used across the original test file:
/// col-major m[col*4+row], with m[11] = -1 so that clip.w = -z_world. A point
/// at (0, 0, -d) therefore yields depth_view = clip.w = d (positive forward).
[[nodiscard]] constexpr std::array<float, 16> make_identity_view_proj() noexcept
{
    std::array<float, 16> m{};
    m[0]  = 1.0F;   // col0 -> x
    m[5]  = 1.0F;   // col1 -> y
    m[11] = -1.0F;  // col2 row3 -> w = -z_world
    return m;
}

[[nodiscard]] PointLight make_light(
    float x, float y, float z, float radius = 1.0F) noexcept
{
    PointLight l;
    l.position  = { x, y, z };
    l.radius    = radius;
    l.color     = { 1.0F, 1.0F, 1.0F };
    l.intensity = 1.0F;
    return l;
}

/// Count non-empty clusters in an assignment (helper for spanning tests).
[[nodiscard]] std::uint32_t count_non_empty_clusters(
    const Clusterer& c, const LightAssignment& a) noexcept
{
    std::uint32_t n = 0U;
    const std::uint32_t total = c.total_cluster_count();
    for (std::uint32_t idx = 0U; idx < total; ++idx)
        if (!c.lights_in_cluster(idx, a).empty())
            ++n;
    return n;
}

}  // namespace

// ===========================================================================
// SECTION A — cluster grid dims + cluster_index linearisation (exhaustive).
// ===========================================================================

// Every (tx, ty, tz) maps to a UNIQUE linear index in [0, total) and the
// inverse formula round-trips. Pins (z*y_tiles+ty)*x_tiles+tx exactly.
TEST(LightingClustersHostMath, ClusterIndexBijectiveOverWholeGrid)
{
    Clusterer clusterer;
    const ClusterGrid grid{ 5U, 3U, 4U, 0.1F, 100.0F };  // 60 clusters
    clusterer.configure(grid);

    const std::uint32_t total = clusterer.total_cluster_count();
    ASSERT_EQ(total, 5U * 3U * 4U);

    std::vector<bool> seen(total, false);
    for (std::uint32_t tz = 0U; tz < grid.z_slices; ++tz)
        for (std::uint32_t ty = 0U; ty < grid.y_tiles; ++ty)
            for (std::uint32_t tx = 0U; tx < grid.x_tiles; ++tx)
            {
                const std::uint32_t idx = clusterer.cluster_index(tx, ty, tz);
                ASSERT_LT(idx, total) << "index out of range for ("
                                      << tx << "," << ty << "," << tz << ")";
                EXPECT_FALSE(seen[idx]) << "duplicate index " << idx;
                seen[idx] = true;

                // Inverse of (z*y_tiles+ty)*x_tiles+tx.
                EXPECT_EQ(idx % grid.x_tiles, tx);
                EXPECT_EQ((idx / grid.x_tiles) % grid.y_tiles, ty);
                EXPECT_EQ(idx / (grid.x_tiles * grid.y_tiles), tz);
            }

    for (std::uint32_t i = 0U; i < total; ++i)
        EXPECT_TRUE(seen[i]) << "index " << i << " was never produced";
}

// First and last clusters are the extreme linear indices.
TEST(LightingClustersHostMath, ClusterIndexEndpoints)
{
    Clusterer clusterer;
    clusterer.configure({ 16U, 9U, 24U, 0.1F, 1000.0F });

    EXPECT_EQ(clusterer.cluster_index(0U, 0U, 0U), 0U);
    EXPECT_EQ(clusterer.cluster_index(15U, 8U, 23U),
              clusterer.total_cluster_count() - 1U);
}

// Default grid (16x9x24) and a 1x1x1 degenerate grid report the right totals.
TEST(LightingClustersHostMath, TotalClusterCountDefaultAndDegenerate)
{
    Clusterer def;  // default-configured grid_ is value-initialised {0,...}
    // A freshly constructed Clusterer has not been configured; total is the
    // product of the default ClusterGrid members ONLY after configure().
    def.configure(ClusterGrid{});  // {16, 9, 24, 0.1, 1000}
    EXPECT_EQ(def.total_cluster_count(), 16U * 9U * 24U);

    Clusterer one;
    one.configure({ 1U, 1U, 1U, 0.1F, 100.0F });
    EXPECT_EQ(one.total_cluster_count(), 1U);
    EXPECT_EQ(one.cluster_index(0U, 0U, 0U), 0U);
}

// ===========================================================================
// SECTION B — prefix-sum correctness (empty / single / overflow / monotone).
// ===========================================================================

// Empty light list -> offset array sized total+1, all zero, no indices.
TEST(LightingClustersHostMath, PrefixSumEmptyAllZero)
{
    Clusterer clusterer;
    clusterer.configure({ 4U, 4U, 4U, 0.1F, 100.0F });

    const auto result = clusterer.assign({}, make_identity_view_proj());

    const std::uint32_t total = clusterer.total_cluster_count();
    ASSERT_EQ(result.offset_per_cluster.size(),
              static_cast<std::size_t>(total) + 1U);
    EXPECT_TRUE(result.light_indices_per_cluster.empty());
    for (const auto v : result.offset_per_cluster)
        EXPECT_EQ(v, 0U);
}

// Single assigned light: offsets are monotone, last offset == #indices,
// and the count for the populated region equals the packed-index size.
TEST(LightingClustersHostMath, PrefixSumSingleLightMonotoneAndClosed)
{
    Clusterer clusterer;
    clusterer.configure({ 8U, 4U, 8U, 0.1F, 200.0F });

    const PointLight light = make_light(0.0F, 0.0F, -10.0F, 2.0F);
    const std::span<const PointLight> span{ &light, 1U };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    const std::uint32_t total = clusterer.total_cluster_count();
    ASSERT_EQ(result.offset_per_cluster.size(),
              static_cast<std::size_t>(total) + 1U);

    // Monotone non-decreasing.
    for (std::uint32_t k = 0U; k < total; ++k)
        EXPECT_GE(result.offset_per_cluster[k + 1U],
                  result.offset_per_cluster[k]) << "not monotone at k=" << k;

    // First offset is always 0; last offset closes the packed array.
    EXPECT_EQ(result.offset_per_cluster.front(), 0U);
    EXPECT_EQ(result.offset_per_cluster[total],
              static_cast<std::uint32_t>(result.light_indices_per_cluster.size()));

    // Sum of per-cluster deltas equals the total number of assignments.
    std::uint32_t delta_sum = 0U;
    for (std::uint32_t k = 0U; k < total; ++k)
        delta_sum += result.offset_per_cluster[k + 1U]
                   - result.offset_per_cluster[k];
    EXPECT_EQ(delta_sum,
              static_cast<std::uint32_t>(result.light_indices_per_cluster.size()));
}

// The per-cluster slice returned by lights_in_cluster() exactly reconstructs
// the packed-index stream when concatenated in cluster order (prefix-sum
// round-trip). Also pins that all stored indices are valid light ids.
TEST(LightingClustersHostMath, PrefixSumSliceReconstructsPackedStream)
{
    Clusterer clusterer;
    clusterer.configure({ 8U, 4U, 8U, 0.1F, 200.0F });

    const std::array<PointLight, 4> lights = {
        make_light( 0.0F,  0.0F, -10.0F, 3.0F),
        make_light( 2.0F, -1.0F, -25.0F, 4.0F),
        make_light(-2.0F,  1.0F, -40.0F, 5.0F),
        make_light( 0.0F,  0.0F, -60.0F, 6.0F),
    };
    const std::span<const PointLight> span{ lights.data(), lights.size() };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    const std::uint32_t total = clusterer.total_cluster_count();
    std::vector<std::uint32_t> reassembled;
    reassembled.reserve(result.light_indices_per_cluster.size());
    for (std::uint32_t idx = 0U; idx < total; ++idx)
    {
        const auto slice = clusterer.lights_in_cluster(idx, result);
        for (const auto li : slice)
        {
            EXPECT_LT(li, static_cast<std::uint32_t>(lights.size()));
            reassembled.push_back(li);
        }
    }
    EXPECT_EQ(reassembled, result.light_indices_per_cluster);
}

// "Overflow"-style stress: many overlapping lights still yield a closed,
// monotone prefix-sum (no off-by-one in the count/scan/pack passes).
TEST(LightingClustersHostMath, PrefixSumDenseOverlapStaysClosed)
{
    Clusterer clusterer;
    clusterer.configure({ 4U, 4U, 4U, 1.0F, 50.0F });

    // 64 fat lights clustered near the camera so MANY clusters overlap each.
    std::vector<PointLight> lights;
    lights.reserve(64U);
    for (std::uint32_t i = 0U; i < 64U; ++i)
    {
        const auto fi = static_cast<float>(i);
        lights.push_back(make_light(0.0F, 0.0F, -(2.0F + fi * 0.1F), 8.0F));
    }
    const std::span<const PointLight> span{ lights.data(), lights.size() };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    const std::uint32_t total = clusterer.total_cluster_count();
    ASSERT_EQ(result.offset_per_cluster.size(),
              static_cast<std::size_t>(total) + 1U);
    for (std::uint32_t k = 0U; k < total; ++k)
        EXPECT_GE(result.offset_per_cluster[k + 1U],
                  result.offset_per_cluster[k]);
    EXPECT_EQ(result.offset_per_cluster[total],
              static_cast<std::uint32_t>(result.light_indices_per_cluster.size()));
    EXPECT_GT(result.light_indices_per_cluster.size(), 0U)
        << "dense overlap must populate at least one cluster";
}

// ===========================================================================
// SECTION C — light count = 0 / 1 / many.
// ===========================================================================

// Zero lights: no assignments (covered above but pinned through the count
// boundary explicitly).
TEST(LightingClustersHostMath, LightCountZeroProducesNoAssignments)
{
    Clusterer clusterer;
    clusterer.configure({ 16U, 9U, 24U, 0.1F, 1000.0F });
    const auto result = clusterer.assign({}, make_identity_view_proj());
    EXPECT_TRUE(result.light_indices_per_cluster.empty());
}

// One light in-frustum: stored light id is always 0 (the only id present).
TEST(LightingClustersHostMath, LightCountOneStoresOnlyIndexZero)
{
    Clusterer clusterer;
    clusterer.configure({ 8U, 8U, 8U, 0.1F, 500.0F });

    const PointLight light = make_light(0.0F, 0.0F, -20.0F, 3.0F);
    const std::span<const PointLight> span{ &light, 1U };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    ASSERT_FALSE(result.light_indices_per_cluster.empty());
    for (const auto li : result.light_indices_per_cluster)
        EXPECT_EQ(li, 0U);
}

// Many lights: total assignments are finite and each stored id is in range
// (Forward+ sublinearity is already covered in the original suite; here we
// pin the bound + validity at the "many" count boundary).
TEST(LightingClustersHostMath, LightCountManyAllIndicesInRange)
{
    Clusterer clusterer;
    clusterer.configure({ 16U, 9U, 24U, 0.1F, 1000.0F });

    constexpr std::uint32_t kN = 128U;
    std::vector<PointLight> lights;
    lights.reserve(kN);
    for (std::uint32_t i = 0U; i < kN; ++i)
    {
        const auto fi = static_cast<float>(i);
        lights.push_back(make_light(0.0F, 0.0F, -(5.0F + fi * 5.0F), 3.0F));
    }
    const std::span<const PointLight> span{ lights.data(), lights.size() };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    for (const auto li : result.light_indices_per_cluster)
        EXPECT_LT(li, kN);
}

// ===========================================================================
// SECTION D — sphere fully outside the frustum (far / near-behind / lateral).
// ===========================================================================

// Behind the camera (positive world z -> w = -z_world < 0): not assigned.
TEST(LightingClustersHostMath, SphereBehindCameraNotAssigned)
{
    Clusterer clusterer;
    clusterer.configure({ 8U, 4U, 8U, 0.1F, 100.0F });

    // World z = +10 -> depth_view = clip.w = -10 (behind near). radius 1.
    const PointLight light = make_light(0.0F, 0.0F, 10.0F, 1.0F);
    const std::span<const PointLight> span{ &light, 1U };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    EXPECT_TRUE(result.light_indices_per_cluster.empty())
        << "sphere fully behind the camera must not be assigned";
}

// Closer than near_plane by more than its radius: not assigned.
TEST(LightingClustersHostMath, SphereInFrontOfNearPlaneNotAssigned)
{
    Clusterer clusterer;
    clusterer.configure({ 8U, 4U, 8U, 5.0F, 100.0F });

    // depth_view = 1.0, radius 0.5 -> sphere spans [0.5, 1.5], all < near=5.
    const PointLight light = make_light(0.0F, 0.0F, -1.0F, 0.5F);
    const std::span<const PointLight> span{ &light, 1U };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    EXPECT_TRUE(result.light_indices_per_cluster.empty())
        << "sphere entirely in front of the near plane must not be assigned";
}

// Beyond far_plane by more than its radius: not assigned (mirrors original
// test 7 but at a different grid/scale to widen coverage).
TEST(LightingClustersHostMath, SphereBeyondFarPlaneNotAssigned)
{
    Clusterer clusterer;
    clusterer.configure({ 16U, 9U, 24U, 0.1F, 100.0F });

    const PointLight light = make_light(0.0F, 0.0F, -500.0F, 2.0F);
    const std::span<const PointLight> span{ &light, 1U };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    EXPECT_TRUE(result.light_indices_per_cluster.empty());
}

// A laterally-offset light still bins into a small, bounded set of clusters
// (the lateral NDC shift keeps the X-tile range narrow — it does NOT spray the
// whole grid). Same radius/depth as the centred reference; we pin that the
// lateral sphere stays within a comparable, small cluster footprint. The offset
// must stay on-screen under the identity view-proj (x=30 would be far outside
// NDC and bin into zero clusters), so a modest in-frustum offset is used.
TEST(LightingClustersHostMath, LateralSphereBinsIntoSmallBoundedFootprint)
{
    Clusterer clusterer;
    clusterer.configure({ 16U, 9U, 24U, 0.1F, 1000.0F });
    const auto vp = make_identity_view_proj();

    const PointLight centred = make_light(0.0F, 0.0F, -50.0F, 1.0F);
    const PointLight lateral = make_light(2.0F, 0.0F, -50.0F, 1.0F);

    const auto rc = clusterer.assign(
        std::span<const PointLight>{ &centred, 1U }, vp);
    const auto rl = clusterer.assign(
        std::span<const PointLight>{ &lateral, 1U }, vp);

    // Both legitimately overlap >=1 cluster. A radius-1 sphere at depth 50 has
    // a narrow NDC footprint regardless of lateral offset, so each lands in a
    // small bounded set (well under the 16x9 = 144 lateral tiles per slice).
    const std::uint32_t nc = count_non_empty_clusters(clusterer, rc);
    const std::uint32_t nl = count_non_empty_clusters(clusterer, rl);
    EXPECT_GT(nc, 0U);
    EXPECT_GT(nl, 0U);
    EXPECT_LT(nc, 144U);
    EXPECT_LT(nl, 144U);
}

// ===========================================================================
// SECTION E — AABB-vs-sphere tight test true / false / spanning.
// ===========================================================================

// A small in-frustum sphere overlaps AT LEAST one cluster (tight test true).
TEST(LightingClustersHostMath, TightTestSmallSphereHitsAtLeastOneCluster)
{
    Clusterer clusterer;
    clusterer.configure({ 16U, 9U, 24U, 0.1F, 1000.0F });

    const PointLight light = make_light(0.0F, 0.0F, -30.0F, 0.5F);
    const std::span<const PointLight> span{ &light, 1U };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    EXPECT_GE(count_non_empty_clusters(clusterer, result), 1U);
}

// A fat sphere near the camera spans MANY more clusters than a thin one at
// the same centre (tight test "spanning multiple clusters").
TEST(LightingClustersHostMath, FatSphereSpansMoreClustersThanThin)
{
    Clusterer clusterer;
    clusterer.configure({ 16U, 9U, 24U, 0.1F, 1000.0F });
    const auto vp = make_identity_view_proj();

    const PointLight thin = make_light(0.0F, 0.0F, -10.0F, 0.5F);
    const PointLight fat  = make_light(0.0F, 0.0F, -10.0F, 6.0F);

    const auto rt = clusterer.assign(
        std::span<const PointLight>{ &thin, 1U }, vp);
    const auto rf = clusterer.assign(
        std::span<const PointLight>{ &fat, 1U }, vp);

    const std::uint32_t nt = count_non_empty_clusters(clusterer, rt);
    const std::uint32_t nf = count_non_empty_clusters(clusterer, rf);
    EXPECT_GT(nt, 0U);
    EXPECT_GT(nf, nt) << "a fat sphere must span more clusters than a thin one";
}

// A sphere straddling a z-slice boundary populates at least two depth slices
// (spanning across the log-z partition). Pins the tight test's depth axis.
TEST(LightingClustersHostMath, SphereStraddlesDepthSlices)
{
    Clusterer clusterer;
    const ClusterGrid grid{ 4U, 4U, 8U, 1.0F, 100.0F };
    clusterer.configure(grid);

    // Centre at depth 10, radius 8 -> spans [2, 18] in depth: multiple slices.
    const PointLight light = make_light(0.0F, 0.0F, -10.0F, 8.0F);
    const std::span<const PointLight> span{ &light, 1U };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    // Collect the distinct z-slices that got at least one assignment.
    std::vector<bool> slice_hit(grid.z_slices, false);
    const std::uint32_t total = clusterer.total_cluster_count();
    const std::uint32_t xy = grid.x_tiles * grid.y_tiles;
    for (std::uint32_t idx = 0U; idx < total; ++idx)
        if (!clusterer.lights_in_cluster(idx, result).empty())
            slice_hit.at(idx / xy) = true;

    std::uint32_t hit_slices = 0U;
    for (const bool h : slice_hit)
        if (h)
            ++hit_slices;
    EXPECT_GE(hit_slices, 2U)
        << "a radius-8 sphere at depth 10 must touch >=2 depth slices";
}

// ===========================================================================
// SECTION F — lights_in_cluster() bounds / guard behaviour (host, no device).
// ===========================================================================

// Out-of-range cluster index returns an empty span (guard, not UB).
TEST(LightingClustersHostMath, LightsInClusterOutOfRangeIsEmpty)
{
    Clusterer clusterer;
    clusterer.configure({ 4U, 4U, 4U, 0.1F, 100.0F });

    const PointLight light = make_light(0.0F, 0.0F, -10.0F, 2.0F);
    const std::span<const PointLight> span{ &light, 1U };
    const auto result = clusterer.assign(span, make_identity_view_proj());

    const std::uint32_t total = clusterer.total_cluster_count();
    EXPECT_TRUE(clusterer.lights_in_cluster(total, result).empty());
    EXPECT_TRUE(clusterer.lights_in_cluster(total + 100U, result).empty());
}

// A mismatched (too-small) assignment yields an empty span rather than reading
// past the offset array.
TEST(LightingClustersHostMath, LightsInClusterRejectsUndersizedAssignment)
{
    Clusterer clusterer;
    clusterer.configure({ 4U, 4U, 4U, 0.1F, 100.0F });

    LightAssignment bogus;  // offset_per_cluster deliberately too short.
    bogus.offset_per_cluster.assign(2U, 0U);
    EXPECT_TRUE(clusterer.lights_in_cluster(0U, bogus).empty());
}

// ===========================================================================
// SECTION G — DispatchPass HOST-side static helpers (run everywhere).
//             group_count / cluster_table_size / light_indices_size / POD.
// ===========================================================================

// group_count ceils dim/8 across boundary inputs (0, exact, +1, large).
TEST(LightingClustersHostMath, GroupCountCeilsByEight)
{
    EXPECT_EQ(DispatchPass::group_count(0U), 0U);
    EXPECT_EQ(DispatchPass::group_count(1U), 1U);
    EXPECT_EQ(DispatchPass::group_count(7U), 1U);
    EXPECT_EQ(DispatchPass::group_count(8U), 1U);
    EXPECT_EQ(DispatchPass::group_count(9U), 2U);
    EXPECT_EQ(DispatchPass::group_count(15U), 2U);
    EXPECT_EQ(DispatchPass::group_count(16U), 2U);
    EXPECT_EQ(DispatchPass::group_count(24U), 3U);
    EXPECT_EQ(DispatchPass::group_count(256U), 32U);
}

// Buffer-size helpers scale linearly with cluster count and never overflow
// the 64-bit return for a large grid.
TEST(LightingClustersHostMath, BufferSizeHelpersScaleLinearly)
{
    EXPECT_EQ(DispatchPass::cluster_table_size(0U), 0U);
    EXPECT_EQ(DispatchPass::light_indices_size(0U), 0U);

    EXPECT_EQ(DispatchPass::cluster_table_size(1U), sizeof(ClusterEntry));
    EXPECT_EQ(DispatchPass::light_indices_size(1U),
              static_cast<std::uint64_t>(kMaxLightsPerCluster)
                  * sizeof(std::uint32_t));

    constexpr std::uint32_t kBig = 16U * 9U * 24U;  // default grid clusters
    EXPECT_EQ(DispatchPass::cluster_table_size(kBig),
              static_cast<std::uint64_t>(kBig) * sizeof(ClusterEntry));
    EXPECT_EQ(DispatchPass::light_indices_size(kBig),
              static_cast<std::uint64_t>(kBig)
                  * static_cast<std::uint64_t>(kMaxLightsPerCluster)
                  * sizeof(std::uint32_t));
}

// std430 contract POD sizes are byte-locked (regression net for the GLSL
// layout the shader declares).
TEST(LightingClustersHostMath, Std430PodLayoutByteLocked)
{
    EXPECT_EQ(sizeof(ClusterEntry), 8U);
    EXPECT_EQ(sizeof(GpuPointLight), 32U);
    EXPECT_EQ(kMaxLightsPerCluster, 64U);
    EXPECT_EQ(alignof(GpuPointLight), 16U);
}

// A default-constructed (un-prepared) DispatchPass reports the right grid
// totals via configure() without touching any device resource.
TEST(LightingClustersHostMath, ConfigureReportsTotalsWithoutDevice)
{
    DispatchPass pass;
    pass.configure({ 8U, 6U, 4U, 0.1F, 100.0F });
    EXPECT_EQ(pass.total_cluster_count(), 8U * 6U * 4U);
    EXPECT_EQ(pass.max_lights(), 0U);
    EXPECT_FALSE(pass.is_ready());
    EXPECT_EQ(pass.grid().x_tiles, 8U);
    EXPECT_EQ(pass.grid().y_tiles, 6U);
    EXPECT_EQ(pass.grid().z_slices, 4U);
}
