// =============================================================================
// CHROMODYNAMIC — cd/asset_gltf tests
//
// These tests exercise GltfLoader without touching the GPU. Strategy:
//   * Materialise a minimal glTF JSON (single triangle) into a temp directory,
//     load it, and verify the decoded scene. No external assets in-repo.
//   * Run the same shape through a .glb (binary glTF) container.
//   * Round-trip material decoding (base color factor, double-sided flag).
//   * Negative tests: missing file, malformed JSON, missing POSITION.
// =============================================================================
#include <cd/asset/gltf/GltfLoader.hpp>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

namespace fs = std::filesystem;

// Single-triangle glTF (3 floats × 3 vertices = 36 bytes, plus a 6-byte
// uint16x3 index block = 42 bytes total in the bin buffer).
//
// Buffer layout:
//   [0..36)  POSITION  (3 vec3 floats)
//   [36..42) INDICES   (3 uint16)
//
// We pack everything into a single buffer view per accessor, with explicit
// byteOffset values so tinygltf has no ambiguity to resolve.
//
// The resulting embedded `data:` URI base64-encodes those 42 bytes.

[[nodiscard]] std::vector<std::uint8_t> make_triangle_bin()
{
    std::vector<std::uint8_t> bin(42);
    auto* p = reinterpret_cast<float*>(bin.data());
    // v0 (-1, 0, 0)
    p[0] = -1.0F;
    p[1] = 0.0F;
    p[2] = 0.0F;
    // v1 (+1, 0, 0)
    p[3] = 1.0F;
    p[4] = 0.0F;
    p[5] = 0.0F;
    // v2 (0, 1, 0)
    p[6] = 0.0F;
    p[7] = 1.0F;
    p[8] = 0.0F;
    // indices (0,1,2)
    auto* idx = reinterpret_cast<std::uint16_t*>(bin.data() + 36);
    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;
    return bin;
}

[[nodiscard]] std::string base64_encode(std::span<const std::uint8_t> in)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size())
    {
        const std::uint32_t v =
            (static_cast<std::uint32_t>(in[i]) << 16) | (static_cast<std::uint32_t>(in[i + 1]) << 8) | in[i + 2];
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
    j +=
        R"("materials":[{"name":"red","pbrMetallicRoughness":{"baseColorFactor":[1.0,0.25,0.125,1.0],"metallicFactor":0.5,"roughnessFactor":0.75},"doubleSided":true}],)";
    j += R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    return j;
}

/// Monotonic counter so every TempFile in the process gets a unique name
/// without needing OS-specific getpid (which differs between MSVC, MinGW,
/// and POSIX headers). Combined with the steady_clock epoch this collides
/// only across concurrent ctest invocations launched within the same ns,
/// which `fs::temp_directory_path` cleanup makes harmless anyway.
[[nodiscard]] std::uint64_t next_unique_id()
{
    static std::atomic<std::uint64_t> counter { 0 };
    return counter.fetch_add(1, std::memory_order_relaxed);
}

/// Write content to a uniquely-named file in the OS temp dir; remove on dtor.
struct TempFile
{
    fs::path path;

    explicit TempFile(std::string_view suffix)
    {
        const auto stamp =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        const auto name = std::string { "cd_gltf_test_" } + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
                          std::to_string(next_unique_id()) + std::string { suffix };
        path = fs::temp_directory_path() / name;
    }

    ~TempFile()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;
};

void write_file(const fs::path& p, std::string_view content)
{
    std::ofstream f(p, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void write_file(const fs::path& p, std::span<const std::uint8_t> bytes)
{
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// ---- .glb (binary glTF) builder -------------------------------------------

[[nodiscard]] std::vector<std::uint8_t> make_triangle_glb()
{
    // glb format (v2):
    //   header: magic("glTF"), version(2), length
    //   chunk0: JSON  — { length, type("JSON"), payload }
    //   chunk1: BIN   — { length, type("BIN\0"), payload }
    // JSON references the BIN chunk via buffer[0].uri OMITTED.
    auto bin = make_triangle_bin();
    while ((bin.size() % 4) != 0)
        bin.push_back(0);  // BIN chunk must be 4-byte aligned.

    std::string j = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42}],)"
                    R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},)"
                    R"({"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],)"
                    R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
                    R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],)"
                    R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
                    R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    while ((j.size() % 4) != 0)
        j.push_back(' ');  // JSON chunk must be 4-byte aligned (space-padded).

    const auto json_chunk_len = static_cast<std::uint32_t>(j.size());
    const auto bin_chunk_len = static_cast<std::uint32_t>(bin.size());
    const std::uint32_t total = 12U                    // header
                                + 8U + json_chunk_len  // JSON chunk header + payload
                                + 8U + bin_chunk_len;  // BIN  chunk header + payload

    std::vector<std::uint8_t> glb;
    glb.reserve(total);
    auto push32 = [&](std::uint32_t v)
    {
        glb.push_back(static_cast<std::uint8_t>(v & 0xFF));
        glb.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        glb.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        glb.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };
    // Header.
    glb.insert(glb.end(), { 'g', 'l', 'T', 'F' });
    push32(2);
    push32(total);
    // JSON chunk.
    push32(json_chunk_len);
    glb.insert(glb.end(), { 'J', 'S', 'O', 'N' });
    glb.insert(glb.end(), j.begin(), j.end());
    // BIN chunk.
    push32(bin_chunk_len);
    glb.insert(glb.end(), { 'B', 'I', 'N', 0 });
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

}  // namespace

// ----------------------------------------------------------------------------
// .gltf (ASCII + embedded data URI buffer)
// ----------------------------------------------------------------------------

TEST(GltfLoader, AsciiTriangleDecodesPositionsAndIndices)
{
    TempFile f { ".gltf" };
    write_file(f.path, make_triangle_gltf_json());

    auto r = cd::asset::gltf::load_gltf(f.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;

    const auto& scene = *r;
    ASSERT_EQ(scene.meshes.size(), 1U);
    ASSERT_EQ(scene.meshes[0].primitives.size(), 1U);

    const auto& prim = scene.meshes[0].primitives[0];
    ASSERT_EQ(prim.vertices.size(), 3U);
    ASSERT_EQ(prim.indices.size(), 3U);
    EXPECT_FLOAT_EQ(prim.vertices[0].position[0], -1.0F);
    EXPECT_FLOAT_EQ(prim.vertices[2].position[1], 1.0F);
    EXPECT_EQ(prim.indices[0], 0U);
    EXPECT_EQ(prim.indices[1], 1U);
    EXPECT_EQ(prim.indices[2], 2U);
    EXPECT_EQ(prim.material_index, 0);
}

TEST(GltfLoader, AsciiTriangleAccumulatesBoundingBox)
{
    TempFile f { ".gltf" };
    write_file(f.path, make_triangle_gltf_json());
    auto r = cd::asset::gltf::load_gltf(f.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;

    const auto& scene = *r;
    EXPECT_FLOAT_EQ(scene.bbox_min[0], -1.0F);
    EXPECT_FLOAT_EQ(scene.bbox_min[1], 0.0F);
    EXPECT_FLOAT_EQ(scene.bbox_max[0], 1.0F);
    EXPECT_FLOAT_EQ(scene.bbox_max[1], 1.0F);
}

TEST(GltfLoader, AsciiMaterialBaseColorAndFlagsRoundtrip)
{
    TempFile f { ".gltf" };
    write_file(f.path, make_triangle_gltf_json());
    auto r = cd::asset::gltf::load_gltf(f.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;

    const auto& scene = *r;
    ASSERT_EQ(scene.materials.size(), 1U);
    const auto& m = scene.materials[0];
    EXPECT_EQ(m.name, "red");
    EXPECT_FLOAT_EQ(m.base_color_factor[0], 1.0F);
    EXPECT_FLOAT_EQ(m.base_color_factor[1], 0.25F);
    EXPECT_FLOAT_EQ(m.base_color_factor[2], 0.125F);
    EXPECT_FLOAT_EQ(m.base_color_factor[3], 1.0F);
    EXPECT_FLOAT_EQ(m.metallic_factor, 0.5F);
    EXPECT_FLOAT_EQ(m.roughness_factor, 0.75F);
    EXPECT_TRUE(m.double_sided);
    EXPECT_EQ(m.base_color_texture, -1);
}

// ----------------------------------------------------------------------------
// .glb (binary glTF)
// ----------------------------------------------------------------------------

TEST(GltfLoader, BinaryGlbTriangleDecodes)
{
    TempFile f { ".glb" };
    write_file(f.path, make_triangle_glb());

    auto r = cd::asset::gltf::load_gltf(f.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;

    const auto& scene = *r;
    ASSERT_EQ(scene.meshes.size(), 1U);
    ASSERT_EQ(scene.meshes[0].primitives.size(), 1U);
    const auto& prim = scene.meshes[0].primitives[0];
    EXPECT_EQ(prim.vertices.size(), 3U);
    EXPECT_EQ(prim.indices.size(), 3U);
    EXPECT_FLOAT_EQ(prim.vertices[0].position[0], -1.0F);
}

// ----------------------------------------------------------------------------
// Negative paths
// ----------------------------------------------------------------------------

TEST(GltfLoader, MissingFileReturnsFileNotFound)
{
    auto r = cd::asset::gltf::load_gltf("c:/definitely/does/not/exist.gltf");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::gltf::gltf_errors::Code::kFileNotFound));
}

TEST(GltfLoader, EmptyPathReturnsInvalidArgument)
{
    auto r = cd::asset::gltf::load_gltf("");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::gltf::gltf_errors::Code::kInvalidArgument));
}

TEST(GltfLoader, MalformedJsonReturnsParseFailed)
{
    TempFile f { ".gltf" };
    write_file(f.path, std::string_view { "{ not valid json" });
    auto r = cd::asset::gltf::load_gltf(f.path.string());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::asset::gltf::gltf_errors::Code::kParseFailed));
}

TEST(GltfLoader, PrimitiveWithoutPositionYieldsEmptyVertices)
{
    // Minimal glTF with a primitive that has only an index buffer but no
    // POSITION attribute. The loader contract is "no POSITION → empty
    // primitive", NOT an error — that lets a multi-primitive mesh still
    // load the primitives that DO have POSITION.
    std::string j =
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":6,"uri":"data:application/octet-stream;base64,AAABAAIA"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":6,"target":34963}],)"
        R"("accessors":[{"bufferView":0,"componentType":5123,"count":3,"type":"SCALAR"}],)"
        R"("meshes":[{"primitives":[{"attributes":{},"indices":0}]}],)"
        R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    TempFile f { ".gltf" };
    write_file(f.path, j);
    auto r = cd::asset::gltf::load_gltf(f.path.string());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    const auto& prim = r->meshes[0].primitives[0];
    EXPECT_TRUE(prim.vertices.empty());
    EXPECT_TRUE(prim.indices.empty());  // load_gltf skips index decode when POSITION absent
}

// ----- AssetLoader adapter -----

#include <cd/asset/gltf/AssetLoader.hpp>

TEST(GltfAssetLoader, AdapterDecodesMinimalAsciiGltf)
{
    constexpr std::string_view text = R"({"asset":{"version":"2.0"},)"
                                      R"("meshes":[{"primitives":[{"attributes":{}}]}],)"
                                      R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    cd::asset::gltf::GltfAssetLoader loader;
    EXPECT_EQ(loader.tag(), "gltf");
    auto r = loader.decode(std::span<const std::byte> { bytes.data(), bytes.size() }, "t.gltf");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    auto* g = dynamic_cast<cd::asset::gltf::GltfAsset*>(r->get());
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->scene().meshes.size(), 1u);
    EXPECT_EQ(g->scene().nodes.size(), 1u);
}

TEST(GltfAssetLoader, AdapterTooSmallBufferRejected)
{
    std::vector<std::byte> tiny(2, std::byte { 0 });
    cd::asset::gltf::GltfAssetLoader loader;
    auto r = loader.decode(std::span<const std::byte> { tiny.data(), tiny.size() }, "x.gltf");
    ASSERT_FALSE(r.has_value());
}

// =============================================================================
// Wave 29 — glTF skin extraction (JOINTS_0 / WEIGHTS_0 / skin index / IBMs)
// =============================================================================

TEST(GltfLoader, NoSkinSceneLeavesSkinsEmpty)
{
    constexpr std::string_view minimal =
        R"({"asset":{"version":"2.0"},)"
        R"("meshes":[{"primitives":[{"attributes":{}}]}],)"
        R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    auto r = cd::asset::gltf::load_gltf_from_memory(
        reinterpret_cast<const std::uint8_t*>(minimal.data()), minimal.size());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_TRUE(r->skins.empty());
    EXPECT_EQ(r->nodes.front().skin_index, -1);
    if (!r->meshes.front().primitives.empty())
        EXPECT_TRUE(r->meshes.front().primitives.front().skin_vertices.empty());
}

// Phase 170 — SkinnedMeshBridge
#include <cd/asset/gltf/SkinnedMeshBridge.hpp>

TEST(SkinnedMeshBridge, ToSkinnedVerticesPreservesPositionsAndUv)
{
    cd::asset::gltf::GltfPrimitive prim;
    prim.vertices.resize(2);
    prim.vertices[0].position = { 1.0F, 2.0F, 3.0F };
    prim.vertices[0].normal   = { 0.0F, 1.0F, 0.0F };
    prim.vertices[0].texcoord0 = { 0.25F, 0.75F };
    prim.vertices[1].position = { 4.0F, 5.0F, 6.0F };
    cd::asset::gltf::GltfSkinVertex sv;
    sv.joints = { 2, 5, 0, 0 };
    sv.weights = { 0.6F, 0.4F, 0.0F, 0.0F };
    prim.skin_vertices.push_back(sv);
    prim.skin_vertices.push_back({});

    auto out = cd::asset::gltf::to_skinned_vertices(prim);
    ASSERT_EQ(out.size(), 2U);
    EXPECT_FLOAT_EQ(out[0].position.x, 1.0F);
    EXPECT_FLOAT_EQ(out[0].uv.x, 0.25F);
    EXPECT_EQ(out[0].bone_ids[0], 2);
    EXPECT_EQ(out[0].bone_ids[1], 5);
    EXPECT_FLOAT_EQ(out[0].bone_weights[0], 0.6F);
    EXPECT_FLOAT_EQ(out[1].position.y, 5.0F);
}

TEST(SkinnedMeshBridge, ToSkinnedVerticesHandlesNonSkinnedPrimitive)
{
    cd::asset::gltf::GltfPrimitive prim;
    prim.vertices.resize(1);
    prim.vertices[0].position = { 7.0F, 8.0F, 9.0F };
    // No skin_vertices.
    auto out = cd::asset::gltf::to_skinned_vertices(prim);
    ASSERT_EQ(out.size(), 1U);
    // Default fallback: full weight on bone 0.
    EXPECT_FLOAT_EQ(out[0].bone_weights[0], 1.0F);
    EXPECT_FLOAT_EQ(out[0].bone_weights[1], 0.0F);
}

TEST(SkinnedMeshBridge, ToSkeletonHandlesEmptyScene)
{
    cd::asset::gltf::GltfScene scene;
    auto skel = cd::asset::gltf::to_skeleton(scene, 0);
    EXPECT_EQ(skel.joint_count(), 0U);
}

TEST(SkinnedMeshBridge, ToSkeletonTwoJointChain)
{
    cd::asset::gltf::GltfScene scene;
    scene.nodes.resize(2);
    scene.nodes[0].name = "root";
    scene.nodes[0].parent = -1;
    scene.nodes[0].children = { 1 };
    scene.nodes[0].local_matrix = cd::math::Mat4f::identity();
    scene.nodes[1].name = "child";
    scene.nodes[1].parent = 0;
    scene.nodes[1].local_matrix = cd::math::Mat4f::identity();
    cd::asset::gltf::GltfSkin skin;
    skin.joints = { 0, 1 };
    skin.inverse_bind_matrices = { cd::math::Mat4f::identity(),
                                   cd::math::Mat4f::identity() };
    scene.skins.push_back(std::move(skin));

    auto skel = cd::asset::gltf::to_skeleton(scene, 0);
    ASSERT_EQ(skel.joint_count(), 2U);
    EXPECT_EQ(skel.joint(0).parent, -1);
    EXPECT_EQ(skel.joint(1).parent, 0);
}

// =============================================================================
// SK1/SK3 (phase 226/227) — animation parse + bridge sampling.
// =============================================================================

TEST(SkinnedMeshBridge, ToSkeletonBundleProducesNodeToJointMap)
{
    cd::asset::gltf::GltfScene scene;
    scene.nodes.resize(2);
    scene.nodes[0].name = "root";
    scene.nodes[1].name = "child";
    scene.nodes[0].children = { 1 };
    scene.nodes[1].parent = 0;
    cd::asset::gltf::GltfSkin skin;
    skin.joints = { 0, 1 };
    skin.inverse_bind_matrices = { cd::math::Mat4f::identity(),
                                   cd::math::Mat4f::identity() };
    scene.skins.push_back(std::move(skin));

    auto bundle = cd::asset::gltf::to_skeleton_bundle(scene, 0);
    EXPECT_EQ(bundle.skeleton.joint_count(), 2U);
    ASSERT_TRUE(bundle.node_to_joint.contains(0));
    ASSERT_TRUE(bundle.node_to_joint.contains(1));
    // Root must map to joint index 0 after topo sort; child to joint 1.
    EXPECT_EQ(bundle.node_to_joint[0], 0);
    EXPECT_EQ(bundle.node_to_joint[1], 1);
}

TEST(SkinnedMeshBridge, SampleGltfAnimationLinearTranslation)
{
    cd::asset::gltf::GltfAnimation anim;
    anim.name = "test";
    cd::asset::gltf::GltfAnimSampler s;
    s.times  = { 0.0F, 1.0F };
    s.values = { 0.0F, 0.0F, 0.0F,    // pos at t=0
                 2.0F, 4.0F, 6.0F };  // pos at t=1
    s.interpolation = cd::asset::gltf::GltfInterpolation::kLinear;
    anim.samplers.push_back(std::move(s));
    cd::asset::gltf::GltfAnimChannel ch;
    ch.sampler_index = 0;
    ch.target_node = 7;
    ch.path = cd::asset::gltf::GltfTargetPath::kTranslation;
    anim.channels.push_back(ch);
    anim.duration = 1.0F;

    std::unordered_map<int, std::int32_t> node_to_joint;
    node_to_joint[7] = 0;
    cd::anim::Pose pose;
    pose.joint_locals.resize(1);

    cd::asset::gltf::sample_gltf_animation(anim, node_to_joint, 0.5F, pose);
    EXPECT_FLOAT_EQ(pose.joint_locals[0].position.x, 1.0F);
    EXPECT_FLOAT_EQ(pose.joint_locals[0].position.y, 2.0F);
    EXPECT_FLOAT_EQ(pose.joint_locals[0].position.z, 3.0F);
}

TEST(SkinnedMeshBridge, SampleGltfAnimationStepHoldsLowerKey)
{
    cd::asset::gltf::GltfAnimation anim;
    cd::asset::gltf::GltfAnimSampler s;
    s.times  = { 0.0F, 1.0F };
    s.values = { 0.0F, 0.0F, 0.0F, 10.0F, 10.0F, 10.0F };
    s.interpolation = cd::asset::gltf::GltfInterpolation::kStep;
    anim.samplers.push_back(std::move(s));
    cd::asset::gltf::GltfAnimChannel ch { 0, 7, cd::asset::gltf::GltfTargetPath::kTranslation };
    anim.channels.push_back(ch);

    std::unordered_map<int, std::int32_t> node_to_joint { { 7, 0 } };
    cd::anim::Pose pose;
    pose.joint_locals.resize(1);

    cd::asset::gltf::sample_gltf_animation(anim, node_to_joint, 0.5F, pose);
    // STEP: holds the lower key value (0,0,0) until next key.
    EXPECT_FLOAT_EQ(pose.joint_locals[0].position.x, 0.0F);
}

TEST(SkinnedMeshBridge, SampleGltfAnimationRotationSlerps)
{
    // Two keys: identity at t=0, 180° around Y at t=1. At t=0.5 we expect
    // 90° around Y, i.e. (0, sin45, 0, cos45).
    cd::asset::gltf::GltfAnimation anim;
    cd::asset::gltf::GltfAnimSampler s;
    s.times  = { 0.0F, 1.0F };
    s.values = { 0.0F, 0.0F, 0.0F, 1.0F,  // identity quat (x, y, z, w)
                 0.0F, 1.0F, 0.0F, 0.0F };// 180° about Y
    s.interpolation = cd::asset::gltf::GltfInterpolation::kLinear;
    anim.samplers.push_back(std::move(s));
    cd::asset::gltf::GltfAnimChannel ch { 0, 7, cd::asset::gltf::GltfTargetPath::kRotation };
    anim.channels.push_back(ch);

    std::unordered_map<int, std::int32_t> node_to_joint { { 7, 0 } };
    cd::anim::Pose pose;
    pose.joint_locals.resize(1);

    cd::asset::gltf::sample_gltf_animation(anim, node_to_joint, 0.5F, pose);
    const float kEps = 1e-3F;
    const float kSqrt2Half = 0.70710678F;
    EXPECT_NEAR(pose.joint_locals[0].rotation.y, kSqrt2Half, kEps);
    EXPECT_NEAR(pose.joint_locals[0].rotation.w, kSqrt2Half, kEps);
}

// =============================================================================
// Robustness / edge / negative coverage for load_gltf_from_memory
// (≥80→100 marathon, ADD-ONLY). Defensive deserialization: malformed blobs
// must return a typed error, never crash. Valid-input decode is unchanged.
// =============================================================================

namespace
{
using GCode = cd::asset::gltf::gltf_errors::Code;

[[nodiscard]] cd::core::Result<cd::asset::gltf::GltfScene> mem_load(std::string_view text)
{
    return cd::asset::gltf::load_gltf_from_memory(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}
}  // namespace

TEST(GltfFromMemoryEdge, NullBufferReturnsInvalidArgument)
{
    auto r = cd::asset::gltf::load_gltf_from_memory(nullptr, 128);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(GCode::kInvalidArgument));
}

TEST(GltfFromMemoryEdge, TooSmallBufferReturnsParseFailed)
{
    const std::array<std::uint8_t, 3> tiny { 'g', 'l', 'T' };
    auto r = cd::asset::gltf::load_gltf_from_memory(tiny.data(), tiny.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(GCode::kParseFailed));
}

TEST(GltfFromMemoryEdge, NonGltfTextReturnsParseFailed)
{
    auto r = mem_load("this is definitely not glTF JSON at all");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(GCode::kParseFailed));
}

TEST(GltfFromMemoryEdge, TruncatedJsonReturnsParseFailed)
{
    auto r = mem_load(R"({"asset":{"version":"2.0")");  // missing closing braces
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(GCode::kParseFailed));
}

TEST(GltfFromMemoryEdge, GlbMagicButTruncatedReturnsParseFailed)
{
    // "glTF" magic routes to the binary loader; the rest is junk → tinygltf
    // rejects it as a malformed container.
    const std::array<std::uint8_t, 12> fake_glb {
        'g', 'l', 'T', 'F', 0x02, 0, 0, 0, 0x10, 0, 0, 0
    };
    auto r = cd::asset::gltf::load_gltf_from_memory(fake_glb.data(), fake_glb.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(GCode::kParseFailed));
}

TEST(GltfFromMemoryEdge, EmptyJsonObjectYieldsEmptyScene)
{
    // A well-formed but content-free glTF: valid JSON, no meshes/nodes.
    // tinygltf accepts {} with asset version absent on some builds, but a
    // bare "{}" lacks the required asset block; either way we must not crash.
    auto r = mem_load(R"({"asset":{"version":"2.0"}})");
    // Whatever tinygltf decides, the result is a typed Result — assert it is
    // either a clean empty scene or a typed parse error, never UB.
    if (r.has_value())
    {
        EXPECT_TRUE(r->meshes.empty());
        EXPECT_TRUE(r->nodes.empty());
        EXPECT_TRUE(r->instances.empty());
    }
    else
    {
        EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(GCode::kParseFailed));
    }
}

TEST(GltfFromMemoryEdge, MeshWithoutPositionDecodesWithoutCrash)
{
    // A primitive with an empty attribute set: the loader skips geometry
    // decode (no POSITION) but must still produce a valid scene object.
    constexpr std::string_view text =
        R"({"asset":{"version":"2.0"},)"
        R"("meshes":[{"primitives":[{"attributes":{}}]}],)"
        R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    auto r = mem_load(text);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->meshes.size(), 1u);
    ASSERT_EQ(r->meshes.front().primitives.size(), 1u);
    EXPECT_TRUE(r->meshes.front().primitives.front().vertices.empty());
}
