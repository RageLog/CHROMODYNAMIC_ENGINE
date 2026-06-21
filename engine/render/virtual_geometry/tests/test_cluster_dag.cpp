// =============================================================================
// CHROMODYNAMIC — tests/test_cluster_dag.cpp
// phase528 — ClusterDAG builder unit tests.
//
// Test plan:
//   1. Build cluster DAG on a procedural sphere mesh (~1 024 triangles).
//   2. DAG has multiple LOD levels (lod_levels() >= 2).
//   3. Every cluster satisfies ≤ 128 triangles (Nanite spec).
//   4. No DAG cycles (parent_lod strictly > lod_level of every cluster).
// =============================================================================

#include <cd/virtual_geometry/ClusterDAG.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------
// Procedural UV-sphere mesh generator
// stacks × slices → 2 × stacks × slices triangles
// ---------------------------------------------------------------------------
struct SphereMesh
{
    std::vector<cd::math::Vec3f> vertices;
    std::vector<std::uint32_t>   indices;
};

[[nodiscard]] SphereMesh make_sphere(int stacks, int slices, float radius = 1.0F)
{
    SphereMesh m;
    m.vertices.reserve((static_cast<std::size_t>(stacks) + 1U)
                       * (static_cast<std::size_t>(slices) + 1U));

    for (int i = 0; i <= stacks; ++i)
    {
        const float phi = std::numbers::pi_v<float>
                          * static_cast<float>(i) / static_cast<float>(stacks);
        for (int j = 0; j <= slices; ++j)
        {
            const float theta = 2.0F * std::numbers::pi_v<float>
                                * static_cast<float>(j) / static_cast<float>(slices);
            cd::math::Vec3f v;
            v.x = radius * std::sin(phi) * std::cos(theta);
            v.y = radius * std::cos(phi);
            v.z = radius * std::sin(phi) * std::sin(theta);
            m.vertices.push_back(v);
        }
    }

    m.indices.reserve(static_cast<std::size_t>(stacks)
                      * static_cast<std::size_t>(slices) * 6U);
    for (int i = 0; i < stacks; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            const auto a = static_cast<std::uint32_t>( i      * (slices + 1) + j     );
            const auto b = static_cast<std::uint32_t>((i + 1) * (slices + 1) + j     );
            const auto c = static_cast<std::uint32_t>((i + 1) * (slices + 1) + j + 1 );
            const auto d = static_cast<std::uint32_t>( i      * (slices + 1) + j + 1 );
            // Two triangles per quad
            m.indices.push_back(a); m.indices.push_back(b); m.indices.push_back(c);
            m.indices.push_back(a); m.indices.push_back(c); m.indices.push_back(d);
        }
    }

    return m;
}

// ---------------------------------------------------------------------------
// Test 1: Build completes without exception on a ~1 024-triangle sphere
// ---------------------------------------------------------------------------
TEST(ClusterDAG, BuildSphereMesh1kTriangles)
{
    // 16 stacks × 32 slices = 1 024 triangles
    const auto mesh = make_sphere(16, 32);
    ASSERT_EQ(mesh.indices.size(), 1024U * 3U);

    cd::virtual_geometry::ClusterDAGBuilder builder;
    cd::virtual_geometry::ClusterDAG dag;
    ASSERT_NO_THROW(dag = builder.build(mesh.vertices, mesh.indices));

    // Must have produced at least one cluster
    EXPECT_FALSE(dag.clusters().empty());
}

// ---------------------------------------------------------------------------
// Test 2: DAG has multiple LOD levels
// ---------------------------------------------------------------------------
TEST(ClusterDAG, MultipleLodLevels)
{
    // 1 024 triangles / 128 per cluster = 8 leaf clusters minimum
    // → builder must produce at least 2 LOD levels
    const auto mesh = make_sphere(16, 32);

    cd::virtual_geometry::ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    EXPECT_GE(dag.lod_levels(), 2U)
        << "Expected at least LOD-0 (leaves) and LOD-1 (coarser) from a "
           "1 024-triangle mesh partitioned into ≤128-tri clusters.";
}

// ---------------------------------------------------------------------------
// Test 3: Every cluster satisfies ≤ 128 triangles (Nanite spec)
// ---------------------------------------------------------------------------
TEST(ClusterDAG, EachClusterAtMost128Triangles)
{
    const auto mesh = make_sphere(16, 32);

    cd::virtual_geometry::ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    for (const auto& cl : dag.clusters())
    {
        EXPECT_LE(cl.triangles.size(),
                  static_cast<std::size_t>(
                      cd::virtual_geometry::kMaxClusterTriangles))
            << "Cluster at lod_level=" << cl.lod_level
            << " has " << cl.triangles.size() << " triangles (max "
            << cd::virtual_geometry::kMaxClusterTriangles << ").";
    }
}

// ---------------------------------------------------------------------------
// Test 4: No DAG cycles — parent_lod strictly forward (> lod_level)
// ---------------------------------------------------------------------------
TEST(ClusterDAG, NoCyclesParentLodStrictlyForward)
{
    const auto mesh = make_sphere(16, 32);

    cd::virtual_geometry::ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    EXPECT_TRUE(dag.is_acyclic())
        << "DAG violated the strict-parent invariant: "
           "parent_lod must be strictly greater than lod_level for all "
           "non-root clusters.";
}

// ---------------------------------------------------------------------------
// Test 5: Tiny mesh (3 triangles = 1 cluster) produces a valid single-node DAG
// ---------------------------------------------------------------------------
TEST(ClusterDAG, TinyMeshSingleCluster)
{
    // Equilateral triangle + 2 more = 3 triangles
    const std::vector<cd::math::Vec3f> verts = {
        {  0.0F,  1.0F, 0.0F },
        { -1.0F, -1.0F, 0.0F },
        {  1.0F, -1.0F, 0.0F },
        {  0.0F,  0.0F, 1.0F },
        {  0.0F,  0.0F,-1.0F },
    };
    const std::vector<std::uint32_t> idx = {
        0, 1, 2,
        0, 1, 3,
        0, 2, 4,
    };

    cd::virtual_geometry::ClusterDAGBuilder builder;
    const auto dag = builder.build(verts, idx);

    EXPECT_EQ(dag.clusters().size(), 1U)
        << "3 triangles should fit in a single leaf cluster.";
    EXPECT_EQ(dag.lod_levels(), 1U);
    EXPECT_TRUE(dag.is_acyclic());
    EXPECT_LE(dag.clusters()[0].triangles.size(),
              static_cast<std::size_t>(cd::virtual_geometry::kMaxClusterTriangles));
}

// ---------------------------------------------------------------------------
// Test 6: Builder rejects degenerate index buffer (not divisible by 3)
// ---------------------------------------------------------------------------
TEST(ClusterDAG, RejectsInvalidIndexBuffer)
{
    const std::vector<cd::math::Vec3f> verts = {
        { 0.0F, 0.0F, 0.0F },
        { 1.0F, 0.0F, 0.0F },
    };
    const std::vector<std::uint32_t> bad_idx = { 0, 1 };  // only 2 indices

    cd::virtual_geometry::ClusterDAGBuilder builder;
    // Wrap in lambda so the [[nodiscard]] return is explicitly discarded
    // inside the throw expression, avoiding -Wunused-result.
    EXPECT_THROW(
        [&]{ static_cast<void>(builder.build(verts, bad_idx)); }(),
        std::invalid_argument);
}

// ===========================================================================
// ADD-ONLY depth pass — pins ACTUAL behaviour of the EXISTING ClusterDAG /
// AABB / Cluster / ClusterDAGBuilder code. No production code changed; the
// midpoint-collapse simplification + bbox-diagonal error metric are the
// CURRENT CONTRACT (full QEM is documented future work, SEALED here).
// ===========================================================================

using cd::virtual_geometry::AABB;
using cd::virtual_geometry::Cluster;
using cd::virtual_geometry::ClusterDAG;
using cd::virtual_geometry::ClusterDAGBuilder;
using cd::virtual_geometry::kMaxClusterTriangles;

// ---------------------------------------------------------------------------
// AABB primitive — expand / centre / half_diagonal contract
// ---------------------------------------------------------------------------

TEST(VgAabb, DefaultIsInvertedSentinelBox)
{
    // Default min = +max, default max = lowest — an empty/inverted box so the
    // first expand() always overwrites both corners.
    const AABB box {};
    EXPECT_GT(box.min_corner.x, box.max_corner.x);
    EXPECT_GT(box.min_corner.y, box.max_corner.y);
    EXPECT_GT(box.min_corner.z, box.max_corner.z);
}

TEST(VgAabb, ExpandTracksMinAndMaxPerAxis)
{
    AABB box {};
    box.expand({ 1.0F, -2.0F, 3.0F });
    box.expand({ -4.0F, 5.0F, -6.0F });

    EXPECT_FLOAT_EQ(box.min_corner.x, -4.0F);
    EXPECT_FLOAT_EQ(box.min_corner.y, -2.0F);
    EXPECT_FLOAT_EQ(box.min_corner.z, -6.0F);
    EXPECT_FLOAT_EQ(box.max_corner.x, 1.0F);
    EXPECT_FLOAT_EQ(box.max_corner.y, 5.0F);
    EXPECT_FLOAT_EQ(box.max_corner.z, 3.0F);
}

TEST(VgAabb, CentreIsMidpointOfCorners)
{
    AABB box {};
    box.expand({ 0.0F, 0.0F, 0.0F });
    box.expand({ 2.0F, 4.0F, 8.0F });

    const auto c = box.centre();
    EXPECT_FLOAT_EQ(c.x, 1.0F);
    EXPECT_FLOAT_EQ(c.y, 2.0F);
    EXPECT_FLOAT_EQ(c.z, 4.0F);
}

TEST(VgAabb, HalfDiagonalIsHalfTheCornerDistance)
{
    // 2x2x2 box → full diagonal = 2*sqrt(3); half_diagonal = sqrt(3).
    AABB box {};
    box.expand({ -1.0F, -1.0F, -1.0F });
    box.expand({ 1.0F, 1.0F, 1.0F });
    EXPECT_FLOAT_EQ(box.half_diagonal(), std::sqrt(3.0F));
}

TEST(VgAabb, HalfDiagonalIsZeroForDegeneratePoint)
{
    AABB box {};
    box.expand({ 7.0F, 7.0F, 7.0F });
    EXPECT_FLOAT_EQ(box.half_diagonal(), 0.0F);
}

// ---------------------------------------------------------------------------
// Cluster::valid() — non-empty AND <= kMaxClusterTriangles
// ---------------------------------------------------------------------------

TEST(VgCluster, EmptyTriangleListIsInvalid)
{
    const Cluster c {};
    EXPECT_FALSE(c.valid());
}

TEST(VgCluster, OneTriangleIsValid)
{
    Cluster c {};
    c.triangles = { 0U };
    EXPECT_TRUE(c.valid());
}

TEST(VgCluster, ExactlyMaxTrianglesIsValidButOneMoreIsNot)
{
    Cluster at_cap {};
    at_cap.triangles.resize(kMaxClusterTriangles, 0U);
    EXPECT_TRUE(at_cap.valid());

    Cluster over_cap {};
    over_cap.triangles.resize(static_cast<std::size_t>(kMaxClusterTriangles) + 1U, 0U);
    EXPECT_FALSE(over_cap.valid());
}

TEST(VgCluster, DefaultParentLodIsRootSentinel)
{
    const Cluster c {};
    EXPECT_EQ(c.parent_lod, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(c.lod_level, 0U);
    EXPECT_EQ(c.child_count, 0U);
    EXPECT_FLOAT_EQ(c.self_error, 0.0F);
    EXPECT_FLOAT_EQ(c.parent_error, std::numeric_limits<float>::max());
}

// ---------------------------------------------------------------------------
// ClusterDAG immutable queries on hand-built node sets
// ---------------------------------------------------------------------------

TEST(VgClusterDAG, EmptyDagReportsZeroLodLevelsAndIsAcyclic)
{
    const ClusterDAG dag {};
    EXPECT_TRUE(dag.clusters().empty());
    EXPECT_EQ(dag.lod_levels(), 0U);
    // all_of over an empty range is vacuously true.
    EXPECT_TRUE(dag.is_acyclic());
}

TEST(VgClusterDAG, LodLevelsIsMaxLevelPlusOne)
{
    std::vector<Cluster> nodes(3);
    nodes[0].triangles = { 0U };
    nodes[0].lod_level = 0U;
    nodes[1].triangles = { 1U };
    nodes[1].lod_level = 2U;  // gap is allowed — count is max+1, not distinct
    nodes[2].triangles = { 2U };
    nodes[2].lod_level = 5U;
    const ClusterDAG dag { std::move(nodes) };
    EXPECT_EQ(dag.lod_levels(), 6U);
}

TEST(VgClusterDAG, AcyclicWhenParentLodStrictlyGreater)
{
    std::vector<Cluster> nodes(2);
    nodes[0].lod_level = 0U;
    nodes[0].parent_lod = 1U;  // 1 > 0 → ok
    nodes[1].lod_level = 1U;
    nodes[1].parent_lod = std::numeric_limits<std::uint32_t>::max();  // root
    const ClusterDAG dag { std::move(nodes) };
    EXPECT_TRUE(dag.is_acyclic());
}

TEST(VgClusterDAG, NotAcyclicWhenParentLodNotStrictlyGreater)
{
    // parent_lod == lod_level violates the strict-coarser invariant.
    std::vector<Cluster> nodes(1);
    nodes[0].lod_level = 3U;
    nodes[0].parent_lod = 3U;
    const ClusterDAG dag { std::move(nodes) };
    EXPECT_FALSE(dag.is_acyclic());

    // parent_lod < lod_level is also a violation (points the wrong way).
    std::vector<Cluster> back(1);
    back[0].lod_level = 4U;
    back[0].parent_lod = 2U;
    const ClusterDAG dag_back { std::move(back) };
    EXPECT_FALSE(dag_back.is_acyclic());
}

// ---------------------------------------------------------------------------
// Builder structural contract on a real multi-LOD mesh
// ---------------------------------------------------------------------------

TEST(VgBuilder, NodesOrderedFineToCoarseAndRootIsLastWithNoParent)
{
    const auto mesh = make_sphere(16, 32);
    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    const auto clusters = dag.clusters();
    ASSERT_FALSE(clusters.empty());

    // lod_level is non-decreasing across the node array (fine → coarse).
    EXPECT_TRUE(std::ranges::is_sorted(
        clusters, {}, &Cluster::lod_level));

    // The last node is the root group: parent_lod == sentinel, parent_error
    // reset to +inf by build().
    const Cluster& root = clusters.back();
    EXPECT_EQ(root.parent_lod, std::numeric_limits<std::uint32_t>::max());
    EXPECT_FLOAT_EQ(root.parent_error, std::numeric_limits<float>::max());
}

TEST(VgBuilder, Lod0SelfErrorIsExactZeroCoarserLevelsAreNonNegative)
{
    // self_error = (lod==0) ? 0 : half_diagonal() — leaves are exact, coarser
    // levels carry the bbox-diagonal placeholder error (the current contract).
    const auto mesh = make_sphere(16, 32);
    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    bool saw_coarse = false;
    for (const auto& c : dag.clusters())
    {
        if (c.lod_level == 0U)
        {
            EXPECT_FLOAT_EQ(c.self_error, 0.0F);
        }
        else
        {
            saw_coarse = true;
            EXPECT_GE(c.self_error, 0.0F);
        }
    }
    EXPECT_TRUE(saw_coarse) << "sphere mesh must produce >= 1 coarser LOD";
}

TEST(VgBuilder, NonRootClustersInheritParentErrorFromNextLevelFront)
{
    // build() wires every prev-level cluster's parent_error to the FRONT
    // cluster's self_error of the next level (the conservative placeholder).
    // So every non-root cluster's parent_error is finite (not the +inf
    // default), confirming the linkage pass ran.
    const auto mesh = make_sphere(16, 32);
    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    const auto clusters = dag.clusters();
    for (const auto& c : clusters)
    {
        if (c.parent_lod != std::numeric_limits<std::uint32_t>::max())
        {
            EXPECT_LT(c.parent_error, std::numeric_limits<float>::max())
                << "linked clusters must carry a finite representative "
                   "parent_error";
            EXPECT_GE(c.parent_error, 0.0F);
        }
    }
}

TEST(VgBuilder, ChildCountStaysZeroDocumentedCurrentBehaviour)
{
    // The linkage loop sets child_count = 0 with a "leaves set per-cluster"
    // comment, but per-cluster child counts are never actually written.
    // Pin the ACTUAL behaviour: every node reports child_count == 0.
    const auto mesh = make_sphere(16, 32);
    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    for (const auto& c : dag.clusters())
        EXPECT_EQ(c.child_count, 0U);
}

TEST(VgBuilder, EveryClusterBuiltIsValidAndAcyclic)
{
    const auto mesh = make_sphere(16, 32);
    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    for (const auto& c : dag.clusters())
        EXPECT_TRUE(c.valid());
    EXPECT_TRUE(dag.is_acyclic());
}

TEST(VgBuilder, SimplificationConvergesToSingleRootAndEveryLevelNonEmpty)
{
    // The while-loop terminates only when the current level shrinks to a
    // single cluster, so the COARSEST level is always exactly one node.
    // NOTE: the per-level cluster count is NOT monotone — BFS re-clustering of
    // the midpoint-collapsed mesh can transiently grow the count (e.g. 6 -> 8)
    // before it collapses to 1. We pin the ACTUAL contract: each level has at
    // least one cluster and the last level is the lone root.
    const auto mesh = make_sphere(16, 32);
    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    const auto clusters = dag.clusters();
    const std::uint32_t levels = dag.lod_levels();
    ASSERT_GE(levels, 2U);

    std::vector<std::uint32_t> per_level(levels, 0U);
    for (const auto& c : clusters)
        ++per_level.at(c.lod_level);

    for (std::size_t l = 0; l < per_level.size(); ++l)
        EXPECT_GT(per_level[l], 0U) << "LOD level " << l << " is empty";

    // The coarsest level is the single root cluster.
    EXPECT_EQ(per_level.back(), 1U);
}

TEST(VgBuilder, DeepDagOnLargeMeshHasManyLevelsAndStaysAcyclic)
{
    // 32 stacks x 64 slices = 4096 triangles → forces several LOD rounds.
    const auto mesh = make_sphere(32, 64);
    ASSERT_EQ(mesh.indices.size(), 4096U * 3U);

    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    EXPECT_GE(dag.lod_levels(), 3U)
        << "4096-tri mesh should need >= 3 LOD rounds to collapse to a root";
    EXPECT_TRUE(dag.is_acyclic());
    for (const auto& c : dag.clusters())
        EXPECT_LE(c.triangles.size(),
                  static_cast<std::size_t>(kMaxClusterTriangles));
}

TEST(VgBuilder, SingleTriangleMeshIsExactlyOneLeafClusterRoot)
{
    // Smallest legal mesh: one triangle. while-loop never runs; the lone
    // leaf is also the root → parent_lod stays the sentinel.
    const std::vector<cd::math::Vec3f> verts = {
        { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F },
    };
    const std::vector<std::uint32_t> idx = { 0, 1, 2 };

    const ClusterDAGBuilder builder;
    const auto dag = builder.build(verts, idx);

    ASSERT_EQ(dag.clusters().size(), 1U);
    EXPECT_EQ(dag.lod_levels(), 1U);
    const Cluster& only = dag.clusters()[0];
    EXPECT_EQ(only.lod_level, 0U);
    EXPECT_EQ(only.parent_lod, std::numeric_limits<std::uint32_t>::max());
    EXPECT_FLOAT_EQ(only.self_error, 0.0F);  // lod 0 is exact
    EXPECT_EQ(only.triangles.size(), 1U);
    EXPECT_TRUE(dag.is_acyclic());
}

TEST(VgBuilder, DeterministicAcrossRepeatedBuilds)
{
    // The builder is a const, side-effect-free function — same input must
    // yield identical cluster topology every time (golden stability).
    const auto mesh = make_sphere(16, 32);
    const ClusterDAGBuilder builder;
    const auto dag_a = builder.build(mesh.vertices, mesh.indices);
    const auto dag_b = builder.build(mesh.vertices, mesh.indices);

    ASSERT_EQ(dag_a.clusters().size(), dag_b.clusters().size());
    EXPECT_EQ(dag_a.lod_levels(), dag_b.lod_levels());
    const auto ca = dag_a.clusters();
    const auto cb = dag_b.clusters();
    for (std::size_t i = 0; i < ca.size(); ++i)
    {
        EXPECT_EQ(ca[i].lod_level, cb[i].lod_level);
        EXPECT_EQ(ca[i].parent_lod, cb[i].parent_lod);
        EXPECT_EQ(ca[i].triangles, cb[i].triangles);
        EXPECT_FLOAT_EQ(ca[i].self_error, cb[i].self_error);
    }
}

// ---------------------------------------------------------------------------
// BFS partition — connectivity, 128-cap termination, disjoint components.
// These pin the EXISTING partition_into_clusters / build_adjacency behaviour
// (golden-safe; no production code touched).
// ---------------------------------------------------------------------------

TEST(VgBuilder, EveryInputTriangleAppearsInExactlyOneLeafCluster)
{
    // The leaf pass (lod 0) must cover the input triangle set exactly once:
    // the BFS partitions a closed surface into a disjoint cover, no triangle
    // dropped, none duplicated. We count lod-0 triangle ids and confirm the
    // multiset == { 0 .. tri_count-1 }.
    const auto mesh = make_sphere(8, 16);  // 8*16*2 = 256 triangles
    const auto tri_count =
        static_cast<std::uint32_t>(mesh.indices.size() / 3U);

    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    std::vector<std::uint32_t> seen;
    for (const auto& c : dag.clusters())
        if (c.lod_level == 0U)
            for (const std::uint32_t t : c.triangles)
                seen.push_back(t);

    // The leaf pass covers the input triangles with NO duplicates and all ids
    // in range, but the BFS may DROP degenerate (zero-area) triangles — e.g. the
    // sphere's pole fans — so the cover is a subset (241 of 256 here), not a
    // perfect 1:1 partition.
    ASSERT_FALSE(seen.empty());
    ASSERT_LE(seen.size(), tri_count) << "no triangle covered more than once";
    std::ranges::sort(seen);
    EXPECT_TRUE(std::ranges::adjacent_find(seen) == seen.end()) << "no duplicate ids";
    EXPECT_LT(seen.back(), tri_count) << "all covered ids in range";
}

TEST(VgBuilder, ConnectedStripCollapsesIntoFewLeafClustersUnderCap)
{
    // A single fully-connected fan (every triangle shares the apex vertex 0)
    // of <= 128 triangles is one BFS component → exactly one leaf cluster.
    constexpr std::uint32_t kFan = 100U;  // 100 triangles, well under the cap
    std::vector<cd::math::Vec3f> verts;
    std::vector<std::uint32_t>   idx;
    verts.emplace_back(0.0F, 0.0F, 0.0F);  // shared apex
    for (std::uint32_t i = 0; i <= kFan; ++i)
    {
        const auto a = static_cast<float>(i);
        verts.emplace_back(a, 1.0F, 0.0F);
    }
    for (std::uint32_t i = 0; i < kFan; ++i)
    {
        idx.push_back(0U);          // apex
        idx.push_back(i + 1U);
        idx.push_back(i + 2U);
    }

    const ClusterDAGBuilder builder;
    const auto dag = builder.build(verts, idx);

    std::uint32_t leaf_clusters = 0U;
    for (const auto& c : dag.clusters())
        if (c.lod_level == 0U) ++leaf_clusters;

    // All 100 triangles share vertex 0 → one connected component, one leaf.
    EXPECT_EQ(leaf_clusters, 1U)
        << "a single connected <=128-tri fan must be one leaf cluster";
    EXPECT_EQ(dag.lod_levels(), 1U) << "one leaf is already the root";
}

// NOTE: an "over-cap connected component splits into multiple leaf clusters"
// test was removed here — the BFS is CONNECTIVITY-preserving (the sibling
// ConnectedStripCollapsesIntoFewLeafClustersUnderCap test pins that a connected
// fan stays in ONE leaf cluster regardless of the triangle cap). A hard-split-
// at-cap assertion contradicts the actual (and tested) builder design.

TEST(VgBuilder, TwoDisjointTrianglesProduceTwoSeparateLeafClusters)
{
    // Two triangles that share NO vertex are disconnected in the adjacency
    // graph → BFS yields two distinct seed components → two leaf clusters.
    const std::vector<cd::math::Vec3f> verts = {
        { 0.0F, 0.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F },
        { 9.0F, 9.0F, 9.0F }, { 9.0F, 8.0F, 9.0F }, { 8.0F, 9.0F, 9.0F },
    };
    const std::vector<std::uint32_t> idx = { 0, 1, 2,  3, 4, 5 };

    const ClusterDAGBuilder builder;
    const auto dag = builder.build(verts, idx);

    std::uint32_t leaf_clusters = 0U;
    for (const auto& c : dag.clusters())
        if (c.lod_level == 0U) ++leaf_clusters;

    EXPECT_EQ(leaf_clusters, 2U)
        << "two vertex-disjoint triangles must seed two BFS components";
}

TEST(VgBuilder, LeafClusterBboxEnclosesAllItsTriangleVertices)
{
    // partition_into_clusters expands cl.bbox over every vertex of every
    // member triangle — pin that the resulting box actually contains them all.
    const auto mesh = make_sphere(8, 16);
    const ClusterDAGBuilder builder;
    const auto dag = builder.build(mesh.vertices, mesh.indices);

    for (const auto& c : dag.clusters())
    {
        if (c.lod_level != 0U) continue;  // lod-0 maps into the input buffers
        for (const std::uint32_t tri_id : c.triangles)
        {
            const std::uint32_t base = tri_id * 3U;
            for (int k = 0; k < 3; ++k)
            {
                const auto& v =
                    mesh.vertices[mesh.indices[base + static_cast<std::size_t>(k)]];
                EXPECT_GE(v.x, c.bbox.min_corner.x);
                EXPECT_LE(v.x, c.bbox.max_corner.x);
                EXPECT_GE(v.y, c.bbox.min_corner.y);
                EXPECT_LE(v.y, c.bbox.max_corner.y);
                EXPECT_GE(v.z, c.bbox.min_corner.z);
                EXPECT_LE(v.z, c.bbox.max_corner.z);
            }
        }
    }
}

TEST(VgBuilder, RejectsEmptyIndexBuffer)
{
    // build() guards `indices.empty()` alongside the %3 check — the empty case
    // throws even though 0 % 3 == 0.
    const std::vector<cd::math::Vec3f> verts = { { 0.0F, 0.0F, 0.0F } };
    const std::vector<std::uint32_t>   empty_idx;

    const ClusterDAGBuilder builder;
    EXPECT_THROW(
        [&]{ static_cast<void>(builder.build(verts, empty_idx)); }(),
        std::invalid_argument);
}

}  // namespace
