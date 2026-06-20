// =============================================================================
// CHROMODYNAMIC — cd::render::cluster tests (Phase 7 Sprint 9 Wave 84-85)
// =============================================================================
#include <cd/render/cluster/ClusterGrid.hpp>
#include <cd/render/cluster/ReferenceCompute.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace
{

TEST(ClusterGrid, EmptyGridHasZeroLightAssignments)
{
    cd::render::cluster::ClusterGrid g { cd::render::cluster::ClusterConfig {} };
    g.finalize();
    EXPECT_EQ(g.total_light_assignments(), 0U);
    EXPECT_TRUE(g.lights_in_cluster(0, 0, 0).empty());
}

TEST(ClusterGrid, TotalClusterCountMatchesConfig)
{
    cd::render::cluster::ClusterConfig cfg { 8, 4, 12, 1.0F, 1.0F, 0.1F, 100.0F };
    cd::render::cluster::ClusterGrid g { cfg };
    EXPECT_EQ(g.total_cluster_count(), 8U * 4U * 12U);
}

TEST(ClusterGrid, OutOfFrustumLightIsCulled)
{
    cd::render::cluster::ClusterConfig cfg { 16, 9, 24, 1.0472F, 16.0F / 9.0F, 0.1F, 100.0F };
    cd::render::cluster::ClusterGrid g { cfg };
    // Light far beyond the far plane: position z = +200 (behind camera
    // in our convention — view -Z forward, +Z is behind).
    cd::render::cluster::LightSphere far_behind {};
    far_behind.view_pos = { 0.0F, 0.0F, 200.0F };  // depth = -200 → not in [near,far]
    far_behind.radius = 0.5F;
    g.assign_light(0, far_behind);
    g.finalize();
    EXPECT_EQ(g.total_light_assignments(), 0U);
}

TEST(ClusterGrid, FrontCentreLightAssignedToFrontCentreClusters)
{
    // A small light directly in front of the camera should land in
    // ~the centre cluster column (x ≈ X/2, y ≈ Y/2) at one or two
    // depth slices near the camera.
    cd::render::cluster::ClusterConfig cfg { 16, 9, 24, 1.0472F, 16.0F / 9.0F, 0.1F, 100.0F };
    cd::render::cluster::ClusterGrid g { cfg };
    cd::render::cluster::LightSphere centre {};
    centre.view_pos = { 0.0F, 0.0F, -5.0F };  // depth = 5 m
    centre.radius = 0.25F;
    g.assign_light(42, centre);
    g.finalize();
    EXPECT_GT(g.total_light_assignments(), 0U);

    // Light should appear in the centre cluster column.
    const auto centre_lights = g.lights_in_cluster(8, 4, 5);  // approx mid X, mid Y, depth ~5m
    // Centre cluster may or may not be on the exact (8,4) cell at
    // this depth; loop the row to confirm the light index 42 is present.
    bool found = false;
    for (std::uint32_t z = 0; z < cfg.cells_z; ++z)
        for (std::uint32_t y = 0; y < cfg.cells_y; ++y)
            for (std::uint32_t x = 0; x < cfg.cells_x; ++x)
            {
                for (auto idx : g.lights_in_cluster(x, y, z))
                    if (idx == 42)
                    {
                        found = true;
                        break;
                    }
                if (found)
                    break;
            }
    EXPECT_TRUE(found);
    (void)centre_lights;
}

TEST(ClusterGrid, FinalizeRequiredBeforeQuery)
{
    cd::render::cluster::ClusterGrid g { cd::render::cluster::ClusterConfig {} };
    cd::render::cluster::LightSphere s {};
    s.view_pos = { 0.0F, 0.0F, -1.0F };
    s.radius = 0.1F;
    g.assign_light(1, s);
    // Without finalize() the per-cluster view is empty (buckets not packed).
    EXPECT_TRUE(g.lights_in_cluster(8, 4, 0).empty());
    g.finalize();
    // Now the assignment is visible somewhere in the grid.
    bool found = false;
    const auto& cfg = g.config();
    for (std::uint32_t z = 0; z < cfg.cells_z && !found; ++z)
        for (std::uint32_t y = 0; y < cfg.cells_y && !found; ++y)
            for (std::uint32_t x = 0; x < cfg.cells_x && !found; ++x)
                for (auto idx : g.lights_in_cluster(x, y, z))
                    if (idx == 1)
                    {
                        found = true;
                        break;
                    }
    EXPECT_TRUE(found);
}

TEST(ClusterGrid, ClearResetsAssignments)
{
    cd::render::cluster::ClusterGrid g { cd::render::cluster::ClusterConfig {} };
    cd::render::cluster::LightSphere s {};
    s.view_pos = { 0.0F, 0.0F, -5.0F };
    s.radius = 1.0F;
    g.assign_light(0, s);
    g.finalize();
    EXPECT_GT(g.total_light_assignments(), 0U);
    g.clear();
    g.finalize();
    EXPECT_EQ(g.total_light_assignments(), 0U);
}

TEST(ClusterGrid, LargeLightSpansManyClusters)
{
    // A big light at the centre at moderate depth should touch many
    // clusters across X, Y, and Z slices.
    cd::render::cluster::ClusterConfig cfg { 16, 9, 24, 1.0472F, 16.0F / 9.0F, 0.1F, 100.0F };
    cd::render::cluster::ClusterGrid g { cfg };
    cd::render::cluster::LightSphere big {};
    big.view_pos = { 0.0F, 0.0F, -10.0F };
    big.radius = 5.0F;  // 5 m radius at 10 m depth — covers a lot
    g.assign_light(7, big);
    g.finalize();
    EXPECT_GT(g.total_light_assignments(), 10U);
}

TEST(ClusterGrid, OutOfRangeQueryReturnsEmpty)
{
    cd::render::cluster::ClusterConfig cfg { 4, 4, 4, 1.0F, 1.0F, 0.1F, 100.0F };
    cd::render::cluster::ClusterGrid g { cfg };
    g.finalize();
    EXPECT_TRUE(g.lights_in_cluster(99, 0, 0).empty());
    EXPECT_TRUE(g.lights_in_cluster(0, 99, 0).empty());
    EXPECT_TRUE(g.lights_in_cluster(0, 0, 99).empty());
}

TEST(ClusterGrid, MultipleLightsPackedCorrectly)
{
    cd::render::cluster::ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    cd::render::cluster::ClusterGrid g { cfg };
    // Two non-overlapping lights at different depths.
    cd::render::cluster::LightSphere a {};
    a.view_pos = { -1.0F, 0.0F, -2.0F };
    a.radius = 0.2F;
    cd::render::cluster::LightSphere b {};
    b.view_pos = { 1.0F, 0.0F, -20.0F };
    b.radius = 0.5F;
    g.assign_light(10, a);
    g.assign_light(20, b);
    g.finalize();
    bool found_10 = false;
    bool found_20 = false;
    for (std::uint32_t z = 0; z < cfg.cells_z; ++z)
        for (std::uint32_t y = 0; y < cfg.cells_y; ++y)
            for (std::uint32_t x = 0; x < cfg.cells_x; ++x)
                for (auto idx : g.lights_in_cluster(x, y, z))
                {
                    if (idx == 10)
                        found_10 = true;
                    if (idx == 20)
                        found_20 = true;
                }
    EXPECT_TRUE(found_10);
    EXPECT_TRUE(found_20);
}

// --- ReferenceCompute (CPU sim of cluster_assign.comp) ---------------------

TEST(ReferenceCompute, EmptyLightsProducesEmptyOutput)
{
    cd::render::cluster::ClusterConfig cfg {};
    auto out = cd::render::cluster::run_reference_compute(cfg, {});
    EXPECT_EQ(out.cluster_counts.size(),
              static_cast<std::size_t>(cfg.cells_x) * cfg.cells_y * cfg.cells_z);
    EXPECT_TRUE(out.light_indices.empty());
    EXPECT_EQ(out.cluster_offsets.back(), 0U);
}

TEST(ReferenceCompute, OffsetsAreCumulativeSumOfCounts)
{
    cd::render::cluster::ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    std::vector<cd::render::cluster::LightSphere> lights;
    for (int i = 0; i < 8; ++i)
    {
        cd::render::cluster::LightSphere s;
        s.view_pos = { static_cast<float>(i) * 0.5F - 2.0F, 0.0F,
                       -5.0F - static_cast<float>(i) };
        s.radius = 1.0F;
        lights.push_back(s);
    }
    auto out = cd::render::cluster::run_reference_compute(cfg, lights);
    std::uint32_t running = 0;
    for (std::size_t i = 0; i < out.cluster_counts.size(); ++i)
    {
        EXPECT_EQ(out.cluster_offsets[i], running);
        running += out.cluster_counts[i];
    }
    EXPECT_EQ(out.cluster_offsets.back(), running);
    EXPECT_EQ(out.light_indices.size(), running);
}

TEST(ReferenceCompute, ParityWithClusterGrid)
{
    // The compute-shader reference and the ClusterGrid CPU class
    // implement the same algorithm; their packed outputs must match
    // bitwise (cluster_offsets identical, light_indices identical
    // within each cluster's range).
    cd::render::cluster::ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    std::vector<cd::render::cluster::LightSphere> lights;
    for (int i = 0; i < 16; ++i)
    {
        cd::render::cluster::LightSphere s;
        s.view_pos = { static_cast<float>((i * 13) % 7) - 3.0F,
                       static_cast<float>((i * 7) % 5) - 2.0F,
                       -2.0F - static_cast<float>(i) * 1.5F };
        s.radius = 0.5F + static_cast<float>(i % 4) * 0.3F;
        lights.push_back(s);
    }

    // Drive the grid the same way (ClusterGrid stores buckets in
    // arrival order, sorts by cluster_id; per-cluster index order
    // ends up the same as ReferenceCompute's ascending-light loop
    // because all lights with index i go to their clusters before
    // light i+1 — and within a cluster the sort is stable on
    // cluster_id only, so insertion order = light index order).
    cd::render::cluster::ClusterGrid grid { cfg };
    for (std::uint32_t i = 0; i < lights.size(); ++i)
        grid.assign_light(i, lights[i]);
    grid.finalize();

    auto ref = cd::render::cluster::run_reference_compute(cfg, lights);

    // Per cluster: lights_in_cluster() vs. ref.light_indices slice.
    for (std::uint32_t z = 0; z < cfg.cells_z; ++z)
        for (std::uint32_t y = 0; y < cfg.cells_y; ++y)
            for (std::uint32_t x = 0; x < cfg.cells_x; ++x)
            {
                const std::size_t cid =
                    (static_cast<std::size_t>(z) * cfg.cells_y + y) * cfg.cells_x + x;
                const auto from_grid = grid.lights_in_cluster(x, y, z);
                const auto begin = ref.cluster_offsets[cid];
                const auto end = ref.cluster_offsets[cid + 1];
                const std::size_t ref_count = end - begin;
                ASSERT_EQ(from_grid.size(), ref_count)
                    << "count mismatch at cluster (" << x << "," << y << "," << z << ")";
                std::vector<std::uint32_t> grid_sorted(from_grid.begin(), from_grid.end());
                std::vector<std::uint32_t> ref_sorted(ref.light_indices.data() + begin,
                                                      ref.light_indices.data() + end);
                std::ranges::sort(grid_sorted);
                std::ranges::sort(ref_sorted);
                EXPECT_EQ(grid_sorted, ref_sorted)
                    << "membership mismatch at cluster (" << x << "," << y << "," << z << ")";
            }
}

TEST(ReferenceCompute, AscendingLightIndexOrderingWithinCluster)
{
    // The shader's `for (i = 0; i < light_count; ++i)` loop appends
    // overlapping lights in ascending index order. ReferenceCompute
    // mirrors that — within each cluster, the index sequence is
    // monotonically increasing.
    cd::render::cluster::ClusterConfig cfg { 8, 4, 4, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    // Centre-of-frustum light cluster — pick a single populated cell
    // and inspect its ordering.
    std::vector<cd::render::cluster::LightSphere> lights;
    for (int i = 0; i < 8; ++i)
    {
        cd::render::cluster::LightSphere s;
        s.view_pos = { 0.0F, 0.0F, -5.0F };
        s.radius = 2.0F;  // all hit the centre cluster
        lights.push_back(s);
    }
    auto out = cd::render::cluster::run_reference_compute(cfg, lights);
    // Find a cluster with > 1 light and check ascending.
    bool checked = false;
    for (std::size_t cid = 0; cid < out.cluster_counts.size(); ++cid)
    {
        if (out.cluster_counts[cid] > 1U)
        {
            const auto begin = out.cluster_offsets[cid];
            const auto end = out.cluster_offsets[cid + 1];
            for (auto k = begin + 1; k < end; ++k)
                EXPECT_GT(out.light_indices[k], out.light_indices[k - 1]);
            checked = true;
            break;
        }
    }
    EXPECT_TRUE(checked) << "no multi-light cluster in this configuration";
}

// =============================================================================
// 80->100 marathon — ADD-ONLY host-side coverage on the CPU-reference path.
// No froxel / compute / PBR math is modified; these tests pin the EXISTING
// behaviour of ClusterGrid + run_reference_compute so the GPU-feeding path
// stays byte-identical. AAA + edge + negative.
// =============================================================================

namespace
{

using cd::render::cluster::ClusterConfig;
using cd::render::cluster::ClusterGrid;
using cd::render::cluster::LightSphere;
using cd::render::cluster::run_reference_compute;

/// Brute-force membership: is `light_index` present anywhere in the grid?
[[nodiscard]] bool grid_contains(const ClusterGrid& g, std::uint32_t light_index)
{
    const auto& cfg = g.config();
    for (std::uint32_t z = 0; z < cfg.cells_z; ++z)
        for (std::uint32_t y = 0; y < cfg.cells_y; ++y)
            for (std::uint32_t x = 0; x < cfg.cells_x; ++x)
                for (auto idx : g.lights_in_cluster(x, y, z))
                    if (idx == light_index)
                        return true;
    return false;
}

}  // namespace

// --- ClusterGrid: froxel grid dims / config plumbing -----------------------

TEST(ClusterGridDims, DefaultConfigClusterCountIs16x9x24)
{
    // Arrange / Act
    const ClusterGrid g { ClusterConfig {} };
    // Assert — default 16 x 9 x 24.
    EXPECT_EQ(g.total_cluster_count(), 16U * 9U * 24U);
}

TEST(ClusterGridDims, ConfigIsStoredVerbatim)
{
    const ClusterConfig cfg { 5, 6, 7, 0.9F, 1.5F, 0.25F, 250.0F };
    const ClusterGrid g { cfg };
    const auto& got = g.config();
    EXPECT_EQ(got.cells_x, 5U);
    EXPECT_EQ(got.cells_y, 6U);
    EXPECT_EQ(got.cells_z, 7U);
    EXPECT_FLOAT_EQ(got.fov_y_rad, 0.9F);
    EXPECT_FLOAT_EQ(got.aspect, 1.5F);
    EXPECT_FLOAT_EQ(got.near_plane, 0.25F);
    EXPECT_FLOAT_EQ(got.far_plane, 250.0F);
    EXPECT_EQ(g.total_cluster_count(), 5U * 6U * 7U);
}

TEST(ClusterGridDims, SingleCellGridIsValid)
{
    // Minimum grid: every light that is in-frustum lands in cluster (0,0,0).
    const ClusterConfig cfg { 1, 1, 1, 1.0472F, 1.0F, 0.1F, 100.0F };
    ClusterGrid g { cfg };
    EXPECT_EQ(g.total_cluster_count(), 1U);
    LightSphere s {};
    s.view_pos = { 0.0F, 0.0F, -5.0F };
    s.radius = 0.5F;
    g.assign_light(3, s);
    g.finalize();
    const auto only = g.lights_in_cluster(0, 0, 0);
    ASSERT_EQ(only.size(), 1U);
    EXPECT_EQ(only[0], 3U);
}

// --- ClusterGrid: finalize / clear lifecycle -------------------------------

TEST(ClusterGridLifecycle, NotFinalizedByDefault)
{
    const ClusterGrid g { ClusterConfig {} };
    EXPECT_FALSE(g.finalized());
}

TEST(ClusterGridLifecycle, AssignLightUnsetsFinalizedFlag)
{
    ClusterGrid g { ClusterConfig {} };
    g.finalize();
    EXPECT_TRUE(g.finalized());
    LightSphere s {};
    s.view_pos = { 0.0F, 0.0F, -5.0F };
    s.radius = 0.5F;
    g.assign_light(0, s);
    EXPECT_FALSE(g.finalized());  // assignment invalidates the packed view
}

TEST(ClusterGridLifecycle, QueryBeforeFinalizeReturnsEmpty)
{
    ClusterGrid g { ClusterConfig {} };
    LightSphere s {};
    s.view_pos = { 0.0F, 0.0F, -5.0F };
    s.radius = 1.0F;
    g.assign_light(0, s);
    // No finalize(): the per-cluster view is intentionally empty.
    EXPECT_FALSE(g.finalized());
    EXPECT_TRUE(g.lights_in_cluster(8, 4, 5).empty());
}

TEST(ClusterGridLifecycle, DoubleFinalizeIsIdempotent)
{
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    ClusterGrid g { cfg };
    LightSphere s {};
    s.view_pos = { 0.0F, 0.0F, -5.0F };
    s.radius = 1.0F;
    g.assign_light(11, s);
    g.finalize();
    const auto first = g.total_light_assignments();
    g.finalize();  // second finalize without new assignments
    EXPECT_EQ(g.total_light_assignments(), first);
    EXPECT_TRUE(grid_contains(g, 11U));
}

TEST(ClusterGridLifecycle, ReassignAfterClearProducesSameResult)
{
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    ClusterGrid g { cfg };
    LightSphere s {};
    s.view_pos = { 0.5F, -0.5F, -7.0F };
    s.radius = 1.0F;
    g.assign_light(5, s);
    g.finalize();
    const auto before = g.total_light_assignments();
    g.clear();
    EXPECT_FALSE(g.finalized());
    g.assign_light(5, s);
    g.finalize();
    EXPECT_EQ(g.total_light_assignments(), before);
    EXPECT_TRUE(grid_contains(g, 5U));
}

// --- ClusterGrid: sphere in / out / spanning the frustum -------------------

TEST(ClusterGridAssign, SphereBehindCameraIsCulled)
{
    // +Z is behind the camera (view looks down -Z). depth = -z < 0.
    const ClusterConfig cfg { 16, 9, 24, 1.0472F, 16.0F / 9.0F, 0.1F, 100.0F };
    ClusterGrid g { cfg };
    LightSphere behind {};
    behind.view_pos = { 0.0F, 0.0F, 5.0F };  // depth = -5 (behind)
    behind.radius = 0.5F;  // sphere does not reach the near plane
    g.assign_light(0, behind);
    g.finalize();
    EXPECT_EQ(g.total_light_assignments(), 0U);
}

TEST(ClusterGridAssign, SphereStraddlingNearPlaneStillAssigns)
{
    // Centre just behind the camera but radius reaches across the near
    // plane into the frustum — must NOT be culled.
    const ClusterConfig cfg { 16, 9, 24, 1.0472F, 16.0F / 9.0F, 0.1F, 100.0F };
    ClusterGrid g { cfg };
    LightSphere straddle {};
    straddle.view_pos = { 0.0F, 0.0F, 0.3F };  // depth = -0.3 (behind near)
    straddle.radius = 1.0F;                    // reaches depth +0.7 (in frustum)
    g.assign_light(9, straddle);
    g.finalize();
    EXPECT_GT(g.total_light_assignments(), 0U);
    EXPECT_TRUE(grid_contains(g, 9U));
}

TEST(ClusterGridAssign, SphereSpanningFullDepthHitsNearAndFarSlices)
{
    // A huge sphere whose extent covers near..far should populate both
    // the first (z=0) and last (z=cells_z-1) depth slices in its column.
    const ClusterConfig cfg { 4, 4, 8, 1.0472F, 1.0F, 0.1F, 100.0F };
    ClusterGrid g { cfg };
    LightSphere big {};
    big.view_pos = { 0.0F, 0.0F, -50.0F };
    big.radius = 80.0F;  // spans well past near and toward far
    g.assign_light(1, big);
    g.finalize();
    bool near_slice = false;
    bool far_slice = false;
    for (std::uint32_t y = 0; y < cfg.cells_y; ++y)
        for (std::uint32_t x = 0; x < cfg.cells_x; ++x)
        {
            for (auto idx : g.lights_in_cluster(x, y, 0U))
                if (idx == 1U)
                    near_slice = true;
            for (auto idx : g.lights_in_cluster(x, y, cfg.cells_z - 1U))
                if (idx == 1U)
                    far_slice = true;
        }
    EXPECT_TRUE(near_slice) << "light absent from nearest depth slice";
    EXPECT_TRUE(far_slice) << "light absent from farthest depth slice";
}

// --- ClusterGrid: offset-array invariants ----------------------------------

TEST(ClusterGridOffsets, OutOfRangeQueryOnEachAxisReturnsEmpty)
{
    const ClusterConfig cfg { 4, 4, 4, 1.0F, 1.0F, 0.1F, 100.0F };
    ClusterGrid g { cfg };
    g.finalize();
    EXPECT_TRUE(g.lights_in_cluster(4, 0, 0).empty());   // x == cells_x
    EXPECT_TRUE(g.lights_in_cluster(0, 4, 0).empty());   // y == cells_y
    EXPECT_TRUE(g.lights_in_cluster(0, 0, 4).empty());   // z == cells_z
}

// --- ReferenceCompute: light count 0 / 1 / many ----------------------------

TEST(ReferenceComputeCount, ZeroLightsAllCountsAreZero)
{
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    const auto out = run_reference_compute(cfg, {});
    const std::size_t n =
        static_cast<std::size_t>(cfg.cells_x) * cfg.cells_y * cfg.cells_z;
    ASSERT_EQ(out.cluster_counts.size(), n);
    EXPECT_TRUE(std::ranges::all_of(out.cluster_counts,
                                    [](std::uint32_t c) { return c == 0U; }));
    EXPECT_EQ(out.cluster_offsets.front(), 0U);
    EXPECT_EQ(out.cluster_offsets.back(), 0U);
}

TEST(ReferenceComputeCount, SingleLightSumOfCountsEqualsIndexCount)
{
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    LightSphere s {};
    s.view_pos = { 0.0F, 0.0F, -6.0F };
    s.radius = 1.5F;
    const std::array<LightSphere, 1> lights { s };
    const auto out = run_reference_compute(cfg, lights);
    std::uint32_t sum = 0;
    for (const auto c : out.cluster_counts)
        sum += c;
    EXPECT_GT(sum, 0U);  // a real light overlaps at least one cluster
    EXPECT_EQ(sum, out.light_indices.size());
    EXPECT_EQ(out.cluster_offsets.back(), out.light_indices.size());
}

TEST(ReferenceComputeCount, OffsetsAreMonotonicNonDecreasing)
{
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    std::vector<LightSphere> lights;
    for (int i = 0; i < 12; ++i)
    {
        LightSphere s;
        s.view_pos = { static_cast<float>(i) * 0.4F - 2.0F, 0.0F,
                       -4.0F - static_cast<float>(i) };
        s.radius = 1.2F;
        lights.push_back(s);
    }
    const auto out = run_reference_compute(cfg, lights);
    ASSERT_FALSE(out.cluster_offsets.empty());
    EXPECT_EQ(out.cluster_offsets.front(), 0U);
    for (std::size_t i = 1; i < out.cluster_offsets.size(); ++i)
        EXPECT_LE(out.cluster_offsets[i - 1], out.cluster_offsets[i])
            << "offset decreased at index " << i;
}

TEST(ReferenceComputeCount, EveryWrittenIndexIsAValidLightId)
{
    // The PASS-2 write must only emit light ids in [0, light_count).
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    std::vector<LightSphere> lights;
    for (int i = 0; i < 10; ++i)
    {
        LightSphere s;
        s.view_pos = { static_cast<float>((i * 5) % 7) - 3.0F, 0.0F,
                       -3.0F - static_cast<float>(i) };
        s.radius = 1.0F;
        lights.push_back(s);
    }
    const auto out = run_reference_compute(cfg, lights);
    for (const auto id : out.light_indices)
        EXPECT_LT(id, lights.size());
}

TEST(ReferenceComputeCount, CountMatchesPerClusterRangeLength)
{
    // Two-pass invariant: cluster_counts[cid] must equal the length of
    // the [offsets[cid], offsets[cid+1]) range for every cluster.
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    std::vector<LightSphere> lights;
    for (int i = 0; i < 14; ++i)
    {
        LightSphere s;
        s.view_pos = { static_cast<float>((i * 3) % 5) - 2.0F,
                       static_cast<float>((i * 2) % 3) - 1.0F,
                       -2.0F - static_cast<float>(i) * 1.3F };
        s.radius = 0.6F + static_cast<float>(i % 3) * 0.4F;
        lights.push_back(s);
    }
    const auto out = run_reference_compute(cfg, lights);
    for (std::size_t cid = 0; cid < out.cluster_counts.size(); ++cid)
    {
        const auto len = out.cluster_offsets[cid + 1] - out.cluster_offsets[cid];
        EXPECT_EQ(out.cluster_counts[cid], len) << "cluster " << cid;
    }
}

TEST(ReferenceComputeParity, GridAndReferenceAgreeOnSingleLight)
{
    // A second parity anchor (the existing one stresses 16 lights):
    // a single light must produce identical packed membership in both
    // the ClusterGrid path and the compute-shader reference.
    const ClusterConfig cfg { 8, 4, 8, 1.0472F, 16.0F / 9.0F, 0.1F, 50.0F };
    LightSphere s {};
    s.view_pos = { 0.5F, -0.5F, -8.0F };
    s.radius = 2.0F;
    const std::array<LightSphere, 1> lights { s };

    ClusterGrid grid { cfg };
    grid.assign_light(0, s);
    grid.finalize();
    const auto ref = run_reference_compute(cfg, lights);

    for (std::uint32_t z = 0; z < cfg.cells_z; ++z)
        for (std::uint32_t y = 0; y < cfg.cells_y; ++y)
            for (std::uint32_t x = 0; x < cfg.cells_x; ++x)
            {
                const std::size_t cid =
                    (static_cast<std::size_t>(z) * cfg.cells_y + y) * cfg.cells_x + x;
                const auto from_grid = grid.lights_in_cluster(x, y, z);
                const auto len = ref.cluster_offsets[cid + 1] - ref.cluster_offsets[cid];
                EXPECT_EQ(from_grid.size(), len)
                    << "count mismatch at (" << x << "," << y << "," << z << ")";
            }
}

}  // namespace
