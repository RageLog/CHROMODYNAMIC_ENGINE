// =============================================================================
// CHROMODYNAMIC — cd::render::scene_ingest tests
//
// Phase 439 (ADR-20260530 P1.4 second half). Exercises ingest_gltf_scene
// against a NullDevice + procedurally built LoadedScene. NullDevice's
// create_* calls all succeed deterministically, so we can validate the
// orchestration logic (ECS entity creation, parent/child wiring, GPU
// handle bookkeeping) without launching a real Vulkan instance.
// =============================================================================
#include <cd/asset/gltf/SceneLoader.hpp>
#include <cd/ecs/World.hpp>
#include <cd/render/scene/SceneIngest.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/scene/Scene.hpp>
#include <gtest/gtest.h>

#include <cstdint>

namespace
{

[[nodiscard]] cd::asset::gltf::LoadedScene make_simple_scene()
{
    cd::asset::gltf::LoadedScene s;

    // One mesh with one primitive (3 vertices, 3 indices).
    cd::asset::gltf::LoadedPrimitive prim {};
    prim.mesh.vertices = {
        { { -1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F } },
        { {  1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F } },
        { {  0.0F, 1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 0.5F, 1.0F } },
    };
    prim.mesh.indices = { 0U, 1U, 2U };
    prim.mesh.material_index = 0;
    prim.material_idx = 0U;
    cd::asset::gltf::LoadedMesh m;
    m.primitives.push_back(std::move(prim));
    s.meshes.push_back(std::move(m));

    cd::asset::gltf::LoadedMaterial mat;
    mat.name = "default";
    s.materials.push_back(std::move(mat));

    // Three-node tree: A -> {B, C}. Root index 0.
    cd::asset::gltf::LoadedNode na;
    na.name = "A";
    na.children = { 1U, 2U };
    na.mesh_idx = 0U;
    s.nodes.push_back(std::move(na));

    cd::asset::gltf::LoadedNode nb;
    nb.name = "B";
    nb.mesh_idx = 0U;
    s.nodes.push_back(std::move(nb));

    cd::asset::gltf::LoadedNode nc;
    nc.name = "C";
    s.nodes.push_back(std::move(nc));

    s.root_nodes = { 0U };

    s.bounds_world_min = { -1.0F, 0.0F, 0.0F };
    s.bounds_world_max = {  1.0F, 1.0F, 0.0F };
    return s;
}

}  // namespace

TEST(SceneIngest, BasicTreeCreatesEntitiesAndUploadsMeshes)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };
    const auto src = make_simple_scene();

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);
    ASSERT_TRUE(r.has_value());
    auto& res = *r;

    EXPECT_TRUE(res.root_entity.is_valid());
    ASSERT_EQ(res.node_entities.size(), 3U);
    EXPECT_TRUE(res.node_entities[0].is_valid());
    EXPECT_TRUE(res.node_entities[1].is_valid());
    EXPECT_TRUE(res.node_entities[2].is_valid());

    // One mesh -> one per-prim vector of length 1.
    ASSERT_EQ(res.gpu_meshes.size(), 1U);
    ASSERT_EQ(res.gpu_meshes[0].size(), 1U);
    EXPECT_TRUE(res.gpu_meshes[0][0].vb.is_valid());
    EXPECT_TRUE(res.gpu_meshes[0][0].ib.is_valid());
    EXPECT_EQ(res.gpu_meshes[0][0].vertex_count, 3U);
    EXPECT_EQ(res.gpu_meshes[0][0].index_count,  3U);

    // No textures in this scene.
    EXPECT_TRUE(res.gpu_textures.empty());

    // Cleanup; smoke test that destroy_ingest_result is safe.
    cd::render::scene::destroy_ingest_result(dev, res);
    EXPECT_TRUE(res.gpu_meshes.empty());
}

TEST(SceneIngest, ParentChildAttachmentMatchesSource)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };
    const auto src = make_simple_scene();

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;

    // Nodes B (idx 1) and C (idx 2) should have node A (idx 0) as parent.
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[1]), res.node_entities[0]);
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[2]), res.node_entities[0]);
    // Node A's parent should be the synthetic root.
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[0]), res.root_entity);
}

TEST(SceneIngest, CreateEcsNodesFalseSkipsEntities)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };
    const auto src = make_simple_scene();

    cd::render::scene::IngestOptions opts {};
    opts.create_ecs_nodes = false;

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src, opts);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;

    EXPECT_FALSE(res.root_entity.is_valid());
    EXPECT_TRUE(res.node_entities.empty());
    // GPU meshes still uploaded.
    EXPECT_EQ(res.gpu_meshes.size(), 1U);
}

TEST(SceneIngest, BoundsPassThroughFromLoadedScene)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };
    const auto src = make_simple_scene();

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;

    EXPECT_FLOAT_EQ(res.bounds_world_min.x, src.bounds_world_min.x);
    EXPECT_FLOAT_EQ(res.bounds_world_max.x, src.bounds_world_max.x);
    EXPECT_FLOAT_EQ(res.bounds_world_min.y, src.bounds_world_min.y);
    EXPECT_FLOAT_EQ(res.bounds_world_max.y, src.bounds_world_max.y);
}

TEST(SceneIngest, UploadTexturesFalseSkipsTextures)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    auto src = make_simple_scene();
    // Push a synthetic texture to verify the gating actually skips upload.
    cd::asset::gltf::LoadedTexture t;
    t.image.width  = 4U;
    t.image.height = 4U;
    t.image.rgba.assign(std::size_t { 4 } * 4 * 4, std::uint8_t { 255 });
    src.textures.push_back(std::move(t));

    cd::render::scene::IngestOptions opts {};
    opts.upload_textures = false;

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src, opts);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;

    EXPECT_TRUE(res.gpu_textures.empty());
    // Mesh upload still happened.
    EXPECT_EQ(res.gpu_meshes.size(), 1U);
}

TEST(SceneIngest, EmptyImageSlotStaysDefault)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    auto src = make_simple_scene();
    cd::asset::gltf::LoadedTexture t {};  // width = 0, height = 0, rgba empty
    src.textures.push_back(std::move(t));

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;

    // Empty image slot creates a default-constructed entry (invalid handle)
    // rather than failing the whole ingest.
    ASSERT_EQ(res.gpu_textures.size(), 1U);
    EXPECT_FALSE(res.gpu_textures[0].image.is_valid());
}

TEST(SceneIngest, UseSuggestedXformFalseAppliesUserXform)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };
    const auto src = make_simple_scene();

    cd::render::scene::IngestOptions opts {};
    opts.use_suggested_xform = false;
    opts.world_xform = cd::math::scaling<float>(cd::math::Vec3f { 2.0F, 2.0F, 2.0F });

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src, opts);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;

    // Root entity's LocalTransform should reflect the user xform (scale=2).
    auto* lt = world.get<cd::scene::LocalTransform>(res.root_entity);
    ASSERT_NE(lt, nullptr);
    EXPECT_NEAR(lt->value.scale.x, 2.0F, 1.0E-4F);
    EXPECT_NEAR(lt->value.scale.y, 2.0F, 1.0E-4F);
    EXPECT_NEAR(lt->value.scale.z, 2.0F, 1.0E-4F);
}

TEST(SceneIngest, MultipleTexturesAllUploadInOrder)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    auto src = make_simple_scene();
    for (std::uint32_t i = 0U; i < 3U; ++i)
    {
        cd::asset::gltf::LoadedTexture t;
        t.image.width  = 2U;
        t.image.height = 2U;
        t.image.rgba.assign(std::size_t { 2 } * 2 * 4, static_cast<std::uint8_t>(i * 80U));
        src.textures.push_back(std::move(t));
    }

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;

    ASSERT_EQ(res.gpu_textures.size(), 3U);
    for (const auto& gt : res.gpu_textures)
    {
        EXPECT_TRUE(gt.image.is_valid());
        EXPECT_TRUE(gt.view.is_valid());
    }
}

TEST(SceneIngest, EmptyNodeListSkipsEcsButStillUploadsGpu)
{
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    cd::asset::gltf::LoadedScene src;
    // One mesh, no nodes.
    cd::asset::gltf::LoadedPrimitive prim {};
    prim.mesh.vertices = { { { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F } } };
    prim.mesh.indices  = { 0U };
    cd::asset::gltf::LoadedMesh m;
    m.primitives.push_back(std::move(prim));
    src.meshes.push_back(std::move(m));

    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;

    EXPECT_EQ(res.gpu_meshes.size(), 1U);
    EXPECT_TRUE(res.node_entities.empty());
}
