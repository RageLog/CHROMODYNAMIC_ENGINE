// =============================================================================
// CHROMODYNAMIC — cd::scene tests
// =============================================================================
#include <cd/asset_json/Json.hpp>
#include <cd/ecs/World.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Transform.hpp>
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
