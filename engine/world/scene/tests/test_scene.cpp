// =============================================================================
// CHROMODYNAMIC — cd::scene tests
// =============================================================================
#include <cd/asset/json/Json.hpp>
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
    bool got_a = false;
    bool got_b = false;
    bool got_c = false;
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
    cd::asset::json::Value scalar { 42 };
    auto r = cd::scene::deserialize_scene(s, scalar);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::scene::serializer_errors::Code::kBadShape));
}

TEST(SceneSerializer, BadVersionRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset::json::Object o;
    o["version"] = cd::asset::json::Value { 99 };
    o["nodes"] = cd::asset::json::Value { cd::asset::json::Array {} };
    cd::asset::json::Value root { std::move(o) };
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
    f.near_ = {{ 0, 0, 1}, 1.0F};   // z >= -1
    f.far_  = {{ 0, 0,-1}, 1.0F};   // z <=  1
    return f;
}

} // namespace

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

TEST(Frustum, ContainsSphere_AtOriginIsAccepted)
{
    const auto f = make_unit_box_frustum();
    EXPECT_TRUE(cd::scene::contains_sphere(f, { 0.0F, 0.0F, 0.0F }, 0.5F));
}

TEST(Frustum, ContainsSphere_FarOutsideIsRejected)
{
    const auto f = make_unit_box_frustum();
    // Center at (10, 0, 0); plane "x <= 1" puts signed_distance(center) = -9.
    // With radius 0.5, -9 < -0.5 → reject.
    EXPECT_FALSE(cd::scene::contains_sphere(f, { 10.0F, 0.0F, 0.0F }, 0.5F));
}

TEST(Frustum, ContainsSphere_TouchingPlaneIsAccepted)
{
    const auto f = make_unit_box_frustum();
    // Center just outside +X plane by radius — touches inclusive boundary.
    EXPECT_TRUE(cd::scene::contains_sphere(f, { 1.5F, 0.0F, 0.0F }, 0.5F));
}

TEST(Frustum, ContainsSphere_PartialOverlapIsAccepted)
{
    const auto f = make_unit_box_frustum();
    // Center well outside one face but radius reaches in.
    EXPECT_TRUE(cd::scene::contains_sphere(f, { 1.8F, 0.0F, 0.0F }, 1.0F));
}

TEST(Frustum, FullyContainsSphere_StrictlyInsideOnly)
{
    const auto f = make_unit_box_frustum();
    // Small sphere fully inside.
    EXPECT_TRUE(cd::scene::fully_contains_sphere(f, { 0.0F, 0.0F, 0.0F }, 0.25F));
    // Sphere crossing the +X plane: partially in but NOT fully.
    EXPECT_FALSE(cd::scene::fully_contains_sphere(f, { 0.9F, 0.0F, 0.0F }, 0.25F));
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
    cd::math::Transformf a;
    cd::math::Transformf b;
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

#include <cd/scene/Skybox.hpp>

TEST(Skybox, DefaultHasNoCubemap)
{
    cd::scene::Skybox s;
    EXPECT_FALSE(cd::scene::has_cubemap(s));
    EXPECT_FLOAT_EQ(s.tint.x, 1.0F);
    EXPECT_FLOAT_EQ(s.intensity, 1.0F);
}

TEST(Skybox, HasCubemapAfterAssign)
{
    cd::scene::Skybox s;
    s.cubemap = cd::asset::AssetId::from_path("sky/dawn.cubemap");
    EXPECT_TRUE(cd::scene::has_cubemap(s));
}

TEST(Skybox, TintAndIntensityIndependent)
{
    cd::scene::Skybox s;
    s.tint = { 0.5F, 0.8F, 1.0F };
    s.intensity = 2.0F;
    s.rotation_y = 0.5F;
    EXPECT_FLOAT_EQ(s.intensity, 2.0F);
    EXPECT_FLOAT_EQ(s.rotation_y, 0.5F);
}

#include <cd/scene/Trigger.hpp>

TEST(Trigger, OnEnterFiresWhenEntering)
{
    cd::physics::Aabb v { { -1, -1, -1 }, { 1, 1, 1 } };
    cd::scene::Trigger t { "Zone", v };
    int enters = 0;
    t.set_on_enter([&](cd::ecs::Entity) { ++enters; });
    cd::ecs::Entity e { 1, 1 };
    t.update(e, { 5, 0, 0 });   // outside
    EXPECT_EQ(enters, 0);
    t.update(e, { 0, 0, 0 });   // inside
    EXPECT_EQ(enters, 1);
    t.update(e, { 0.5F, 0, 0 });   // still inside — no re-fire
    EXPECT_EQ(enters, 1);
}

TEST(Trigger, OnExitFiresWhenLeaving)
{
    cd::physics::Aabb v { { -1, -1, -1 }, { 1, 1, 1 } };
    cd::scene::Trigger t { "Zone", v };
    int exits = 0;
    t.set_on_exit([&](cd::ecs::Entity) { ++exits; });
    cd::ecs::Entity e { 1, 1 };
    t.update(e, { 0, 0, 0 });    // enter
    t.update(e, { 5, 0, 0 });    // exit
    EXPECT_EQ(exits, 1);
    EXPECT_FALSE(t.is_occupied());
}

TEST(Trigger, NeverEnteredDoesNotFireExit)
{
    cd::physics::Aabb v { { -1, -1, -1 }, { 1, 1, 1 } };
    cd::scene::Trigger t { "Zone", v };
    int exits = 0;
    t.set_on_exit([&](cd::ecs::Entity) { ++exits; });
    cd::ecs::Entity e { 1, 1 };
    t.update(e, { 5, 0, 0 });
    EXPECT_EQ(exits, 0);
}

#include <cd/scene/SceneStats.hpp>

TEST(SceneStats, DefaultZeroed)
{
    cd::scene::SceneStats s;
    EXPECT_EQ(s.entity_count, 0u);
    EXPECT_EQ(s.total_triangle_count, 0u);
}

TEST(SceneStats, BboxExtentComputed)
{
    cd::scene::SceneStats s;
    s.bbox_min = { -5.0F, 0.0F, -2.0F };
    s.bbox_max = {  5.0F, 4.0F,  6.0F };
    auto ex = cd::scene::bbox_extent(s);
    EXPECT_FLOAT_EQ(ex.x, 10.0F);
    EXPECT_FLOAT_EQ(ex.y, 4.0F);
    EXPECT_FLOAT_EQ(ex.z, 8.0F);
}

TEST(SceneStats, MemoryEstimateStorable)
{
    cd::scene::SceneStats s;
    s.estimated_bytes = 1024ULL * 1024ULL * 50ULL;   // 50 MB
    EXPECT_GT(s.estimated_bytes, 0u);
}

#include <cd/scene/TagBucket.hpp>

TEST(TagBucket, TagAddsToBucket)
{
    cd::scene::TagBucket b;
    cd::ecs::Entity e1 { 1, 1 };
    cd::ecs::Entity e2 { 2, 1 };
    b.tag(e1, "Enemy");
    b.tag(e2, "Enemy");
    EXPECT_EQ(b.entities_with("Enemy").size(), 2u);
}

TEST(TagBucket, DuplicateTagNoOp)
{
    cd::scene::TagBucket b;
    cd::ecs::Entity e { 1, 1 };
    b.tag(e, "X");
    b.tag(e, "X");
    EXPECT_EQ(b.entities_with("X").size(), 1u);
}

TEST(TagBucket, UntagRemoves)
{
    cd::scene::TagBucket b;
    cd::ecs::Entity e1 { 1, 1 };
    cd::ecs::Entity e2 { 2, 1 };
    b.tag(e1, "X");
    b.tag(e2, "X");
    b.untag(e1, "X");
    EXPECT_EQ(b.entities_with("X").size(), 1u);
    EXPECT_EQ(b.entities_with("X")[0], e2);
}

TEST(TagBucket, UnknownTagReturnsEmpty)
{
    cd::scene::TagBucket b;
    EXPECT_TRUE(b.entities_with("nope").empty());
}

// =============================================================================
// ≥80→100 marathon — edge / negative depth pass (ADD-ONLY).
// Every assumption below was verified against the actual header source:
//   * Aabb::contains is inclusive on BOTH bounds (>= min && <= max).
//   * JSON deser uses as_number() for id/version/parent; missing transform
//     fields default to identity; dangling parent silently stays root.
//   * Binary record layout: u32 id, u8 has_parent, [u32 parent], 40 B payload.
//   * Frustum tests inclusive (signed_distance >= 0 / >= -radius).
//   * LodSelector::select uses `distance < threshold` (exact == next LOD).
// =============================================================================

// ----- Scene graph: deeper hierarchy / dirty-flag / detach semantics -----

TEST(SceneDepth, EmptySceneUpdateTransformsIsNoOp)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    s.update_transforms();  // must not crash with zero roots
    EXPECT_EQ(w.alive_count(), 0u);
}

TEST(SceneDepth, RotationAndScaleComposeThroughHierarchy)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto child = s.create_node();
    ASSERT_TRUE(s.attach(child, root));

    // Root scales x2 uniformly; child sits at local +(1,0,0). Child world
    // position must be the parent scale applied to the child offset = (2,0,0).
    s.local(root)->value.scale = { 2.0F, 2.0F, 2.0F };
    s.local(child)->value.position = { 1.0F, 0.0F, 0.0F };
    s.update_transforms();
    const auto& wm = s.world_transform(child)->matrix;
    EXPECT_TRUE(approx_eq(wm[3][0], 2.0F));
    EXPECT_TRUE(approx_eq(wm[3][1], 0.0F));
    EXPECT_TRUE(approx_eq(wm[3][2], 0.0F));
}

TEST(SceneDepth, DetachMidTreeRecomputesPromotedSubtreeAsRoot)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto mid = s.create_node();
    auto leaf = s.create_node();
    ASSERT_TRUE(s.attach(mid, root));
    ASSERT_TRUE(s.attach(leaf, mid));
    s.local(root)->value.position = { 10.0F, 0.0F, 0.0F };
    s.local(mid)->value.position = { 1.0F, 0.0F, 0.0F };
    s.local(leaf)->value.position = { 1.0F, 0.0F, 0.0F };

    s.update_transforms();
    // Before detach: leaf world = 10 + 1 + 1 = 12.
    EXPECT_TRUE(approx_eq(s.world_transform(leaf)->matrix[3][0], 12.0F));

    // Promote `mid` (and its `leaf`) to root: parent contribution drops.
    s.detach(mid);
    EXPECT_FALSE(s.parent_of(mid).is_valid());
    s.update_transforms();
    // Now mid world = 1, leaf world = 1 + 1 = 2.
    EXPECT_TRUE(approx_eq(s.world_transform(mid)->matrix[3][0], 1.0F));
    EXPECT_TRUE(approx_eq(s.world_transform(leaf)->matrix[3][0], 2.0F));
}

TEST(SceneDepth, DeepChainPropagatesAccumulatedTranslation)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    constexpr int kDepth = 32;
    std::vector<cd::ecs::Entity> chain;
    chain.reserve(kDepth);
    cd::ecs::Entity prev {};
    for (int i = 0; i < kDepth; ++i)
    {
        auto e = s.create_node();
        s.local(e)->value.position = { 1.0F, 0.0F, 0.0F };
        if (prev.is_valid())
            ASSERT_TRUE(s.attach(e, prev));
        chain.push_back(e);
        prev = e;
    }
    s.update_transforms();
    // Leaf world x = sum of kDepth unit translations.
    EXPECT_TRUE(approx_eq(s.world_transform(chain.back())->matrix[3][0],
                          static_cast<float>(kDepth)));
}

TEST(SceneDepth, ForEachDescendantCountsAllNodesOfBalancedTree)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto a = s.create_node();
    auto b = s.create_node();
    auto a1 = s.create_node();
    auto a2 = s.create_node();
    ASSERT_TRUE(s.attach(a, root));
    ASSERT_TRUE(s.attach(b, root));
    ASSERT_TRUE(s.attach(a1, a));
    ASSERT_TRUE(s.attach(a2, a));
    std::size_t count = 0;
    s.for_each_descendant(root, [&](cd::ecs::Entity, std::uint32_t) { ++count; });
    EXPECT_EQ(count, 5u);  // root + 2 children + 2 grandchildren
}

TEST(SceneDepth, DetachWithoutParentIsNoOp)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    s.detach(e);  // no Parent component — must be a quiet no-op
    EXPECT_FALSE(s.parent_of(e).is_valid());
    EXPECT_TRUE(w.is_alive(e));
}

TEST(SceneDepth, DestroyDeadNodeIsNoOp)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    s.destroy_node(e);
    EXPECT_FALSE(w.is_alive(e));
    s.destroy_node(e);  // already dead — must not crash or double-free
    EXPECT_EQ(w.alive_count(), 0u);
}

// ----- JSON serializer: forward-compat / corrupt / defaulting paths -----

TEST(SceneSerializerEdge, NodeMissingIdRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset::json::Object node;  // no "id"
    node["translation"] = cd::scene::vec3_to_json({ 0, 0, 0 });
    cd::asset::json::Array nodes;
    nodes.emplace_back(std::move(node));
    cd::asset::json::Object root;
    root["version"] = cd::asset::json::Value { static_cast<int>(cd::scene::kSceneJsonVersion) };
    root["nodes"] = cd::asset::json::Value { std::move(nodes) };
    auto r = cd::scene::deserialize_scene(s, cd::asset::json::Value { std::move(root) });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::scene::serializer_errors::Code::kBadShape));
}

TEST(SceneSerializerEdge, NodesFieldNotArrayRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset::json::Object root;
    root["version"] = cd::asset::json::Value { static_cast<int>(cd::scene::kSceneJsonVersion) };
    root["nodes"] = cd::asset::json::Value { 7 };  // wrong type
    auto r = cd::scene::deserialize_scene(s, cd::asset::json::Value { std::move(root) });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::scene::serializer_errors::Code::kBadShape));
}

TEST(SceneSerializerEdge, MissingVersionRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset::json::Object root;
    root["nodes"] = cd::asset::json::Value { cd::asset::json::Array {} };
    auto r = cd::scene::deserialize_scene(s, cd::asset::json::Value { std::move(root) });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::scene::serializer_errors::Code::kBadShape));
}

TEST(SceneSerializerEdge, NodeNotObjectRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset::json::Array nodes;
    nodes.emplace_back(42);  // scalar, not object
    cd::asset::json::Object root;
    root["version"] = cd::asset::json::Value { static_cast<int>(cd::scene::kSceneJsonVersion) };
    root["nodes"] = cd::asset::json::Value { std::move(nodes) };
    auto r = cd::scene::deserialize_scene(s, cd::asset::json::Value { std::move(root) });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::scene::serializer_errors::Code::kBadShape));
}

TEST(SceneSerializerEdge, MalformedTranslationArrayRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset::json::Array bad_xlate;  // length 2, not 3
    bad_xlate.emplace_back(1.0);
    bad_xlate.emplace_back(2.0);
    cd::asset::json::Object node;
    node["id"] = cd::asset::json::Value { static_cast<std::int64_t>(1) };
    node["translation"] = cd::asset::json::Value { std::move(bad_xlate) };
    cd::asset::json::Array nodes;
    nodes.emplace_back(std::move(node));
    cd::asset::json::Object root;
    root["version"] = cd::asset::json::Value { static_cast<int>(cd::scene::kSceneJsonVersion) };
    root["nodes"] = cd::asset::json::Value { std::move(nodes) };
    auto r = cd::scene::deserialize_scene(s, cd::asset::json::Value { std::move(root) });
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::scene::serializer_errors::Code::kBadShape));
}

TEST(SceneSerializerEdge, MissingTransformFieldsDefaultToIdentity)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    // A node with only an id — every transform field omitted.
    cd::asset::json::Object node;
    node["id"] = cd::asset::json::Value { static_cast<std::int64_t>(5) };
    cd::asset::json::Array nodes;
    nodes.emplace_back(std::move(node));
    cd::asset::json::Object root;
    root["version"] = cd::asset::json::Value { static_cast<int>(cd::scene::kSceneJsonVersion) };
    root["nodes"] = cd::asset::json::Value { std::move(nodes) };
    auto r = cd::scene::deserialize_scene(s, cd::asset::json::Value { std::move(root) });
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->size(), 1u);
    const auto e = r->at(5);
    const auto& lt = s.local(e)->value;
    EXPECT_TRUE(approx_eq(lt.position.x, 0.0F));
    EXPECT_TRUE(approx_eq(lt.scale.x, 1.0F));   // identity scale, not zero
    EXPECT_TRUE(approx_eq(lt.rotation.w, 1.0F));
}

TEST(SceneSerializerEdge, DanglingParentReferenceLeavesNodeAsRoot)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::asset::json::Object node;
    node["id"] = cd::asset::json::Value { static_cast<std::int64_t>(1) };
    node["parent"] = cd::asset::json::Value { static_cast<std::int64_t>(999) };  // no such id
    cd::asset::json::Array nodes;
    nodes.emplace_back(std::move(node));
    cd::asset::json::Object root;
    root["version"] = cd::asset::json::Value { static_cast<int>(cd::scene::kSceneJsonVersion) };
    root["nodes"] = cd::asset::json::Value { std::move(nodes) };
    auto r = cd::scene::deserialize_scene(s, cd::asset::json::Value { std::move(root) });
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->size(), 1u);
    EXPECT_FALSE(s.parent_of(r->at(1)).is_valid());  // silently left a root
}

TEST(SceneSerializerEdge, ExtrasCallbacksRoundTripCustomField)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    s.local(e)->value.position = { 1.0F, 2.0F, 3.0F };

    const auto json = cd::scene::serialize_scene_with(
        s,
        [](cd::ecs::Entity, cd::asset::json::Object& obj)
        {
            obj["custom_tag"] = cd::asset::json::Value { std::string { "hero" } };
        });

    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    std::string seen_tag;
    auto r = cd::scene::deserialize_scene_with(
        s2, json,
        [&](cd::ecs::Entity, const cd::asset::json::Object& obj)
        {
            if (auto it = obj.find("custom_tag"); it != obj.end() && it->second.is_string())
                seen_tag = it->second.as_string();
        });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(seen_tag, "hero");
}

// ----- Binary serializer: corrupt / truncated / forward-compat paths -----

TEST(SceneBinaryEdge, NullPointerRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto r = cd::scene::deserialize_scene_binary(s, nullptr, 64);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::scene::bin_errors::Code::kCorrupt));
}

TEST(SceneBinaryEdge, BufferSmallerThanHeaderRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    std::vector<std::byte> tiny(cd::scene::kBinaryHeaderSize - 1, std::byte { 0 });
    auto r = cd::scene::deserialize_scene_binary(s, tiny.data(), tiny.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::scene::bin_errors::Code::kCorrupt));
}

TEST(SceneBinaryEdge, BadVersionRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    s.local(a)->value.position = { 1.0F, 0.0F, 0.0F };
    auto bytes = cd::scene::serialize_scene_binary(s);
    // Corrupt the version dword (bytes 4..7) to an unknown value.
    bytes[4] = std::byte { 0xFF };
    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::scene::bin_errors::Code::kVersionMismatch));
}

TEST(SceneBinaryEdge, TruncatedRecordRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    s.local(a)->value.position = { 1.0F, 2.0F, 3.0F };
    auto bytes = cd::scene::serialize_scene_binary(s);
    // Drop the trailing half of the single record — header still claims 1 node.
    ASSERT_GT(bytes.size(), cd::scene::kBinaryHeaderSize + 4);
    bytes.resize(cd::scene::kBinaryHeaderSize + 4);  // only the id survives
    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::scene::bin_errors::Code::kCorrupt));
}

TEST(SceneBinaryEdge, NodeCountClaimsMoreThanPresentRejected)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto bytes = cd::scene::serialize_scene_binary(s);  // empty scene = header only
    // Forge node_count (bytes 8..11) to 1 while the payload is empty.
    bytes[8] = std::byte { 1 };
    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::scene::bin_errors::Code::kCorrupt));
}

TEST(SceneBinaryEdge, ReservedFlagsIgnoredForwardCompat)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto a = s.create_node();
    s.local(a)->value.position = { 4.0F, 5.0F, 6.0F };
    auto bytes = cd::scene::serialize_scene_binary(s);
    // Set the reserved flags dword (bytes 12..15) — a forward-compatible
    // reader must ignore unknown flag bits and still decode the payload.
    bytes[12] = std::byte { 0xAB };
    bytes[15] = std::byte { 0xCD };
    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->size(), 1u);
    const auto e = r->at(a.id);
    EXPECT_TRUE(approx_eq(s2.local(e)->value.position.x, 4.0F));
}

TEST(SceneBinaryEdge, RootStaysRootAcrossRoundTrip)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto parent = s.create_node();
    auto child = s.create_node();
    s.attach(child, parent);
    auto bytes = cd::scene::serialize_scene_binary(s);

    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value());
    const auto p2 = r->at(parent.id);
    const auto c2 = r->at(child.id);
    EXPECT_FALSE(s2.parent_of(p2).is_valid());  // parent has no has_parent byte set
    EXPECT_EQ(s2.parent_of(c2), p2);            // child re-links to parent
}

TEST(SceneBinaryEdge, DeepHierarchyByteRoundTrip)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::ecs::Entity prev {};
    std::vector<cd::ecs::Entity> chain;
    for (int i = 0; i < 8; ++i)
    {
        auto e = s.create_node();
        s.local(e)->value.position = { static_cast<float>(i), 0.0F, 0.0F };
        if (prev.is_valid())
            s.attach(e, prev);
        chain.push_back(e);
        prev = e;
    }
    auto bytes = cd::scene::serialize_scene_binary(s);
    cd::ecs::World w2;
    cd::scene::Scene s2 { w2 };
    auto r = cd::scene::deserialize_scene_binary(s2, bytes.data(), bytes.size());
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(r->size(), 8u);
    // Verify the chain re-links: child 7's parent maps to original child 6.
    const auto leaf = r->at(chain[7].id);
    const auto mid = r->at(chain[6].id);
    EXPECT_EQ(s2.parent_of(leaf), mid);
}

// ----- Frustum: exact-plane and degenerate radii -----

TEST(FrustumEdge, AabbExactlyOnPlaneIsAccepted)
{
    const auto f = make_unit_box_frustum();
    // Box whose +X max face sits exactly on the x<=1 plane (p-vertex dist == 0).
    cd::physics::Aabb on_plane { { 0.0F, -0.5F, -0.5F }, { 1.0F, 0.5F, 0.5F } };
    EXPECT_TRUE(cd::scene::intersects(f, on_plane));
}

TEST(FrustumEdge, SphereExactlyAtNegativeRadiusBoundaryIsAccepted)
{
    const auto f = make_unit_box_frustum();
    // Center at x=1.5, radius 0.5 → signed_distance to x<=1 plane is -0.5,
    // exactly -radius → inclusive boundary accepts.
    EXPECT_TRUE(cd::scene::contains_sphere(f, { 1.5F, 0.0F, 0.0F }, 0.5F));
}

TEST(FrustumEdge, SphereJustBeyondNegativeRadiusIsRejected)
{
    const auto f = make_unit_box_frustum();
    EXPECT_FALSE(cd::scene::contains_sphere(f, { 1.5001F, 0.0F, 0.0F }, 0.5F));
}

TEST(FrustumEdge, ZeroRadiusSphereBehavesLikePoint)
{
    const auto f = make_unit_box_frustum();
    EXPECT_TRUE(cd::scene::contains_sphere(f, { 0.0F, 0.0F, 0.0F }, 0.0F));
    EXPECT_FALSE(cd::scene::contains_sphere(f, { 2.0F, 0.0F, 0.0F }, 0.0F));
}

TEST(FrustumEdge, FullyContainsSphereTouchingPlaneIsAccepted)
{
    const auto f = make_unit_box_frustum();
    // Center at origin, radius exactly 1 → touches every face from inside
    // (signed_distance == radius) → inclusive strict-containment accepts.
    EXPECT_TRUE(cd::scene::fully_contains_sphere(f, { 0.0F, 0.0F, 0.0F }, 1.0F));
    EXPECT_FALSE(cd::scene::fully_contains_sphere(f, { 0.0F, 0.0F, 0.0F }, 1.0001F));
}

// ----- SpatialHash: empty / dense / boundary / negative-space queries -----

TEST(SpatialHashEdge, QueryEmptyHashReturnsNothing)
{
    cd::scene::SpatialHash<int> sh { 4.0F };
    std::vector<int> out;
    sh.query_sphere({ 0, 0, 0 }, 10.0F, out);
    EXPECT_TRUE(out.empty());
    EXPECT_EQ(sh.cell_count(), 0u);
}

TEST(SpatialHashEdge, DenseSameCellReturnsAllItems)
{
    cd::scene::SpatialHash<int> sh { 10.0F };
    for (int i = 0; i < 100; ++i)
        sh.insert(i, cd::math::Vec3f { 1.0F, 1.0F, 1.0F });  // all in cell (0,0,0)
    EXPECT_EQ(sh.cell_count(), 1u);
    std::vector<int> out;
    sh.query_sphere({ 1.0F, 1.0F, 1.0F }, 0.5F, out);
    EXPECT_EQ(out.size(), 100u);
}

TEST(SpatialHashEdge, NegativeCoordinatesGetDistinctCells)
{
    cd::scene::SpatialHash<int> sh { 5.0F };
    sh.insert(1, cd::math::Vec3f { -1.0F, 0.0F, 0.0F });   // floor(-0.2) = -1 → cell -1
    sh.insert(2, cd::math::Vec3f { 1.0F, 0.0F, 0.0F });    // floor(0.2) = 0  → cell  0
    EXPECT_EQ(sh.cell_count(), 2u);
    std::vector<int> out;
    sh.query_sphere({ -1.0F, 0.0F, 0.0F }, 0.1F, out);
    bool saw_1 = false;
    bool saw_2 = false;
    for (int v : out)
    {
        if (v == 1) saw_1 = true;
        if (v == 2) saw_2 = true;
    }
    EXPECT_TRUE(saw_1);
    EXPECT_FALSE(saw_2);  // adjacent positive cell not touched by tiny radius
}

TEST(SpatialHashEdge, CellBoundaryFloorAssignment)
{
    cd::scene::SpatialHash<int> sh { 5.0F };
    // Exactly on a cell boundary: floor(5.0/5.0) = 1 → cell 1, not cell 0.
    sh.insert(1, cd::math::Vec3f { 5.0F, 0.0F, 0.0F });
    sh.insert(2, cd::math::Vec3f { 4.999F, 0.0F, 0.0F });  // cell 0
    EXPECT_EQ(sh.cell_count(), 2u);
}

TEST(SpatialHashEdge, NegativeRadiusClampedToZero)
{
    cd::scene::SpatialHash<int> sh { 5.0F };
    sh.insert(1, cd::math::Vec3f { 0.0F, 0.0F, 0.0F });
    std::vector<int> out;
    sh.query_sphere({ 0.0F, 0.0F, 0.0F }, -100.0F, out);  // must not blow up the loop
    // Negative radius clamps to 0 → still touches the center's own cell.
    bool saw_1 = false;
    for (int v : out) if (v == 1) saw_1 = true;
    EXPECT_TRUE(saw_1);
}

TEST(SpatialHashEdge, NonPositiveCellSizeFallsBackToUnit)
{
    cd::scene::SpatialHash<int> sh { -2.0F };  // invalid → clamped to 1.0
    EXPECT_FLOAT_EQ(sh.cell_size(), 1.0F);
}

// ----- Trigger: exact boundary + re-entry identity -----

TEST(TriggerEdge, ExactBoundaryPositionCountsAsInside)
{
    cd::physics::Aabb v { { -1, -1, -1 }, { 1, 1, 1 } };
    cd::scene::Trigger t { "Zone", v };
    int enters = 0;
    t.set_on_enter([&](cd::ecs::Entity) { ++enters; });
    cd::ecs::Entity e { 1, 1 };
    // Aabb::contains is inclusive — a point on max.x is inside.
    t.update(e, { 1.0F, 0.0F, 0.0F });
    EXPECT_EQ(enters, 1);
    EXPECT_TRUE(t.is_occupied());
}

TEST(TriggerEdge, ReEntryAfterExitFiresEnterAgain)
{
    cd::physics::Aabb v { { -1, -1, -1 }, { 1, 1, 1 } };
    cd::scene::Trigger t { "Zone", v };
    int enters = 0;
    int exits = 0;
    t.set_on_enter([&](cd::ecs::Entity) { ++enters; });
    t.set_on_exit([&](cd::ecs::Entity) { ++exits; });
    cd::ecs::Entity e { 7, 3 };
    t.update(e, { 0, 0, 0 });   // enter
    t.update(e, { 5, 0, 0 });   // exit
    t.update(e, { 0, 0, 0 });   // re-enter
    EXPECT_EQ(enters, 2);
    EXPECT_EQ(exits, 1);
    EXPECT_TRUE(t.is_occupied());
}

TEST(TriggerEdge, ExitCallbackReceivesOriginalOccupant)
{
    cd::physics::Aabb v { { -1, -1, -1 }, { 1, 1, 1 } };
    cd::scene::Trigger t { "Zone", v };
    cd::ecs::Entity reported {};
    t.set_on_exit([&](cd::ecs::Entity who) { reported = who; });
    cd::ecs::Entity e { 11, 5 };
    t.update(e, { 0, 0, 0 });   // enter — current_entity_ latches `e`
    t.update(e, { 5, 0, 0 });   // exit
    EXPECT_EQ(reported, e);
    EXPECT_FALSE(t.is_occupied());
    EXPECT_FALSE(t.occupant().is_valid());  // cleared after exit
}

// ----- LodSelector: degenerate / exact-threshold -----

TEST(LodSelectorEdge, EmptyThresholdsAlwaysSelectsLodZero)
{
    cd::scene::LodSelector s { {} };
    EXPECT_EQ(s.lod_count(), 1u);
    EXPECT_EQ(s.select(0.0F), 0u);
    EXPECT_EQ(s.select(1e9F), 0u);
}

TEST(LodSelectorEdge, ExactThresholdSelectsHigherLod)
{
    cd::scene::LodSelector s { { 10.0F } };
    // select uses `distance < threshold`, so exactly 10.0 falls to LOD1.
    EXPECT_EQ(s.select(9.999F), 0u);
    EXPECT_EQ(s.select(10.0F), 1u);
}

// ----- Layer / NameRegistry / Polyline degenerate cases -----

TEST(LayerRegistryEdge, DuplicateNamesGetDistinctIndices)
{
    cd::scene::LayerRegistry r;
    const auto a = r.add("Geo");
    const auto b = r.add("Geo");   // duplicate name allowed; distinct index
    EXPECT_NE(a, b);
    // find() returns the FIRST matching index by construction.
    EXPECT_EQ(r.find("Geo"), a);
}

TEST(NameRegistryEdge, FindMissingNameReturnsInvalid)
{
    cd::scene::NameRegistry r;
    EXPECT_FALSE(r.find("ghost").is_valid());
    EXPECT_TRUE(r.name_of(cd::ecs::Entity { 99, 1 }).empty());
}

TEST(Polyline3DEdge, SinglePointSampleReturnsThatPoint)
{
    cd::scene::Polyline3D p;
    p.add({ 3, 4, 5 });
    EXPECT_FLOAT_EQ(p.length(), 0.0F);
    const auto s = p.point_at(10.0F);  // arc-length past the lone vertex
    EXPECT_FLOAT_EQ(s.x, 3.0F);
    EXPECT_FLOAT_EQ(s.z, 5.0F);
}

TEST(Polyline3DEdge, ZeroLengthSegmentDoesNotDivideByZero)
{
    cd::scene::Polyline3D p;
    p.add({ 1, 1, 1 });
    p.add({ 1, 1, 1 });   // duplicate → zero-length segment
    p.add({ 2, 1, 1 });
    EXPECT_NEAR(p.length(), 1.0F, 1e-5F);
    const auto mid = p.point_at(0.5F);  // guarded by `seg > 0.0F`
    EXPECT_NEAR(mid.x, 1.5F, 1e-5F);
}
