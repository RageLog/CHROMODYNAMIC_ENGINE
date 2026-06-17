#include <cd/mesh_shader/Meshlet.hpp>

#include <gtest/gtest.h>

#include <numeric>

namespace
{

using cd::mesh_shader::build_meshlets;
using cd::mesh_shader::kTrianglesPerMeshlet;
using cd::mesh_shader::kVerticesPerMeshlet;


TEST(MeshShader, EmptyInputProducesEmptyOutput)
{
    const auto d = build_meshlets({}, {});
    EXPECT_TRUE(d.meshlets.empty());
}

TEST(MeshShader, SingleTriangleProducesOneMeshletWithThreeVertices)
{
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    EXPECT_EQ(d.meshlets[0].vertex_count, 3U);
    EXPECT_EQ(d.meshlets[0].triangle_count, 1U);
}

TEST(MeshShader, MeshletsRespectVertexCap)
{
    // Build a strip with 200 unique vertices, 200 - 2 = 198 triangles.
    std::vector<std::uint32_t> idx;
    std::vector<cd::math::Vec3f> pos;
    pos.reserve(200);
for (std::uint32_t i = 0; i < 200; ++i)
        pos.emplace_back( static_cast<float>(i), 0.0F, 0.0F );
    for (std::uint32_t i = 0; i + 2 < 200; ++i)
    {
        idx.push_back(i);
        idx.push_back(i + 1);
        idx.push_back(i + 2);
    }
    const auto d = build_meshlets(idx, pos);
    EXPECT_FALSE(d.meshlets.empty());
    for (const auto& m : d.meshlets)
    {
        EXPECT_LE(m.vertex_count,   kVerticesPerMeshlet);
        EXPECT_LE(m.triangle_count, kTrianglesPerMeshlet);
    }
}

TEST(MeshShader, MeshletBoundingSpheresContainAllPoints)
{
    std::vector<std::uint32_t> idx { 0, 1, 2, 0, 2, 3 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    const auto& s = d.meshlets[0].bounds_sphere;
    for (const auto& p : pos)
    {
        const float dx = p.x - s.x;
        const float dy = p.y - s.y;
        const float dz = p.z - s.z;
        EXPECT_LE(std::sqrt(dx*dx + dy*dy + dz*dz), s.w + 1e-3F);
    }
}

TEST(MeshShader, TriangleIndicesPointToValidLocalSlots)
{
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    for (const auto& m : d.meshlets)
    {
        for (std::uint32_t i = 0; i < m.triangle_count; ++i)
        {
            for (std::uint32_t j = 0; j < 3; ++j)
            {
                const auto slot = d.triangle_indices[m.triangle_offset + i * 3 + j];
                EXPECT_LT(slot, m.vertex_count);
            }
        }
    }
}

TEST(MeshShader, GlslSkeletonsNonEmpty)
{
    EXPECT_FALSE(cd::mesh_shader::kMeshletTaskGlsl.empty());
    EXPECT_FALSE(cd::mesh_shader::kMeshletMeshGlsl.empty());
    EXPECT_NE(cd::mesh_shader::kMeshletMeshGlsl.find("SetMeshOutputsEXT"),
              std::string_view::npos);
}

// =============================================================================
// BAND 3 — genuinely-untested clusterizer edge branches. The greedy
// CPU-clusterizer-v1 scope (cone/spatial optimisation = promote-on-need) is
// sealed in ADR-20260616-band3-render-core-scope.md §2.3; these pin the
// degenerate-input + cap-boundary + vertex-dedup branches the original 6 tests
// never reached.
// =============================================================================

TEST(MeshShader, IndicesPresentButPositionsEmptyProducesEmptyOutput)
{
    // The early-out is `indices.empty() || positions.empty()`. Only the
    // both-empty case was tested; pin the positions-empty leg.
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    const auto d = build_meshlets(idx, {});
    EXPECT_TRUE(d.meshlets.empty());
    EXPECT_TRUE(d.vertex_indices.empty());
    EXPECT_TRUE(d.triangle_indices.empty());
}

TEST(MeshShader, TrailingPartialTriangleIsIgnored)
{
    // 5 indices = one whole triangle + a 2-index dangling remainder. The
    // `tri + 2 < indices.size()` loop bound must drop the partial triangle.
    std::vector<std::uint32_t> idx { 0, 1, 2, /* dangling */ 0, 1 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    EXPECT_EQ(d.meshlets[0].triangle_count, 1U);
    EXPECT_EQ(d.meshlets[0].vertex_count, 3U);
}

TEST(MeshShader, SharedVerticesAreDedupedWithinMeshlet)
{
    // Two triangles of a quad share the diagonal edge (verts 0 and 2). The
    // slot_for() dedup path must reuse slots so vertex_count is 4, not 6.
    std::vector<std::uint32_t> idx { 0, 1, 2, 0, 2, 3 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    EXPECT_EQ(d.meshlets[0].triangle_count, 2U);
    EXPECT_EQ(d.meshlets[0].vertex_count, 4U);  // deduped diagonal
    EXPECT_EQ(d.vertex_indices.size(), 4U);
}

TEST(MeshShader, VertexCapForcesFlushAndReslot)
{
    // Independent (non-shared) triangles each contribute 3 fresh vertices.
    // With the 64-vertex cap, the 22nd triangle's 3 new verts (66) overflow
    // → the cap-boundary flush + new_count=3 reslot branch fires. Every
    // meshlet must stay within both caps and triangles must be conserved.
    constexpr std::uint32_t kTris = 30;
    std::vector<std::uint32_t> idx;
    std::vector<cd::math::Vec3f> pos;
    for (std::uint32_t t = 0; t < kTris; ++t)
    {
        const std::uint32_t base = t * 3;
        idx.push_back(base);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        pos.emplace_back(static_cast<float>(t),        0.0F, 0.0F);
        pos.emplace_back(static_cast<float>(t) + 0.3F, 1.0F, 0.0F);
        pos.emplace_back(static_cast<float>(t) + 0.6F, 0.0F, 1.0F);
    }
    const auto d = build_meshlets(idx, pos);
    ASSERT_GE(d.meshlets.size(), 2U);  // 90 verts / 64-cap → at least 2 meshlets
    std::uint32_t total_tris = 0;
    for (const auto& m : d.meshlets)
    {
        EXPECT_LE(m.vertex_count,   kVerticesPerMeshlet);
        EXPECT_LE(m.triangle_count, kTrianglesPerMeshlet);
        EXPECT_GT(m.vertex_count, 0U);
        total_tris += m.triangle_count;
    }
    EXPECT_EQ(total_tris, kTris);  // no triangle dropped across the flush
}

TEST(MeshShader, DegenerateTriangleStillProducesBoundedSphere)
{
    // Collinear (zero-area) triangle: the Ritter bounds must still contain all
    // three points and produce a finite, non-negative radius.
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };  // all on the x-axis
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    const auto& s = d.meshlets[0].bounds_sphere;
    EXPECT_GE(s.w, 0.0F);
    EXPECT_TRUE(std::isfinite(s.w));
    for (const auto& p : pos)
    {
        const float dx = p.x - s.x;
        const float dy = p.y - s.y;
        const float dz = p.z - s.z;
        EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), s.w + 1e-3F);
    }
}

TEST(MeshShader, FreshMeshletConeCutoffDefaultsToZero)
{
    // build_meshlets() fills bounds_sphere but leaves cone_axis_cutoff at its
    // default (greedy v1 does no cone fit — that's the sealed promote-on-need
    // item). Pin the documented default so a future cone-fit can't silently
    // regress callers relying on the zero sentinel.
    std::vector<std::uint32_t> idx { 0, 1, 2 };
    std::vector<cd::math::Vec3f> pos {
        { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    const auto d = build_meshlets(idx, pos);
    ASSERT_EQ(d.meshlets.size(), 1U);
    const auto& c = d.meshlets[0].cone_axis_cutoff;
    EXPECT_FLOAT_EQ(c.x, 0.0F);
    EXPECT_FLOAT_EQ(c.y, 0.0F);
    EXPECT_FLOAT_EQ(c.z, 0.0F);
    EXPECT_FLOAT_EQ(c.w, 0.0F);
}

TEST(MeshShader, TaskGlslSkeletonCarriesConeCullAndPayload)
{
    // The original GLSL test only inspected the MESH skeleton. Pin that the
    // TASK skeleton (the cull stage) is non-empty and references its
    // load-bearing tokens.
    const auto task = cd::mesh_shader::kMeshletTaskGlsl;
    EXPECT_FALSE(task.empty());
    EXPECT_NE(task.find("EmitMeshTasksEXT"), std::string_view::npos);
    EXPECT_NE(task.find("cone_axis_cutoff"), std::string_view::npos);
}

}  // namespace
