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

#include <cstddef>
#include <cstdint>
#include <vector>

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

// ===========================================================================
// 80->100 depth pass (phase12xx): ADD-ONLY edge/negative coverage. No
// production change — existing valid-input output stays byte-identical. These
// exercise documented-but-untested paths: empty scene, single node, deep DFS
// hierarchy + world compose, AABB across nodes + empty-mesh node, rollback
// (GPU + prior-texture teardown + ECS-world-clean verification), out-of-range
// child tolerance, and multi-mesh / multi-primitive (instance) nodes.
// ===========================================================================

// ---- Edge: a fully empty LoadedScene still produces a valid synthetic root,
// no node entities, no GPU meshes, and zero (default) bounds. -----------------
TEST(SceneIngest, EmptyGltfSceneIngestsCleanly)
{
    // Arrange
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };
    const cd::asset::gltf::LoadedScene src;  // no meshes / nodes / textures

    // Act
    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);

    // Assert
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;
    EXPECT_TRUE(res.root_entity.is_valid());  // synthetic wrapper still created
    EXPECT_TRUE(res.node_entities.empty());
    EXPECT_TRUE(res.gpu_meshes.empty());
    EXPECT_TRUE(res.gpu_textures.empty());
    EXPECT_FLOAT_EQ(res.bounds_world_min.x, 0.0F);
    EXPECT_FLOAT_EQ(res.bounds_world_max.x, 0.0F);
    // Only the synthetic root exists in the ECS.
    EXPECT_EQ(world.component_count<cd::scene::LocalTransform>(), 1U);
}

// ---- Edge: a single node with no mesh creates exactly one node entity wired
// under the synthetic root. -----------------------------------------------------
TEST(SceneIngest, SingleNodeNoMeshCreatesOneEntityUnderRoot)
{
    // Arrange
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    cd::asset::gltf::LoadedScene src;
    cd::asset::gltf::LoadedNode solo;
    solo.name = "solo";  // no mesh_idx, no children
    src.nodes.push_back(std::move(solo));
    src.root_nodes = { 0U };

    // Act
    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);

    // Assert
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;
    ASSERT_EQ(res.node_entities.size(), 1U);
    EXPECT_TRUE(res.node_entities[0].is_valid());
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[0]), res.root_entity);
    EXPECT_TRUE(res.gpu_meshes.empty());
}

// ---- Edge: deep A->B->C chain. Verifies (a) DFS parent wiring, (b) DFS visit
// order via for_each_descendant, (c) world-transform composition through the
// hierarchy after update_transforms (each node translates +1 on X). -----------
TEST(SceneIngest, DeepTransformHierarchyDfsOrderAndWorldCompose)
{
    // Arrange — three nested nodes, each a local +1 X translation.
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    cd::asset::gltf::LoadedScene src;
    const cd::math::Mat4f step =
        cd::math::translation<float>(cd::math::Vec3f { 1.0F, 0.0F, 0.0F });

    cd::asset::gltf::LoadedNode a;
    a.name = "A";
    a.local_transform = step;
    a.children = { 1U };
    cd::asset::gltf::LoadedNode b;
    b.name = "B";
    b.local_transform = step;
    b.children = { 2U };
    cd::asset::gltf::LoadedNode c;
    c.name = "C";
    c.local_transform = step;
    src.nodes.push_back(std::move(a));
    src.nodes.push_back(std::move(b));
    src.nodes.push_back(std::move(c));
    src.root_nodes = { 0U };

    // Act
    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;
    ASSERT_EQ(res.node_entities.size(), 3U);

    // Assert — parent chain root -> A -> B -> C.
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[0]), res.root_entity);
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[1]), res.node_entities[0]);
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[2]), res.node_entities[1]);

    // Assert — DFS visit order from the synthetic root is root, A, B, C.
    std::vector<cd::ecs::Entity> visited;
    scene_tree.for_each_descendant(
        res.root_entity,
        [&](cd::ecs::Entity e, std::uint32_t /*depth*/) { visited.push_back(e); });
    ASSERT_EQ(visited.size(), 4U);
    EXPECT_EQ(visited[0], res.root_entity);
    EXPECT_EQ(visited[1], res.node_entities[0]);
    EXPECT_EQ(visited[2], res.node_entities[1]);
    EXPECT_EQ(visited[3], res.node_entities[2]);

    // Assert — composed world translation accumulates down the chain.
    // ingest already called update_transforms(); column 3 holds translation.
    const auto* wa = scene_tree.world_transform(res.node_entities[0]);
    const auto* wb = scene_tree.world_transform(res.node_entities[1]);
    const auto* wc = scene_tree.world_transform(res.node_entities[2]);
    ASSERT_NE(wa, nullptr);
    ASSERT_NE(wb, nullptr);
    ASSERT_NE(wc, nullptr);
    EXPECT_NEAR(wa->matrix[3][0], 1.0F, 1.0E-4F);
    EXPECT_NEAR(wb->matrix[3][0], 2.0F, 1.0E-4F);
    EXPECT_NEAR(wc->matrix[3][0], 3.0F, 1.0E-4F);
}

// ---- Edge: AABB pass-through is unaffected by a node that references no mesh
// (mesh_idx == nullopt) sitting between mesh-bearing nodes. --------------------
TEST(SceneIngest, AabbPassThroughAcrossNodesWithEmptyMeshNode)
{
    // Arrange — base scene (1 mesh) + an extra mesh-less node in the middle.
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    auto src = make_simple_scene();
    // Insert a fourth, mesh-less node and route it as a child of A.
    cd::asset::gltf::LoadedNode empty_node;
    empty_node.name = "empty";  // no mesh_idx
    src.nodes.push_back(std::move(empty_node));
    src.nodes[0].children.push_back(3U);  // A now parents B, C and the empty node
    // Give the source distinct bounds we can assert flow through untouched.
    src.bounds_world_min = { -2.0F, -3.0F, -4.0F };
    src.bounds_world_max = {  5.0F,  6.0F,  7.0F };

    // Act
    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);

    // Assert
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;
    ASSERT_EQ(res.node_entities.size(), 4U);
    EXPECT_TRUE(res.node_entities[3].is_valid());
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[3]), res.node_entities[0]);
    // Bounds are an exact pass-through of the LoadedScene's AABB.
    EXPECT_FLOAT_EQ(res.bounds_world_min.x, -2.0F);
    EXPECT_FLOAT_EQ(res.bounds_world_min.y, -3.0F);
    EXPECT_FLOAT_EQ(res.bounds_world_min.z, -4.0F);
    EXPECT_FLOAT_EQ(res.bounds_world_max.x, 5.0F);
    EXPECT_FLOAT_EQ(res.bounds_world_max.y, 6.0F);
    EXPECT_FLOAT_EQ(res.bounds_world_max.z, 7.0F);
}

// ---- Negative: mid-ingest mesh upload failure must roll back every GPU
// resource AND leave the ECS world completely clean. A primitive with empty
// vertices makes upload_mesh request a zero-size vertex buffer, which
// NullDevice rejects -> upload fails -> rollback. The first (valid) mesh's
// buffers must be freed, and no node entities may leak. ----------------------
TEST(SceneIngest, RollbackOnMidIngestMeshFailureLeavesWorldAndDeviceClean)
{
    // Arrange — mesh 0 is valid, mesh 1 has an empty-vertex primitive.
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    auto src = make_simple_scene();  // mesh 0 valid (3 verts / 3 indices)

    cd::asset::gltf::LoadedPrimitive bad {};
    // No vertices -> zero-size vertex buffer -> create_buffer rejects.
    bad.mesh.indices = { 0U };
    cd::asset::gltf::LoadedMesh bad_mesh;
    bad_mesh.primitives.push_back(std::move(bad));
    src.meshes.push_back(std::move(bad_mesh));

    const std::size_t buffers_before = dev.live_buffer_count();
    const std::size_t alive_before   = world.alive_count();

    // Act
    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);

    // Assert — failure surfaced, not a silent partial scene.
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, 2U);  // mesh upload failure domain code

    // GPU clean: the valid mesh's vb+ib were destroyed by rollback_gpu.
    EXPECT_EQ(dev.live_buffer_count(), buffers_before);
    // ECS clean: entity creation (step 3) never runs because mesh upload
    // (step 2) fails first, so the world is untouched.
    EXPECT_EQ(world.alive_count(), alive_before);
    EXPECT_EQ(world.component_count<cd::scene::LocalTransform>(), 0U);
}

// ---- Negative: a mesh failure AFTER successful texture uploads must also
// free those prior textures (rollback_gpu tears down meshes + textures). -----
TEST(SceneIngest, RollbackOnMeshFailureFreesPriorTextures)
{
    // Arrange — one valid texture, then a mesh with an empty-vertex primitive.
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    cd::asset::gltf::LoadedScene src;
    cd::asset::gltf::LoadedTexture tex;
    tex.image.width  = 2U;
    tex.image.height = 2U;
    tex.image.rgba.assign(std::size_t { 2 } * 2 * 4, std::uint8_t { 200 });
    src.textures.push_back(std::move(tex));

    cd::asset::gltf::LoadedPrimitive bad {};
    bad.mesh.indices = { 0U };  // empty vertices -> mesh upload fails
    cd::asset::gltf::LoadedMesh bad_mesh;
    bad_mesh.primitives.push_back(std::move(bad));
    src.meshes.push_back(std::move(bad_mesh));

    const std::size_t textures_before = dev.live_texture_count();

    // Act
    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);

    // Assert — failed, and the texture allocated before the mesh step is gone.
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, 2U);
    EXPECT_EQ(dev.live_texture_count(), textures_before);
}

// ---- Negative: an out-of-range child index is tolerated (silently ignored),
// the ingest still succeeds and the in-range siblings stay correctly wired. ---
TEST(SceneIngest, OutOfRangeChildIndexIsTolerated)
{
    // Arrange — node A lists child 1 (valid) and child 99 (out of range).
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    cd::asset::gltf::LoadedScene src;
    cd::asset::gltf::LoadedNode a;
    a.name = "A";
    a.children = { 1U, 99U };  // 99 has no backing node
    cd::asset::gltf::LoadedNode b;
    b.name = "B";
    src.nodes.push_back(std::move(a));
    src.nodes.push_back(std::move(b));
    src.root_nodes = { 0U };

    // Act
    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);

    // Assert — no crash, no error; valid child wired, bogus index dropped.
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;
    ASSERT_EQ(res.node_entities.size(), 2U);
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[1]), res.node_entities[0]);
}

// ---- Edge: multi-mesh scene where one mesh has multiple primitives (the
// instance / multi-mesh node case). gpu_meshes must mirror the source shape:
// one per-prim vector per mesh, sized to that mesh's primitive count. --------
TEST(SceneIngest, MultiMeshMultiPrimitiveNodeUploadsAllPrimitives)
{
    // Arrange — mesh 0 has 1 primitive, mesh 1 has 2 primitives.
    cd::rhi::NullDevice dev;
    cd::ecs::World world;
    cd::scene::Scene scene_tree { world };

    cd::asset::gltf::LoadedScene src;

    const auto make_prim = [] {
        cd::asset::gltf::LoadedPrimitive p {};
        p.mesh.vertices = {
            { { 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F } },
            { { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 1.0F, 0.0F } },
            { { 0.0F, 1.0F, 0.0F }, { 0.0F, 1.0F, 0.0F }, { 0.5F, 1.0F } },
        };
        p.mesh.indices = { 0U, 1U, 2U };
        return p;
    };

    cd::asset::gltf::LoadedMesh m0;
    m0.primitives.push_back(make_prim());
    cd::asset::gltf::LoadedMesh m1;
    m1.primitives.push_back(make_prim());
    m1.primitives.push_back(make_prim());
    src.meshes.push_back(std::move(m0));
    src.meshes.push_back(std::move(m1));

    // Two nodes referencing the two meshes (multi-mesh instance layout).
    cd::asset::gltf::LoadedNode n0;
    n0.mesh_idx = 0U;
    cd::asset::gltf::LoadedNode n1;
    n1.mesh_idx = 1U;
    src.nodes.push_back(std::move(n0));
    src.nodes.push_back(std::move(n1));
    src.root_nodes = { 0U, 1U };

    // Act
    auto r = cd::render::scene::ingest_gltf_scene(dev, world, scene_tree, src);

    // Assert — gpu_meshes shape mirrors the source mesh/primitive layout.
    ASSERT_TRUE(r.has_value());
    const auto& res = *r;
    ASSERT_EQ(res.gpu_meshes.size(), 2U);
    ASSERT_EQ(res.gpu_meshes[0].size(), 1U);
    ASSERT_EQ(res.gpu_meshes[1].size(), 2U);
    for (const auto& mesh_set : res.gpu_meshes)
    {
        for (const auto& gm : mesh_set)
        {
            EXPECT_TRUE(gm.vb.is_valid());
            EXPECT_TRUE(gm.ib.is_valid());
            EXPECT_EQ(gm.vertex_count, 3U);
            EXPECT_EQ(gm.index_count, 3U);
        }
    }
    // Both root nodes attach under the synthetic wrapper.
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[0]), res.root_entity);
    EXPECT_EQ(scene_tree.parent_of(res.node_entities[1]), res.root_entity);
}
