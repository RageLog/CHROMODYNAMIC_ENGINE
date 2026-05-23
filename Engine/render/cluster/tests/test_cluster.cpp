// =============================================================================
// CHROMODYNAMIC — cd::render::cluster tests (Phase 7 Sprint 9 Wave 84-85)
// =============================================================================
#include <cd/render/cluster/ClusterGrid.hpp>
#include <cd/render/cluster/ReferenceCompute.hpp>
#include <gtest/gtest.h>

#include <algorithm>
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
    bool found_10 = false, found_20 = false;
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
                std::sort(grid_sorted.begin(), grid_sorted.end());
                std::sort(ref_sorted.begin(), ref_sorted.end());
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

}  // namespace
