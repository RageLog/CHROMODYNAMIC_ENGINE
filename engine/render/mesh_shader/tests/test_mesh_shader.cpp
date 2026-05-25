#include <cd/mesh_shader/Meshlet.hpp>

#include <gtest/gtest.h>

#include <numeric>

namespace
{

using cd::mesh_shader::build_meshlets;
using cd::mesh_shader::kTrianglesPerMeshlet;
using cd::mesh_shader::kVerticesPerMeshlet;
using cd::mesh_shader::Meshlet;
using cd::mesh_shader::MeshletData;

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
    for (std::uint32_t i = 0; i < 200; ++i)
        pos.push_back({ static_cast<float>(i), 0.0F, 0.0F });
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

}  // namespace
