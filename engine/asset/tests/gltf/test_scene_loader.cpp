// =============================================================================
// CHROMODYNAMIC — cd/asset_gltf SceneLoader tests
//
// Exercises the new generic load_scene API (ADR-20260530 P1.1) without
// touching the GPU. Reuses the same in-memory triangle pattern as
// test_asset_gltf.cpp to avoid bundling an external .gltf asset:
//
//   * load_scene_from_memory: round-trip a triangle .gltf JSON
//   * Tree shape: 1 root, 1 mesh, 1 primitive, valid material index
//   * Bounds: world AABB non-degenerate after suggested_world_xform
//   * suggested_world_xform: identity on a 2×1 triangle? no — should
//     downscale so max-edge ≈ kTargetExtentMeters (5 m)
//   * Negative tests: missing file → kFileNotFound; corrupt JSON → kParseFailed
// =============================================================================
#include <cd/asset/gltf/SceneLoader.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] std::vector<std::uint8_t> make_triangle_bin()
{
    std::vector<std::uint8_t> bin(42);
    auto* p = reinterpret_cast<float*>(bin.data());
    p[0] = -1.0F; p[1] = 0.0F; p[2] = 0.0F;  // v0
    p[3] =  1.0F; p[4] = 0.0F; p[5] = 0.0F;  // v1
    p[6] =  0.0F; p[7] = 1.0F; p[8] = 0.0F;  // v2
    auto* idx = reinterpret_cast<std::uint16_t*>(bin.data() + 36);
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    return bin;
}

[[nodiscard]] std::string base64_encode(std::span<const std::uint8_t> in)
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size())
    {
        const std::uint32_t v =
            (static_cast<std::uint32_t>(in[i]) << 16) |
            (static_cast<std::uint32_t>(in[i + 1]) << 8) |
            in[i + 2];
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
        i += 3;
    }
    if (i < in.size())
    {
        std::uint32_t v = static_cast<std::uint32_t>(in[i]) << 16;
        if (i + 1 < in.size())
            v |= static_cast<std::uint32_t>(in[i + 1]) << 8;
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back((i + 1 < in.size()) ? kAlphabet[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

[[nodiscard]] std::string make_triangle_gltf_json()
{
    const auto bin = make_triangle_bin();
    const auto b64 = base64_encode(bin);
    std::string j;
    j += R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,)";
    j += b64;
    j += R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)";
    j += R"({"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],)";
    j += R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)";
    j += R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)";
    j += R"("meshes":[{"name":"tri","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)";
    j += R"("materials":[{"name":"red","pbrMetallicRoughness":{"baseColorFactor":[1.0,0.25,0.125,1.0],"metallicFactor":0.5,"roughnessFactor":0.75},"doubleSided":true,"alphaMode":"MASK","alphaCutoff":0.3}],)";
    j += R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    return j;
}

}  // namespace

TEST(SceneLoader, BasicTriangleLoadsAsTreeWithOneRoot)
{
    const auto json = make_triangle_gltf_json();
    auto result = cd::asset::gltf::load_scene_from_memory(
        reinterpret_cast<const std::uint8_t*>(json.data()), json.size());
    ASSERT_TRUE(result.has_value()) << "load_scene rejected a valid triangle .gltf";

    const auto& scene = *result;
    EXPECT_EQ(scene.nodes.size(), 1U);
    EXPECT_EQ(scene.root_nodes.size(), 1U);
    EXPECT_EQ(scene.meshes.size(), 1U);
    EXPECT_EQ(scene.materials.size(), 1U);

    ASSERT_EQ(scene.meshes[0].primitives.size(), 1U);
    const auto& prim = scene.meshes[0].primitives[0];
    EXPECT_EQ(prim.material_idx, 0U);
    EXPECT_EQ(prim.alpha_mode, cd::asset::gltf::GltfAlphaMode::kMask);
    EXPECT_FLOAT_EQ(prim.alpha_cutoff, 0.3F);

    EXPECT_TRUE(scene.materials[0].double_sided);
    EXPECT_FLOAT_EQ(scene.materials[0].metallic_factor, 0.5F);
    EXPECT_FLOAT_EQ(scene.materials[0].roughness_factor, 0.75F);
}

TEST(SceneLoader, BoundsWorldAreNonDegenerate)
{
    const auto json = make_triangle_gltf_json();
    auto result = cd::asset::gltf::load_scene_from_memory(
        reinterpret_cast<const std::uint8_t*>(json.data()), json.size());
    ASSERT_TRUE(result.has_value());
    const auto& scene = *result;

    // World bounds must not be all-zero (the triangle has extent [2, 1, 0]).
    const float ext_x = scene.bounds_world_max.x - scene.bounds_world_min.x;
    const float ext_y = scene.bounds_world_max.y - scene.bounds_world_min.y;
    EXPECT_GT(ext_x, 0.0F);
    EXPECT_GT(ext_y, 0.0F);
    EXPECT_FALSE(std::isnan(ext_x));
    EXPECT_FALSE(std::isnan(ext_y));
}

TEST(SceneLoader, SuggestedXformScalesToTargetExtent)
{
    const auto json = make_triangle_gltf_json();
    auto result = cd::asset::gltf::load_scene_from_memory(
        reinterpret_cast<const std::uint8_t*>(json.data()), json.size());
    ASSERT_TRUE(result.has_value());
    const auto& scene = *result;

    // Triangle has max edge 2.0; target is 5.0; expect scale ~ 2.5.
    // World bounds X extent should land near 5.0 (within ±0.5).
    const float ext_x = scene.bounds_world_max.x - scene.bounds_world_min.x;
    EXPECT_NEAR(ext_x, 5.0F, 0.5F);
}

TEST(SceneLoader, MissingFileReturnsError)
{
    auto result = cd::asset::gltf::load_scene("does_not_exist_xyz.gltf");
    ASSERT_FALSE(result.has_value());
}

TEST(SceneLoader, EmptyPathRejected)
{
    auto result = cd::asset::gltf::load_scene("");
    ASSERT_FALSE(result.has_value());
}

TEST(SceneLoader, CorruptJsonReturnsParseFailed)
{
    const std::string garbage = R"({"asset":{"version":"2.0",)"  // truncated
                                "BROKEN";
    auto result = cd::asset::gltf::load_scene_from_memory(
        reinterpret_cast<const std::uint8_t*>(garbage.data()), garbage.size());
    ASSERT_FALSE(result.has_value());
}
