// =============================================================================
// CHROMODYNAMIC — cd::asset_obj tests
// =============================================================================
#include <cd/asset_obj/ObjLoader.hpp>
#include <gtest/gtest.h>

#include <string_view>

namespace
{

// Simple triangle file: three positions, one face. Should produce 3
// vertices + 3 indices and a synthesized flat normal of (0, 0, 1).
constexpr std::string_view kTriangle = R"obj(
# minimal triangle
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 0.0 1.0 0.0
f 1 2 3
)obj";

// Square (two tris via a quad face) with explicit UVs and normals.
// The dedup table should fold 4 unique (v,vt,vn) tuples to 4 vertices
// and produce 6 indices (one quad → two triangles).
constexpr std::string_view kQuadWithUvAndNormal = R"obj(
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 1.0 1.0 0.0
v 0.0 1.0 0.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
vn 0.0 0.0 1.0
f 1/1/1 2/2/1 3/3/1 4/4/1
)obj";

// Cube: 8 positions, 6 quad faces. After fan-triangulation: 12 triangles
// = 36 indices. The dedup map should fold to 8 vertices since each
// vertex shows up with the same (v, vt=0, vn=0) tuple across faces.
constexpr std::string_view kCube = R"obj(
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
f 1 2 3 4
f 5 6 7 8
f 1 5 6 2
f 2 6 7 3
f 3 7 8 4
f 4 8 5 1
)obj";

}  // namespace

TEST(AssetObj, TriangleProducesThreeVerticesAndOneFace)
{
    auto r = cd::asset_obj::parse_obj(kTriangle);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->vertices.size(), 3U);
    EXPECT_EQ(r->indices.size(), 3U);
    EXPECT_EQ(r->indices[0], 0U);
    EXPECT_EQ(r->indices[1], 1U);
    EXPECT_EQ(r->indices[2], 2U);
}

TEST(AssetObj, TriangleSynthesizesFlatNormalWhenVnAbsent)
{
    auto r = cd::asset_obj::parse_obj(kTriangle);
    ASSERT_TRUE(r.has_value());
    // CCW triangle in Z=0 plane → normal (0, 0, 1).
    for (const auto& v : r->vertices)
    {
        EXPECT_NEAR(v.normal[0], 0.0F, 1e-5F);
        EXPECT_NEAR(v.normal[1], 0.0F, 1e-5F);
        EXPECT_NEAR(v.normal[2], 1.0F, 1e-5F);
    }
}

TEST(AssetObj, QuadFanTriangulatesToTwoTris)
{
    auto r = cd::asset_obj::parse_obj(kQuadWithUvAndNormal);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->vertices.size(), 4U);
    EXPECT_EQ(r->indices.size(), 6U);
    // First tri: 0-1-2, second: 0-2-3 (fan).
    EXPECT_EQ(r->indices[0], 0U);
    EXPECT_EQ(r->indices[1], 1U);
    EXPECT_EQ(r->indices[2], 2U);
    EXPECT_EQ(r->indices[3], 0U);
    EXPECT_EQ(r->indices[4], 2U);
    EXPECT_EQ(r->indices[5], 3U);
}

TEST(AssetObj, ExplicitUvAndNormalAreReadCorrectly)
{
    auto r = cd::asset_obj::parse_obj(kQuadWithUvAndNormal);
    ASSERT_TRUE(r.has_value());
    // Each vertex gets its matching UV; normal is (0,0,1) for every vertex.
    EXPECT_NEAR(r->vertices[0].texcoord0[0], 0.0F, 1e-5F);
    EXPECT_NEAR(r->vertices[0].texcoord0[1], 0.0F, 1e-5F);
    EXPECT_NEAR(r->vertices[2].texcoord0[0], 1.0F, 1e-5F);
    EXPECT_NEAR(r->vertices[2].texcoord0[1], 1.0F, 1e-5F);
    for (const auto& v : r->vertices)
    {
        EXPECT_NEAR(v.normal[0], 0.0F, 1e-5F);
        EXPECT_NEAR(v.normal[2], 1.0F, 1e-5F);
    }
}

TEST(AssetObj, CubeProducesEightVerticesAndThirtySixIndices)
{
    auto r = cd::asset_obj::parse_obj(kCube);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->vertices.size(), 8U);
    EXPECT_EQ(r->indices.size(), 36U);
}

TEST(AssetObj, CubeBoundingBoxIsTight)
{
    auto r = cd::asset_obj::parse_obj(kCube);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->bbox_min[0], -1.0F, 1e-5F);
    EXPECT_NEAR(r->bbox_min[1], -1.0F, 1e-5F);
    EXPECT_NEAR(r->bbox_min[2], -1.0F, 1e-5F);
    EXPECT_NEAR(r->bbox_max[0], 1.0F, 1e-5F);
    EXPECT_NEAR(r->bbox_max[1], 1.0F, 1e-5F);
    EXPECT_NEAR(r->bbox_max[2], 1.0F, 1e-5F);
}

TEST(AssetObj, NegativeIndicesRejected)
{
    constexpr std::string_view neg = R"obj(
v 0 0 0
v 1 0 0
v 0 1 0
f -3 -2 -1
)obj";
    auto r = cd::asset_obj::parse_obj(neg);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_obj::obj_errors::Code::kUnsupported));
}

TEST(AssetObj, EmptyInputReturnsInvalidArgument)
{
    auto r = cd::asset_obj::parse_obj("");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_obj::obj_errors::Code::kInvalidArgument));
}

TEST(AssetObj, MissingFileReturnsFileNotFound)
{
    auto r = cd::asset_obj::load_obj("c:/definitely/does/not/exist.obj");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset_obj::obj_errors::Code::kFileNotFound));
}

// ----- AssetLoader adapter -----

#include <cd/asset_obj/AssetLoader.hpp>

TEST(ObjAssetLoader, AdapterDecodesValidObj)
{
    const std::string_view src = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    std::vector<std::byte> bytes(src.size());
    std::memcpy(bytes.data(), src.data(), src.size());
    cd::asset_obj::ObjAssetLoader loader;
    EXPECT_EQ(loader.tag(), "obj");
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "t.obj");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* a = dynamic_cast<cd::asset_obj::ObjAsset*>(r->get());
    ASSERT_NE(a, nullptr);
    EXPECT_FALSE(a->mesh().vertices.empty());
}
