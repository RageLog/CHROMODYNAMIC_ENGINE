#include <cd/virtual_geometry/VirtualGeometry.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
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

// ===========================================================================
// ADD-ONLY depth pass — pins ACTUAL behaviour of the EXISTING projected-error
// / LOD-frontier / pick_clusters host helpers (the CPU reference the GLSL
// kernel mirrors). No production code changed. These are the bbox-diagonal /
// screen-space-error contract; QEM mesh simplification stays SEALED future
// work.
// ===========================================================================

// ---------------------------------------------------------------------------
// projected_error_pixels — radius-linearity, distance-falloff, clamps.
// ---------------------------------------------------------------------------

TEST(VirtualGeometry, ProjectedErrorLinearInRadius)
{
    // angular = radius / dist → doubling radius doubles projected pixels.
    const float e1 = projected_error_pixels({ 0, 0, 10 }, 1.0F,
                                             { 0, 0, 0 }, 0.7F, 1080);
    const float e2 = projected_error_pixels({ 0, 0, 10 }, 2.0F,
                                             { 0, 0, 0 }, 0.7F, 1080);
    EXPECT_NEAR(e2, 2.0F * e1, 1e-3F);
}

TEST(VirtualGeometry, ProjectedErrorZeroRadiusIsZeroPixels)
{
    EXPECT_FLOAT_EQ(projected_error_pixels({ 0, 0, 10 }, 0.0F,
                                           { 0, 0, 0 }, 0.7F, 1080),
                    0.0F);
}

TEST(VirtualGeometry, ProjectedErrorScalesWithViewportHeight)
{
    // fov_px = vp_h / (2 tan(half_fov)) → projected error is linear in vp_h.
    const float low  = projected_error_pixels({ 0, 0, 10 }, 1.0F,
                                              { 0, 0, 0 }, 0.7F, 540);
    const float high = projected_error_pixels({ 0, 0, 10 }, 1.0F,
                                              { 0, 0, 0 }, 0.7F, 1080);
    EXPECT_NEAR(high, 2.0F * low, 1e-3F);
}

TEST(VirtualGeometry, ProjectedErrorDistanceClampAvoidsBlowupAtCamera)
{
    // dist is clamped to >= 1e-3, so a cluster sitting on the eye does not
    // produce an infinite projected error — pin a large-but-finite value.
    const float on_eye = projected_error_pixels({ 0, 0, 0 }, 1.0F,
                                                 { 0, 0, 0 }, 0.7F, 1080);
    EXPECT_TRUE(std::isfinite(on_eye));
    EXPECT_GT(on_eye, 0.0F);
}

// ---------------------------------------------------------------------------
// is_lod_frontier — dual-bound predicate boundary + negative cases.
// ---------------------------------------------------------------------------

TEST(VirtualGeometry, LodFrontierFalseWhenSelfErrorExceedsThreshold)
{
    // self_px above threshold → too coarse here, caller draws a finer node.
    ClusterNode n {};
    n.bounds_sphere = { 0, 0, 5, 1.0F };
    n.self_error   = 100.0F;
    n.parent_error = 1000.0F;
    EXPECT_FALSE(is_lod_frontier(n, /*threshold_px=*/1.0F,
                                 { 0, 0, 0 }, 0.7F, 1080));
}

TEST(VirtualGeometry, LodFrontierRootWithInfiniteParentAlwaysPassesUpperBound)
{
    // Root: parent_error huge → parent_px > threshold always → the only gate
    // is the self-bound. Tiny self_error → frontier true.
    ClusterNode root {};
    root.bounds_sphere = { 0, 0, 30, 1.0F };
    root.self_error   = 0.0001F;
    root.parent_error = 1e30F;
    EXPECT_TRUE(is_lod_frontier(root, /*threshold_px=*/2.0F,
                                { 0, 0, 0 }, 0.7F, 1080));
}

// ---------------------------------------------------------------------------
// pick_clusters — selects exactly the frontier indices, edge sets.
// ---------------------------------------------------------------------------

TEST(VirtualGeometry, PickClustersEmptyDagReturnsEmpty)
{
    const std::vector<ClusterNode> dag;
    const auto picked = pick_clusters(dag, 4.0F, { 0, 0, 0 }, 0.7F, 1080);
    EXPECT_TRUE(picked.empty());
}

TEST(VirtualGeometry, PickClustersReturnsIndicesInAscendingOrder)
{
    // Three frontier-eligible nodes at increasing depth; pick must preserve
    // the input index order (the loop walks i ascending).
    std::vector<ClusterNode> dag(3);
    for (std::uint32_t i = 0; i < 3U; ++i)
    {
        dag[i].bounds_sphere = { 0, 0, 10.0F + static_cast<float>(i) * 5.0F, 1 };
        dag[i].self_error    = 0.001F;     // self always fits
        dag[i].parent_error  = 100.0F;     // parent never fits → all frontier
    }
    const auto picked = pick_clusters(dag, 1.0F, { 0, 0, 0 }, 0.7F, 1080);
    ASSERT_EQ(picked.size(), 3U);
    EXPECT_EQ(picked[0], 0U);
    EXPECT_EQ(picked[1], 1U);
    EXPECT_EQ(picked[2], 2U);
}

TEST(VirtualGeometry, PickClustersDropsAllWhenEveryParentIsAcceptable)
{
    // Every node's parent error is tiny → parent_px <= threshold everywhere →
    // no node is the frontier (the coarsest is always preferable).
    std::vector<ClusterNode> dag(4);
    for (auto& n : dag)
    {
        n.bounds_sphere = { 0, 0, 200, 1 };
        n.self_error   = 0.0001F;
        n.parent_error = 0.0002F;
    }
    const auto picked = pick_clusters(dag, 5.0F, { 0, 0, 0 }, 0.7F, 1080);
    EXPECT_TRUE(picked.empty());
}

TEST(VirtualGeometry, PickClustersMatchesPerNodeIsLodFrontier)
{
    // pick_clusters must agree node-for-node with is_lod_frontier (they share
    // the same predicate) — a cross-check that locks the two helpers together.
    std::vector<ClusterNode> dag(5);
    dag[0].bounds_sphere = { 0, 0, 10, 1 };  dag[0].self_error = 0.05F; dag[0].parent_error = 5.0F;
    dag[1].bounds_sphere = { 0, 0, 100, 1 }; dag[1].self_error = 0.005F; dag[1].parent_error = 0.01F;
    dag[2].bounds_sphere = { 0, 0, 20, 1 };  dag[2].self_error = 0.02F; dag[2].parent_error = 9.0F;
    dag[3].bounds_sphere = { 0, 0, 5, 1 };   dag[3].self_error = 50.0F; dag[3].parent_error = 99.0F;
    dag[4].bounds_sphere = { 0, 0, 40, 1 };  dag[4].self_error = 0.01F; dag[4].parent_error = 6.0F;

    constexpr float kThresh = 4.0F;
    const auto picked = pick_clusters(dag, kThresh, { 0, 0, 0 }, 0.7F, 1080);

    std::vector<std::uint32_t> expected;
    for (std::uint32_t i = 0; i < dag.size(); ++i)
        if (is_lod_frontier(dag[i], kThresh, { 0, 0, 0 }, 0.7F, 1080))
            expected.push_back(i);

    EXPECT_EQ(picked, expected);
}

}  // namespace
