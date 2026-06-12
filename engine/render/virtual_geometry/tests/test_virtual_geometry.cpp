#include <cd/virtual_geometry/VirtualGeometry.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace
{

using cd::virtual_geometry::ClusterNode;
using cd::virtual_geometry::is_lod_frontier;
using cd::virtual_geometry::pick_clusters;
using cd::virtual_geometry::projected_error_pixels;

TEST(VirtualGeometry, ProjectedErrorFarther)
{
    const float near_px = projected_error_pixels({ 0, 0, 5 }, 1.0F,
                                                  { 0, 0, 0 }, 0.7F, 1080);
    const float far_px  = projected_error_pixels({ 0, 0, 50 }, 1.0F,
                                                  { 0, 0, 0 }, 0.7F, 1080);
    EXPECT_GT(near_px, far_px);
}

TEST(VirtualGeometry, LodFrontierTrueWhenSelfPassesParentFails)
{
    ClusterNode n {};
    n.bounds_sphere = { 0, 0, 10, 1.0F };
    n.self_error   = 0.1F;
    n.parent_error = 1.0F;
    EXPECT_TRUE(is_lod_frontier(n, /*threshold_px=*/8.0F,
                                { 0, 0, 0 }, 0.7F, 1080));
}

TEST(VirtualGeometry, LodFrontierFalseWhenBothBelowThreshold)
{
    ClusterNode n {};
    n.bounds_sphere = { 0, 0, 100, 1.0F };
    n.self_error   = 0.01F;
    n.parent_error = 0.02F;  // both tiny -> parent should still be acceptable
    EXPECT_FALSE(is_lod_frontier(n, /*threshold_px=*/5.0F,
                                 { 0, 0, 0 }, 0.7F, 1080));
}

TEST(VirtualGeometry, PickClustersReturnsFrontier)
{
    std::vector<ClusterNode> dag(2);
    dag[0].bounds_sphere = { 0, 0, 10, 1 };
    dag[0].self_error    = 0.05F;
    dag[0].parent_error  = 5.0F;
    dag[1].bounds_sphere = { 0, 0, 100, 1 };
    dag[1].self_error    = 0.005F;
    dag[1].parent_error  = 0.01F;
    const auto picked = pick_clusters(dag, 4.0F, { 0, 0, 0 }, 0.7F, 1080);
    EXPECT_EQ(picked.size(), 1U);
    EXPECT_EQ(picked[0], 0U);
}

TEST(VirtualGeometry, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::virtual_geometry::kLodPickCS.empty());
}

}  // namespace
