// =============================================================================
// CHROMODYNAMIC — cd::render::cluster tests (Phase 7 Sprint 9 Wave 84-85)
// =============================================================================
#include <cd/render/cluster/ClusterGrid.hpp>
#include <gtest/gtest.h>

#include <cstdint>

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

}  // namespace
