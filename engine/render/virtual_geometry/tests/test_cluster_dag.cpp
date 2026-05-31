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

#include <cmath>
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

}  // namespace
