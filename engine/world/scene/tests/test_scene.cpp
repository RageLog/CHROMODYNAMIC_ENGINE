// =============================================================================
// CHROMODYNAMIC — cd::scene tests
// =============================================================================
#include <cd/asset_json/Json.hpp>
#include <cd/ecs/World.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Transform.hpp>
#include <cd/scene/EnvironmentLight.hpp>
#include <cd/scene/Scene.hpp>
#include <cd/scene/Serializer.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

namespace
{

constexpr float kEps = 1e-5F;

[[nodiscard]] bool approx_eq(float a, float b) noexcept
{
    return std::fabs(a - b) < kEps;
}

TEST(Scene, CreateNodeInstallsIdentityTransforms)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    EXPECT_TRUE(w.is_alive(e));
    ASSERT_NE(s.local(e), nullptr);
    ASSERT_NE(s.world_transform(e), nullptr);
    const auto id = cd::math::Mat4f::identity();
    for (std::size_t c = 0; c < 4; ++c)
    {
        for (std::size_t r = 0; r < 4; ++r)
        {
            EXPECT_TRUE(approx_eq(s.world_transform(e)->matrix[c][r], id[c][r]));
        }
    }
}

TEST(Scene, AttachAndDetachMaintainChildrenList)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto parent = s.create_node();
    auto child = s.create_node();
    ASSERT_TRUE(s.attach(child, parent));
    // Parent component on child.
    EXPECT_EQ(s.parent_of(child), parent);
    // Children list on parent.
    ASSERT_NE(w.get<cd::scene::Children>(parent), nullptr);
    EXPECT_EQ(w.get<cd::scene::Children>(parent)->entities.size(), 1U);

    s.detach(child);
    EXPECT_EQ(s.parent_of(child), cd::ecs::Entity {});
    // Empty Children list is cleaned up.
    EXPECT_EQ(w.get<cd::scene::Children>(parent), nullptr);
}

TEST(Scene, AttachToSelfRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    EXPECT_FALSE(s.attach(e, e));
}

TEST(Scene, ReparentReplacesOldLink)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    auto b = s.create_node();
    auto c = s.create_node();
    ASSERT_TRUE(s.attach(c, a));
    ASSERT_TRUE(s.attach(c, b));
    EXPECT_EQ(s.parent_of(c), b);
    EXPECT_EQ(w.get<cd::scene::Children>(a), nullptr);
    EXPECT_EQ(w.get<cd::scene::Children>(b)->entities.size(), 1U);
}

TEST(Scene, UpdateTransformsComposesParentChild)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto child = s.create_node();
    ASSERT_TRUE(s.attach(child, root));

    // Root translated to +(2, 0, 0). Child offset by +(1, 0, 0). Child's
    // world position must therefore be +(3, 0, 0).
    s.local(root)->value.position = { 2.0F, 0.0F, 0.0F };
    s.local(child)->value.position = { 1.0F, 0.0F, 0.0F };

    s.update_transforms();
    // Column-major: translation lives in column 3 (m[3]).
    const auto& wm = s.world_transform(child)->matrix;
    EXPECT_TRUE(approx_eq(wm[3][0], 3.0F));
    EXPECT_TRUE(approx_eq(wm[3][1], 0.0F));
    EXPECT_TRUE(approx_eq(wm[3][2], 0.0F));
}

TEST(Scene, UpdateTransformsThreeLevels)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    auto b = s.create_node();
    auto c = s.create_node();
    ASSERT_TRUE(s.attach(b, a));
    ASSERT_TRUE(s.attach(c, b));
    s.local(a)->value.position = { 1.0F, 0.0F, 0.0F };
    s.local(b)->value.position = { 0.0F, 1.0F, 0.0F };
    s.local(c)->value.position = { 0.0F, 0.0F, 1.0F };
    s.update_transforms();
    const auto& wm = s.world_transform(c)->matrix;
    EXPECT_TRUE(approx_eq(wm[3][0], 1.0F));
    EXPECT_TRUE(approx_eq(wm[3][1], 1.0F));
    EXPECT_TRUE(approx_eq(wm[3][2], 1.0F));
}

TEST(Scene, DestroySubtreeRemovesAllDescendants)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto mid = s.create_node();
    auto leaf = s.create_node();
    ASSERT_TRUE(s.attach(mid, root));
    ASSERT_TRUE(s.attach(leaf, mid));
    EXPECT_EQ(w.alive_count(), 3U);
    s.destroy_node(root);
    EXPECT_EQ(w.alive_count(), 0U);
}

TEST(Scene, DestroyMiddleNodeRemovesDescendantsOnly)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto mid = s.create_node();
    auto leaf = s.create_node();
    auto sibling = s.create_node();
    ASSERT_TRUE(s.attach(mid, root));
    ASSERT_TRUE(s.attach(leaf, mid));
    ASSERT_TRUE(s.attach(sibling, root));
    EXPECT_EQ(w.alive_count(), 4U);
    s.destroy_node(mid);  // takes leaf with it
    EXPECT_EQ(w.alive_count(), 2U);
    EXPECT_TRUE(w.is_alive(root));
    EXPECT_TRUE(w.is_alive(sibling));
    EXPECT_FALSE(w.is_alive(mid));
    EXPECT_FALSE(w.is_alive(leaf));
    // root's Children should no longer reference mid.
    ASSERT_NE(w.get<cd::scene::Children>(root), nullptr);
    EXPECT_EQ(w.get<cd::scene::Children>(root)->entities.size(), 1U);
    EXPECT_EQ(w.get<cd::scene::Children>(root)->entities[0], sibling);
}

TEST(Scene, AttachDeadEntityRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto parent = s.create_node();
    cd::ecs::Entity ghost {};
    EXPECT_FALSE(s.attach(ghost, parent));
    EXPECT_FALSE(s.attach(parent, ghost));
}

// ----- Enumeration helpers -----

TEST(Scene, ForEachNodeVisitsEveryCreatedEntity)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    auto b = s.create_node();
    auto c = s.create_node();
    std::size_t seen = 0;
    bool got_a = false, got_b = false, got_c = false;
    s.for_each_node(
        [&](cd::ecs::Entity e, cd::scene::LocalTransform&)
        {
            ++seen;
            if (e == a)
                got_a = true;
            if (e == b)
                got_b = true;
            if (e == c)
                got_c = true;
        }
    );
    EXPECT_EQ(seen, 3u);
    EXPECT_TRUE(got_a && got_b && got_c);
}

TEST(Scene, ForEachRootSkipsChildren)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto p = s.create_node();
    auto a = s.create_node();
    auto b = s.create_node();
    s.attach(a, p);
    s.attach(b, p);
    std::vector<cd::ecs::Entity> roots;
    s.for_each_root(
        [&](cd::ecs::Entity e, cd::scene::LocalTransform&)
        {
            roots.push_back(e);
        }
    );
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0], p);
}

TEST(Scene, ForEachDescendantWalksDepthFirst)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto r = s.create_node();
    auto c1 = s.create_node();
    auto c2 = s.create_node();
    auto g1 = s.create_node();
    s.attach(c1, r);
    s.attach(c2, r);
    s.attach(g1, c1);

    std::vector<std::pair<cd::ecs::Entity, std::uint32_t>> walk;
    s.for_each_descendant(
        r,
        [&](cd::ecs::Entity e, std::uint32_t d)
        {
            walk.emplace_back(e, d);
        }
    );
    ASSERT_EQ(walk.size(), 4u);
    EXPECT_EQ(walk[0].first, r);
    EXPECT_EQ(walk[0].second, 0u);
    // c1 visited before c2 (attach order), grandchild visited inside c1.
    EXPECT_EQ(walk[1].first, c1);
    EXPECT_EQ(walk[1].second, 1u);
    EXPECT_EQ(walk[2].first, g1);
    EXPECT_EQ(walk[2].second, 2u);
    EXPECT_EQ(walk[3].first, c2);
    EXPECT_EQ(walk[3].second, 1u);
}

// ----- Serializer (cd/scene/Serializer.hpp) -----

TEST(SceneSerializer, EmptySceneSerializesToVersionedShell)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    const auto j = cd::scene::serialize_scene(s);
    ASSERT_TRUE(j.is_object());
    const auto& obj = j.as_object();
    auto v = obj.find("version");
    ASSERT_NE(v, obj.end());
    EXPECT_EQ(static_cast<std::uint32_t>(v->second.as_number()), cd::scene::kSceneJsonVersion);
    auto n = obj.find("nodes");
    ASSERT_NE(n, obj.end());
    EXPECT_TRUE(n->second.is_array());
    EXPECT_EQ(n->second.as_array().size(), 0u);
}

TEST(SceneSerializer, RoundTripFlatScenePreservesTransforms)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };

    auto a = s.create_node();
    s.local(a)->value.position = cd::math::Vec3f { 1.0F, 2.0F, 3.0F };
    s.local(a)->value.scale = cd::math::Vec3f { 2.0F, 1.0F, 0.5F };

    auto b = s.create_node();
    s.local(b)->value.position = cd::math::Vec3f { -4.0F, 0.0F, 7.0F };

    const auto json = cd::scene::serialize_scene(s);

    // Now decode into a fresh world.
    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene(s2, json);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->size(), 2u);

    // Find the two restored nodes and verify their transforms.
    int seen = 0;
    w2.for_each<cd::scene::LocalTransform>(
        [&](cd::ecs::Entity, cd::scene::LocalTransform& lt)
        {
            const auto& p = lt.value.position;
            const auto& sv = lt.value.scale;
            if (approx_eq(p.x, 1.0F) && approx_eq(p.y, 2.0F) && approx_eq(p.z, 3.0F))
            {
                EXPECT_TRUE(approx_eq(sv.x, 2.0F));
                EXPECT_TRUE(approx_eq(sv.z, 0.5F));
                ++seen;
            }
            else if (approx_eq(p.x, -4.0F) && approx_eq(p.z, 7.0F))
            {
                ++seen;
            }
        }
    );
    EXPECT_EQ(seen, 2);
}

TEST(SceneSerializer, ParentLinksAreRebuilt)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto parent = s.create_node();
    auto child = s.create_node();
    s.attach(child, parent);

    const auto json = cd::scene::serialize_scene(s);

    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene(s2, json);
    ASSERT_TRUE(r.has_value());
    const auto& id_map = *r;
    ASSERT_EQ(id_map.size(), 2u);

    auto p2 = id_map.at(parent.id);
    auto c2 = id_map.at(child.id);
    EXPECT_EQ(s2.parent_of(c2), p2);
    EXPECT_FALSE(s2.parent_of(p2).is_valid());  // root preserved
}

TEST(SceneSerializer, BadShapeRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset_json::Value scalar { 42 };
    auto r = cd::scene::deserialize_scene(s, scalar);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::scene::serializer_errors::Code::kBadShape));
}

TEST(SceneSerializer, BadVersionRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset_json::Object o;
    o["version"] = cd::asset_json::Value { 99 };
    o["nodes"] = cd::asset_json::Value { cd::asset_json::Array {} };
    cd::asset_json::Value root { std::move(o) };
    auto r = cd::scene::deserialize_scene(s, root);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::scene::serializer_errors::Code::kBadVersion));
}

}  // namespace

// =============================================================================
// BinarySerializer (.cdscene) — Phase 5 / S4.7.b
// =============================================================================

#include <cd/scene/BinarySerializer.hpp>

TEST(SceneBinarySerializer, EmptySceneRoundTrip)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    const auto bytes = cd::scene::serialize_scene_binary(s);
    EXPECT_GE(bytes.size(), cd::scene::kBinaryHeaderSize);

    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 0u);
}

TEST(SceneBinarySerializer, FlatSceneRoundTripPreservesTransforms)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    s.local(a)->value.position = cd::math::Vec3f { 3.5F, -1.0F, 2.0F };
    s.local(a)->value.scale = cd::math::Vec3f { 1.5F, 0.5F, 2.0F };
    auto b = s.create_node();
    s.local(b)->value.position = cd::math::Vec3f { -4.0F, 0.0F, 7.0F };

    const auto bytes = cd::scene::serialize_scene_binary(s);

    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->size(), 2u);

    int seen = 0;
    w2.for_each<cd::scene::LocalTransform>([&](cd::ecs::Entity, cd::scene::LocalTransform& lt) {
        if (approx_eq(lt.value.position.x, 3.5F) && approx_eq(lt.value.scale.x, 1.5F))
            ++seen;
        else if (approx_eq(lt.value.position.x, -4.0F))
            ++seen;
    });
    EXPECT_EQ(seen, 2);
}

TEST(SceneBinarySerializer, ParentLinksAreRebuilt)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto parent = s.create_node();
    auto child = s.create_node();
    s.attach(child, parent);

    const auto bytes = cd::scene::serialize_scene_binary(s);

    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    const auto p2 = r->at(parent.id);
    const auto c2 = r->at(child.id);
    EXPECT_EQ(s2.parent_of(c2), p2);
    EXPECT_FALSE(s2.parent_of(p2).is_valid());
}

TEST(SceneBinarySerializer, BadMagicRejected)
{
    std::vector<std::byte> tiny(20, std::byte { 0 });
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto r = cd::scene::deserialize_scene_binary(s, tiny.data(), tiny.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::scene::bin_errors::Code::kMagicMismatch));
}

// ---------------------------------------------------------------------------
// Phase 19.E — ParticleSystem tests (Wave 180)
// ---------------------------------------------------------------------------
#include <cd/scene/ParticleSystem.hpp>

TEST(ParticleSystem, SpawnIncrementsLiveCount)
{
    cd::scene::ParticleSystem ps { 4 };
    EXPECT_EQ(ps.live_count(), 0u);
    EXPECT_TRUE(ps.spawn(cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 0, 1, 0 }, 1.0F));
    EXPECT_TRUE(ps.spawn(cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 0, 1, 0 }, 1.0F));
    EXPECT_EQ(ps.live_count(), 2u);
}

TEST(ParticleSystem, PoolExhaustedReturnsFalse)
{
    cd::scene::ParticleSystem ps { 2 };
    EXPECT_TRUE(ps.spawn(cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 0, 0, 0 }, 1.0F));
    EXPECT_TRUE(ps.spawn(cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 0, 0, 0 }, 1.0F));
    EXPECT_FALSE(ps.spawn(cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 0, 0, 0 }, 1.0F));
}

TEST(ParticleSystem, TickAdvancesPositionAndKillsExpired)
{
    cd::scene::ParticleSystem ps { 4 };
    ps.spawn(cd::math::Vec3f { 0, 0, 0 }, cd::math::Vec3f { 1, 0, 0 }, 1.0F);
    ps.tick(0.5F);
    EXPECT_EQ(ps.live_count(), 1u);
    // The pool allocates from the back of the free list, so the
    // first spawn lands at an arbitrary slot. Walk the pool and
    // assert the one alive particle has the expected position.
    bool found = false;
    for (const auto& p : ps.particles())
    {
        if (p.alive)
        {
            EXPECT_NEAR(p.position.x, 0.5F, 1e-5F);
            found = true;
        }
    }
    EXPECT_TRUE(found);
    ps.tick(0.6F);
    EXPECT_EQ(ps.live_count(), 0u);
}

// ---------------------------------------------------------------------------
// Phase 19.G — LodSelector tests
// ---------------------------------------------------------------------------
#include <cd/scene/LodSelector.hpp>

TEST(LodSelector, ThreeLodsTwoThresholds)
{
    cd::scene::LodSelector s { { 5.0F, 20.0F } };
    EXPECT_EQ(s.lod_count(), 3u);
    EXPECT_EQ(s.select(0.0F),  0u);
    EXPECT_EQ(s.select(4.99F), 0u);
    EXPECT_EQ(s.select(5.0F),  1u);
    EXPECT_EQ(s.select(19.0F), 1u);
    EXPECT_EQ(s.select(50.0F), 2u);
}

TEST(LodSelector, ForceOverridesDistance)
{
    cd::scene::LodSelector s { { 5.0F } };
    s.force(0);
    EXPECT_EQ(s.select(100.0F), 0u);
    s.unforce();
    EXPECT_EQ(s.select(100.0F), 1u);
}

// ---------------------------------------------------------------------------
// Phase 22.A — SpatialHash tests (Wave 186)
// ---------------------------------------------------------------------------
#include <cd/scene/SpatialHash.hpp>

TEST(SpatialHash, InsertAndQueryReturnsCandidate)
{
    cd::scene::SpatialHash<int> sh { 5.0F };
    sh.insert(1, cd::math::Vec3f { 0, 0, 0 });
    sh.insert(2, cd::math::Vec3f { 100, 100, 100 });
    std::vector<int> out;
    sh.query_sphere(cd::math::Vec3f { 0, 0, 0 }, 1.0F, out);
    // item 1 is in the queried cell range; item 2 is far away.
    bool saw_1 = false;
    for (int v : out) if (v == 1) saw_1 = true;
    EXPECT_TRUE(saw_1);
}

TEST(SpatialHash, ClearEmptiesEverything)
{
    cd::scene::SpatialHash<int> sh { 5.0F };
    sh.insert(1, cd::math::Vec3f { 0, 0, 0 });
    sh.insert(2, cd::math::Vec3f { 6, 0, 0 });
    EXPECT_GE(sh.cell_count(), 1u);
    sh.clear();
    EXPECT_EQ(sh.cell_count(), 0u);
}

#include <cd/scene/Frustum.hpp>
#include <cd/physics/Aabb.hpp>

namespace {

cd::scene::Frustum make_unit_box_frustum()
{
    // Six axis-aligned planes forming the box [-1,1]^3, normals pointing inward.
    cd::scene::Frustum f;
    f.left   = {{ 1, 0, 0}, 1.0F};   // x >= -1
    f.right  = {{-1, 0, 0}, 1.0F};   // x <=  1
    f.bottom = {{ 0, 1, 0}, 1.0F};   // y >= -1
    f.top    = {{ 0,-1, 0}, 1.0F};   // y <=  1
    f.near_  = {{ 0, 0, 1}, 1.0F};   // z >= -1
    f.far_   = {{ 0, 0,-1}, 1.0F};   // z <=  1
    return f;
}

}  // anonymous

TEST(Frustum, AabbInsideIsAccepted)
{
    const auto f = make_unit_box_frustum();
    cd::physics::Aabb inside { { -0.5F, -0.5F, -0.5F }, { 0.5F, 0.5F, 0.5F } };
    EXPECT_TRUE(cd::scene::intersects(f, inside));
}

TEST(Frustum, AabbOutsideIsRejected)
{
    const auto f = make_unit_box_frustum();
    cd::physics::Aabb outside { { 2.0F, 2.0F, 2.0F }, { 3.0F, 3.0F, 3.0F } };
    EXPECT_FALSE(cd::scene::intersects(f, outside));
}

TEST(Frustum, AabbStraddlingFaceIsAccepted)
{
    const auto f = make_unit_box_frustum();
    cd::physics::Aabb cross { { 0.5F, 0.5F, 0.5F }, { 1.5F, 1.5F, 1.5F } };
    EXPECT_TRUE(cd::scene::intersects(f, cross));
}

#include <cd/scene/VisibilityMask.hpp>

TEST(VisibilityMask, DefaultMaskIsAllOnes)
{
    cd::scene::VisibilityMask m;
    EXPECT_EQ(m.bits, cd::scene::kVisAll);
}

TEST(VisibilityMask, AllSeesEverything)
{
    cd::scene::VisibilityMask entity;       // kVisAll
    cd::scene::VisibilityMask camera;
    camera.bits = cd::scene::kVisShadowPass;
    EXPECT_TRUE(cd::scene::is_visible(entity, camera));
}

TEST(VisibilityMask, EditorGizmoHiddenFromMainCamera)
{
    cd::scene::VisibilityMask gizmo { cd::scene::kVisEditorGizmo };
    cd::scene::VisibilityMask main_cam { cd::scene::kVisMainCamera };
    EXPECT_FALSE(cd::scene::is_visible(gizmo, main_cam));
}

TEST(VisibilityMask, WithBitTurnsOnAndWithoutTurnsOff)
{
    cd::scene::VisibilityMask m { cd::scene::kVisMainCamera };
    auto added = cd::scene::with_bit(m, cd::scene::kVisShadowPass);
    EXPECT_NE(added.bits & cd::scene::kVisShadowPass, 0u);
    auto removed = cd::scene::without_bit(added, cd::scene::kVisShadowPass);
    EXPECT_EQ(removed.bits & cd::scene::kVisShadowPass, 0u);
}

#include <cd/scene/NameRegistry.hpp>

TEST(NameRegistry, BindAndFindByName)
{
    cd::scene::NameRegistry r;
    cd::ecs::Entity player { 1, 1 };
    r.bind(player, "Player");
    EXPECT_EQ(r.find("Player"), player);
    EXPECT_EQ(r.size(), 1u);
}

TEST(NameRegistry, NameOfRoundTrip)
{
    cd::scene::NameRegistry r;
    cd::ecs::Entity sun { 7, 2 };
    r.bind(sun, "Sun");
    EXPECT_EQ(r.name_of(sun), "Sun");
}

TEST(NameRegistry, UnbindRemoves)
{
    cd::scene::NameRegistry r;
    cd::ecs::Entity e { 3, 1 };
    r.bind(e, "Tmp");
    r.unbind(e);
    EXPECT_EQ(r.size(), 0u);
    EXPECT_FALSE(r.find("Tmp").is_valid());
}

TEST(NameRegistry, RebindOverwritesPrevious)
{
    cd::scene::NameRegistry r;
    cd::ecs::Entity e { 5, 1 };
    r.bind(e, "First");
    r.bind(e, "Second");
    EXPECT_EQ(r.size(), 1u);
    EXPECT_EQ(r.name_of(e), "Second");
    EXPECT_FALSE(r.find("First").is_valid());
}

TEST(NameRegistry, HashLookupMatchesStringLookup)
{
    cd::scene::NameRegistry r;
    cd::ecs::Entity e { 9, 1 };
    r.bind(e, "Cam");
    const auto h = cd::scene::name_hash("Cam");
    EXPECT_EQ(r.find_by_hash(h), e);
}

TEST(EnvironmentLight, DefaultHasNoIblButHasAmbient)
{
    cd::scene::EnvironmentLight e;
    EXPECT_FALSE(cd::scene::has_ibl(e));
    EXPECT_FLOAT_EQ(e.intensity, 1.0F);
    EXPECT_GT(e.ambient_rgb[2], 0.0F);
}

TEST(EnvironmentLight, HasIblAfterCubemapAssigned)
{
    cd::scene::EnvironmentLight e;
    e.cubemap = cd::asset::AssetId::from_path("env/sunset.cubemap");
    EXPECT_TRUE(cd::scene::has_ibl(e));
}

TEST(EnvironmentLight, IntensityScalable)
{
    cd::scene::EnvironmentLight e;
    e.intensity = 2.5F;
    EXPECT_FLOAT_EQ(e.intensity, 2.5F);
}

#include <cd/scene/HeightField.hpp>

TEST(HeightField, GetReturnsStoredValue)
{
    cd::scene::HeightField hf { 4, 4, 1.0F };
    hf.set(1, 2, 5.0F);
    EXPECT_FLOAT_EQ(hf.get(1, 2), 5.0F);
}

TEST(HeightField, OutOfBoundsGetReturnsZero)
{
    cd::scene::HeightField hf { 4, 4 };
    EXPECT_FLOAT_EQ(hf.get(99, 99), 0.0F);
}

TEST(HeightField, SampleBilinearMidpoint)
{
    cd::scene::HeightField hf { 2, 2, 1.0F };
    hf.set(0, 0, 0.0F);
    hf.set(1, 0, 1.0F);
    hf.set(0, 1, 2.0F);
    hf.set(1, 1, 3.0F);
    // sample at (0.5, 0.5) — average of all 4 = 1.5
    EXPECT_FLOAT_EQ(hf.sample(0.5F, 0.5F), 1.5F);
}

TEST(HeightField, SampleClampsOutOfBounds)
{
    cd::scene::HeightField hf { 2, 2, 1.0F };
    hf.set(1, 1, 5.0F);
    EXPECT_FLOAT_EQ(hf.sample(100.0F, 100.0F), 5.0F);
}

TEST(HeightField, CellSizeRespected)
{
    cd::scene::HeightField hf { 4, 4, 2.0F };   // 2 units per cell
    hf.set(2, 2, 10.0F);
    // world x = 2 units/cell * 2 = 4
    EXPECT_FLOAT_EQ(hf.sample(4.0F, 4.0F), 10.0F);
}

#include <cd/scene/Marker.hpp>

TEST(MarkerSet, AddAndFindByName)
{
    cd::scene::MarkerSet ms;
    cd::math::Transformf t;
    t.position = { 1, 2, 3 };
    ms.add("Spawn", t);
    const auto* m = ms.find("Spawn");
    ASSERT_NE(m, nullptr);
    EXPECT_FLOAT_EQ(m->transform.position.x, 1.0F);
    EXPECT_EQ(m->name, "Spawn");
}

TEST(MarkerSet, FindByHashRoundTrip)
{
    cd::scene::MarkerSet ms;
    cd::math::Transformf t;
    t.position = { 9, 8, 7 };
    ms.add("CamFocus", t);
    const auto h = cd::scene::name_hash("CamFocus");
    const auto* m = ms.find_by_hash(h);
    ASSERT_NE(m, nullptr);
    EXPECT_FLOAT_EQ(m->transform.position.z, 7.0F);
}

TEST(MarkerSet, RemoveDrops)
{
    cd::scene::MarkerSet ms;
    cd::math::Transformf t;
    ms.add("Tmp", t);
    ms.remove("Tmp");
    EXPECT_EQ(ms.size(), 0u);
    EXPECT_EQ(ms.find("Tmp"), nullptr);
}

TEST(MarkerSet, AddOverwritesSameName)
{
    cd::scene::MarkerSet ms;
    cd::math::Transformf a, b;
    a.position = { 1, 0, 0 };
    b.position = { 2, 0, 0 };
    ms.add("Slot", a);
    ms.add("Slot", b);
    EXPECT_EQ(ms.size(), 1u);
    EXPECT_FLOAT_EQ(ms.find("Slot")->transform.position.x, 2.0F);
}

#include <cd/scene/Layer.hpp>

TEST(LayerRegistry, AddReturnsIncrementingIndex)
{
    cd::scene::LayerRegistry r;
    EXPECT_EQ(r.add("Background"), 0);
    EXPECT_EQ(r.add("Geometry"), 1);
    EXPECT_EQ(r.add("UI"), 2);
    EXPECT_EQ(r.size(), 3u);
}

TEST(LayerRegistry, FindByNameReturnsIndex)
{
    cd::scene::LayerRegistry r;
    r.add("Background");
    r.add("UI");
    EXPECT_EQ(r.find("UI"), 1);
    EXPECT_EQ(r.find("Missing"), cd::scene::kInvalidLayer);
}

TEST(LayerRegistry, NameLookupReverse)
{
    cd::scene::LayerRegistry r;
    r.add("Foreground");
    EXPECT_EQ(r.name(0), "Foreground");
    EXPECT_EQ(r.name(99), "");
}

TEST(LayerRegistry, ClearEmpties)
{
    cd::scene::LayerRegistry r;
    r.add("a");
    r.add("b");
    r.clear();
    EXPECT_EQ(r.size(), 0u);
    EXPECT_EQ(r.find("a"), cd::scene::kInvalidLayer);
}

#include <cd/scene/LightProbe.hpp>

TEST(LightProbe, ZeroCoefficientsYieldZeroIrradiance)
{
    cd::scene::LightProbe p;
    auto r = cd::scene::evaluate(p, cd::math::Vec3f { 0, 1, 0 });
    EXPECT_FLOAT_EQ(r.x, 0.0F);
    EXPECT_FLOAT_EQ(r.y, 0.0F);
    EXPECT_FLOAT_EQ(r.z, 0.0F);
}

TEST(LightProbe, ConstantCoefficientReturnsConstant)
{
    // Only the constant (Y_0_0) term set → output = c * 0.282095 for
    // every direction.
    cd::scene::LightProbe p;
    p.coefficients[0] = { 1.0F, 2.0F, 3.0F };
    auto r1 = cd::scene::evaluate(p, cd::math::Vec3f { 1, 0, 0 });
    auto r2 = cd::scene::evaluate(p, cd::math::Vec3f { 0, 0, -1 });
    EXPECT_FLOAT_EQ(r1.x, r2.x);
    EXPECT_FLOAT_EQ(r1.y, r2.y);
    EXPECT_NEAR(r1.z, 3.0F * 0.282095F, 1e-5F);
}

TEST(LightProbe, FirstOrderDirectional)
{
    // Set Y_1_1 (x-direction) RGB green only → +x irradiates green,
    // -x irradiates negative green.
    cd::scene::LightProbe p;
    p.coefficients[3] = { 0.0F, 1.0F, 0.0F };
    auto rx = cd::scene::evaluate(p, cd::math::Vec3f { 1, 0, 0 });
    auto rn = cd::scene::evaluate(p, cd::math::Vec3f { -1, 0, 0 });
    EXPECT_GT(rx.y, 0.0F);
    EXPECT_LT(rn.y, 0.0F);
}

#include <cd/scene/Polyline3D.hpp>

TEST(Polyline3D, EmptyHasZeroLength)
{
    cd::scene::Polyline3D p;
    EXPECT_EQ(p.point_count(), 0u);
    EXPECT_FLOAT_EQ(p.length(), 0.0F);
}

TEST(Polyline3D, LengthOfStraightSegment)
{
    cd::scene::Polyline3D p;
    p.add({ 0, 0, 0 });
    p.add({ 3, 4, 0 });
    EXPECT_NEAR(p.length(), 5.0F, 1e-5F);   // 3-4-5 triangle
}

TEST(Polyline3D, PointAtMidIsHalf)
{
    cd::scene::Polyline3D p;
    p.add({ 0, 0, 0 });
    p.add({ 10, 0, 0 });
    auto mid = p.point_at(5.0F);
    EXPECT_FLOAT_EQ(mid.x, 5.0F);
}

TEST(Polyline3D, PointAtClampsAtEndpoints)
{
    cd::scene::Polyline3D p;
    p.add({ 0, 0, 0 });
    p.add({ 10, 0, 0 });
    auto before = p.point_at(-5.0F);
    auto after  = p.point_at(100.0F);
    EXPECT_FLOAT_EQ(before.x, 0.0F);
    EXPECT_FLOAT_EQ(after.x, 10.0F);
}

TEST(Polyline3D, ThreeSegmentArcLength)
{
    cd::scene::Polyline3D p;
    p.add({ 0, 0, 0 });
    p.add({ 1, 0, 0 });
    p.add({ 1, 1, 0 });
    p.add({ 1, 1, 1 });
    EXPECT_NEAR(p.length(), 3.0F, 1e-5F);
}

#include <cd/scene/Light.hpp>

TEST(Light, DefaultIsPointLight)
{
    cd::scene::Light l;
    EXPECT_EQ(l.kind, cd::scene::LightKind::kPoint);
    EXPECT_FLOAT_EQ(l.intensity, 1.0F);
}

TEST(Light, PointLightFactory)
{
    auto l = cd::scene::point_light({ 1, 2, 3 }, { 1, 0, 0 }, 5.0F, 20.0F);
    EXPECT_EQ(l.kind, cd::scene::LightKind::kPoint);
    EXPECT_FLOAT_EQ(l.position.x, 1.0F);
    EXPECT_FLOAT_EQ(l.color.x, 1.0F);
    EXPECT_FLOAT_EQ(l.intensity, 5.0F);
    EXPECT_FLOAT_EQ(l.range, 20.0F);
}

TEST(Light, DirectionalLightFactory)
{
    auto l = cd::scene::directional_light({ 0, -1, 0 }, { 1, 1, 0.8F });
    EXPECT_EQ(l.kind, cd::scene::LightKind::kDirectional);
    EXPECT_FLOAT_EQ(l.direction.y, -1.0F);
}

TEST(Light, SpotConeAnglesDefaults)
{
    cd::scene::Light l;
    l.kind = cd::scene::LightKind::kSpot;
    EXPECT_LT(l.inner_cone, l.outer_cone);
}
