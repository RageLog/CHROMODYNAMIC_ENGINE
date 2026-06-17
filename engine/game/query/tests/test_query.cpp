// =============================================================================
// CHROMODYNAMIC — tests/test_query.cpp
// Phase 475 — cd::game::query unit tests.
//
// Validates:
//   1) intersect_ray_aabb hits a centred box at the expected `t`/normal.
//   2) intersect_ray_aabb misses when the ray points the other way.
//   3) QueryWorld::raycast returns the nearest hit out of several.
//   4) QueryWorld::raycast returns nullopt for an empty world / zero dir.
//   5) QueryWorld::sphere_query enumerates and sorts nearest-first.
//   6) QueryWorld::sphere_query honours a zero-radius query (centre point).
//   7) QueryWorld::box_query returns overlapping entities only.
//   8) QueryWorld::frustum_query keeps in-frustum boxes, drops outsiders.
//   9) rebuild_from + add_entity round-trips entity counts correctly.
//  10) remove_entity drops the right record without disturbing siblings.
// =============================================================================
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>
#include <cd/game/query/Query.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>
#include <cd/scene/Frustum.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{

using cd::ecs::Entity;
using cd::ecs::World;
using cd::game::query::QueryWorld;
using cd::game::query::intersect_ray_aabb;
using cd::math::Vec3f;
using cd::physics::Aabb;
using cd::scene::Frustum;
using cd::scene::Plane;

// Convenience: AABB centred at `c` with half-extent `h`.
[[nodiscard]] Aabb box_at(const Vec3f& c, float h)
{
    return Aabb { Vec3f { c.x - h, c.y - h, c.z - h },
                  Vec3f { c.x + h, c.y + h, c.z + h } };
}

// Build a trivial axis-aligned frustum: a box-shaped "frustum" spanning
// x in [-10, 10], y in [-10, 10], z in [-10, 10], with inward normals.
[[nodiscard]] Frustum box_frustum(float lo, float hi)
{
    Frustum f;
    // Each plane: inward normal `n`, distance `d` such that `n.p + d >= 0`
    // when `p` is on the inside half-space.
    f.left   = Plane { Vec3f { 1.0F, 0.0F, 0.0F }, -lo };
    f.right  = Plane { Vec3f { -1.0F, 0.0F, 0.0F }, hi };
    f.bottom = Plane { Vec3f { 0.0F, 1.0F, 0.0F }, -lo };
    f.top    = Plane { Vec3f { 0.0F, -1.0F, 0.0F }, hi };
    f.near_  = Plane { Vec3f { 0.0F, 0.0F, 1.0F }, -lo };
    f.far_   = Plane { Vec3f { 0.0F, 0.0F, -1.0F }, hi };
    return f;
}

// -----------------------------------------------------------------------------
// 1) intersect_ray_aabb hits a centred box and reports correct t / normal.
// -----------------------------------------------------------------------------
TEST(GameQuery, RayHitsCenteredBox)
{
    const Aabb box = box_at(Vec3f { 5.0F, 0.0F, 0.0F }, 1.0F);
    auto hit = intersect_ray_aabb(Vec3f { 0.0F, 0.0F, 0.0F },
                                  Vec3f { 1.0F, 0.0F, 0.0F }, box,
                                  /*max_dist=*/100.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->t, 4.0F, 1e-5F);
    EXPECT_NEAR(hit->normal.x, -1.0F, 1e-5F);
    EXPECT_NEAR(hit->normal.y, 0.0F, 1e-5F);
    EXPECT_NEAR(hit->normal.z, 0.0F, 1e-5F);
}

// -----------------------------------------------------------------------------
// 2) intersect_ray_aabb misses when the ray points the other way.
// -----------------------------------------------------------------------------
TEST(GameQuery, RayMissesWhenPointingAway)
{
    const Aabb box = box_at(Vec3f { 5.0F, 0.0F, 0.0F }, 1.0F);
    auto hit = intersect_ray_aabb(Vec3f { 0.0F, 0.0F, 0.0F },
                                  Vec3f { -1.0F, 0.0F, 0.0F }, box, 100.0F);
    EXPECT_FALSE(hit.has_value());

    // Also miss if max_dist falls short of the box.
    auto short_hit = intersect_ray_aabb(Vec3f { 0.0F, 0.0F, 0.0F },
                                        Vec3f { 1.0F, 0.0F, 0.0F }, box, 1.0F);
    EXPECT_FALSE(short_hit.has_value());
}

// -----------------------------------------------------------------------------
// 3) QueryWorld::raycast returns the nearest hit out of several.
// -----------------------------------------------------------------------------
TEST(GameQuery, RaycastPicksNearestHit)
{
    World w;
    Entity e_far  = w.create();
    Entity e_near = w.create();
    Entity e_off  = w.create();

    QueryWorld q { /*cell_size=*/4.0F };
    q.add_entity(e_far,  Vec3f { 20.0F, 0.0F, 0.0F }, box_at(Vec3f { 20.0F, 0.0F, 0.0F }, 1.0F));
    q.add_entity(e_near, Vec3f {  5.0F, 0.0F, 0.0F }, box_at(Vec3f {  5.0F, 0.0F, 0.0F }, 1.0F));
    q.add_entity(e_off,  Vec3f { 10.0F, 8.0F, 0.0F }, box_at(Vec3f { 10.0F, 8.0F, 0.0F }, 1.0F));
    q.rebuild();

    auto hit = q.raycast(Vec3f { 0.0F, 0.0F, 0.0F },
                         Vec3f { 1.0F, 0.0F, 0.0F }, 100.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, e_near);
    EXPECT_NEAR(hit->distance, 4.0F, 1e-4F);
    EXPECT_NEAR(hit->point.x, 4.0F, 1e-4F);
    EXPECT_NEAR(hit->normal.x, -1.0F, 1e-5F);
}

// -----------------------------------------------------------------------------
// 4) Raycast returns nullopt for an empty world OR a zero-length direction.
// -----------------------------------------------------------------------------
TEST(GameQuery, RaycastNulloptForEmptyOrZeroDir)
{
    QueryWorld q;
    EXPECT_FALSE(q.raycast(Vec3f { 0, 0, 0 }, Vec3f { 1, 0, 0 }, 100.0F).has_value());

    World w;
    Entity e = w.create();
    q.add_entity(e, Vec3f { 5, 0, 0 }, box_at(Vec3f { 5, 0, 0 }, 1.0F));
    q.rebuild();
    EXPECT_FALSE(q.raycast(Vec3f { 0, 0, 0 }, Vec3f { 0, 0, 0 }, 100.0F).has_value());
    EXPECT_FALSE(q.raycast(Vec3f { 0, 0, 0 }, Vec3f { 1, 0, 0 }, 0.0F).has_value());
}

// -----------------------------------------------------------------------------
// 5) sphere_query enumerates overlaps and sorts nearest-first.
// -----------------------------------------------------------------------------
TEST(GameQuery, SphereQuerySortsNearestFirst)
{
    World w;
    Entity a = w.create();
    Entity b = w.create();
    Entity c = w.create();

    QueryWorld q { 2.0F };
    q.add_entity(a, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.5F));
    q.add_entity(b, Vec3f { 3.0F, 0.0F, 0.0F }, box_at(Vec3f { 3.0F, 0.0F, 0.0F }, 0.5F));
    q.add_entity(c, Vec3f { 1.0F, 0.0F, 0.0F }, box_at(Vec3f { 1.0F, 0.0F, 0.0F }, 0.5F));
    q.rebuild();

    auto hits = q.sphere_query(Vec3f { 0, 0, 0 }, /*radius=*/5.0F);
    ASSERT_EQ(hits.size(), 3U);
    EXPECT_EQ(hits[0].entity, a);
    EXPECT_EQ(hits[1].entity, c);
    EXPECT_EQ(hits[2].entity, b);
    EXPECT_LT(hits[0].distance, hits[1].distance);
    EXPECT_LT(hits[1].distance, hits[2].distance);
}

// -----------------------------------------------------------------------------
// 6) Zero-radius sphere query returns the entity at the exact centre.
// -----------------------------------------------------------------------------
TEST(GameQuery, SphereQueryZeroRadius)
{
    World w;
    Entity a = w.create();
    Entity b = w.create();
    QueryWorld q { 2.0F };
    q.add_entity(a, Vec3f { 1.0F, 1.0F, 1.0F }, box_at(Vec3f { 1.0F, 1.0F, 1.0F }, 0.25F));
    q.add_entity(b, Vec3f { 5.0F, 5.0F, 5.0F }, box_at(Vec3f { 5.0F, 5.0F, 5.0F }, 0.25F));
    q.rebuild();

    auto hits = q.sphere_query(Vec3f { 1.0F, 1.0F, 1.0F }, 0.0F);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].entity, a);
    EXPECT_NEAR(hits[0].distance, 0.0F, 1e-5F);
}

// -----------------------------------------------------------------------------
// 7) box_query returns overlapping entities and only those.
// -----------------------------------------------------------------------------
TEST(GameQuery, BoxQueryReturnsOverlappingOnly)
{
    World w;
    Entity inside  = w.create();
    Entity outside = w.create();
    Entity touch   = w.create();

    QueryWorld q { 2.0F };
    q.add_entity(inside,  Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.5F));
    q.add_entity(outside, Vec3f { 10.0F, 0.0F, 0.0F }, box_at(Vec3f { 10.0F, 0.0F, 0.0F }, 0.5F));
    q.add_entity(touch,   Vec3f { 2.0F, 0.0F, 0.0F }, box_at(Vec3f { 2.0F, 0.0F, 0.0F }, 0.5F));
    q.rebuild();

    const Aabb query_box = box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 1.5F);
    auto hits = q.box_query(query_box);

    // `inside` (centre 0) and `touch` (face touching at x=1.5) must be in.
    // `outside` at x=10 must not.
    ASSERT_EQ(hits.size(), 2U);
    bool saw_inside = false;
    bool saw_touch = false;
    bool saw_outside = false;
    for (auto e : hits)
    {
        if (e == inside)  saw_inside  = true;
        if (e == touch)   saw_touch   = true;
        if (e == outside) saw_outside = true;
    }
    EXPECT_TRUE(saw_inside);
    EXPECT_TRUE(saw_touch);
    EXPECT_FALSE(saw_outside);
}

// -----------------------------------------------------------------------------
// 8) frustum_query keeps in-frustum entities, drops outsiders.
// -----------------------------------------------------------------------------
TEST(GameQuery, FrustumQueryKeepsInsideDropsOutside)
{
    World w;
    Entity in_a = w.create();
    Entity in_b = w.create();
    Entity out  = w.create();

    QueryWorld q { 4.0F };
    q.add_entity(in_a, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 1.0F));
    q.add_entity(in_b, Vec3f { 5.0F, 2.0F, -3.0F }, box_at(Vec3f { 5.0F, 2.0F, -3.0F }, 0.5F));
    q.add_entity(out,  Vec3f { 50.0F, 0.0F, 0.0F }, box_at(Vec3f { 50.0F, 0.0F, 0.0F }, 1.0F));
    q.rebuild();

    const Frustum f = box_frustum(-10.0F, 10.0F);
    auto hits = q.frustum_query(f);

    ASSERT_EQ(hits.size(), 2U);
    bool saw_a = false;
    bool saw_b = false;
    bool saw_out = false;
    for (auto e : hits)
    {
        if (e == in_a) saw_a = true;
        if (e == in_b) saw_b = true;
        if (e == out)  saw_out = true;
    }
    EXPECT_TRUE(saw_a);
    EXPECT_TRUE(saw_b);
    EXPECT_FALSE(saw_out);
}

// -----------------------------------------------------------------------------
// 9) rebuild_from + add_entity round-trips entity counts correctly.
// -----------------------------------------------------------------------------
TEST(GameQuery, RebuildFromCallbackPopulates)
{
    World w;
    std::vector<Entity> es;
    es.reserve(5);
for (int i = 0; i < 5; ++i)
        es.push_back(w.create());

    QueryWorld q { 2.0F };
    q.rebuild_from(
        [&](QueryWorld& target)
        {
            for (std::size_t i = 0; i < es.size(); ++i)
            {
                const auto fi = static_cast<float>(i);
                target.add_entity(es[i], Vec3f { fi, 0.0F, 0.0F },
                                  box_at(Vec3f { fi, 0.0F, 0.0F }, 0.25F));
            }
        });

    EXPECT_EQ(q.entity_count(), 5U);

    // And a follow-up sphere query sees all five.
    auto hits = q.sphere_query(Vec3f { 2.0F, 0.0F, 0.0F }, 10.0F);
    EXPECT_EQ(hits.size(), 5U);
}

// -----------------------------------------------------------------------------
// 10) remove_entity drops the right record without disturbing siblings.
// -----------------------------------------------------------------------------
TEST(GameQuery, RemoveEntityIsSurgical)
{
    World w;
    Entity a = w.create();
    Entity b = w.create();
    Entity c = w.create();

    QueryWorld q { 2.0F };
    q.add_entity(a, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.25F));
    q.add_entity(b, Vec3f { 1.0F, 0.0F, 0.0F }, box_at(Vec3f { 1.0F, 0.0F, 0.0F }, 0.25F));
    q.add_entity(c, Vec3f { 2.0F, 0.0F, 0.0F }, box_at(Vec3f { 2.0F, 0.0F, 0.0F }, 0.25F));
    q.rebuild();
    EXPECT_EQ(q.entity_count(), 3U);

    q.remove_entity(b);
    q.rebuild();
    EXPECT_EQ(q.entity_count(), 2U);

    auto hits = q.sphere_query(Vec3f { 1.0F, 0.0F, 0.0F }, 10.0F);
    ASSERT_EQ(hits.size(), 2U);
    bool saw_a = false;
    bool saw_b = false;
    bool saw_c = false;
    for (const auto& h : hits)
    {
        if (h.entity == a) saw_a = true;
        if (h.entity == b) saw_b = true;
        if (h.entity == c) saw_c = true;
    }
    EXPECT_TRUE(saw_a);
    EXPECT_FALSE(saw_b);
    EXPECT_TRUE(saw_c);
}

// -----------------------------------------------------------------------------
// 11) Degenerate: sphere_query with a negative radius returns an empty set
//     (header contract `radius < 0.0F` -> empty), and an empty world returns
//     empty regardless of radius. Locks the early-out guards in sphere_query.
// -----------------------------------------------------------------------------
TEST(GameQuery, SphereQueryNegativeRadiusAndEmptyWorld)
{
    // Empty world: any radius -> empty.
    QueryWorld empty { 2.0F };
    EXPECT_TRUE(empty.sphere_query(Vec3f { 0, 0, 0 }, 5.0F).empty());

    World w;
    Entity a = w.create();
    QueryWorld q { 2.0F };
    q.add_entity(a, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.5F));
    q.rebuild();

    // Negative radius -> empty (degenerate query, not a crash).
    EXPECT_TRUE(q.sphere_query(Vec3f { 0, 0, 0 }, -1.0F).empty());
    // Sanity: a valid radius still finds the entity.
    EXPECT_EQ(q.sphere_query(Vec3f { 0, 0, 0 }, 1.0F).size(), 1U);
}

// -----------------------------------------------------------------------------
// 12) Degenerate ray: a ray parallel to a slab whose origin lies OUTSIDE that
//     slab misses the box (the `d[i] == 0` parallel-slab branch of
//     intersect_ray_aabb). Prior tests only covered axis-aligned hits/misses
//     where every component was non-zero.
// -----------------------------------------------------------------------------
TEST(GameQuery, RayParallelToSlabOutsideMisses)
{
    const Aabb box = box_at(Vec3f { 5.0F, 0.0F, 0.0F }, 1.0F);  // y,z in [-1,1]

    // Ray travels +x but is offset to y=5 (outside the box's y-slab [-1,1]).
    // dir.y == 0 and origin.y is outside the slab -> miss.
    auto miss = intersect_ray_aabb(Vec3f { 0.0F, 5.0F, 0.0F },
                                   Vec3f { 1.0F, 0.0F, 0.0F }, box, 100.0F);
    EXPECT_FALSE(miss.has_value());

    // Same ray but offset INSIDE the y-slab (y=0.5) -> hits.
    auto hit = intersect_ray_aabb(Vec3f { 0.0F, 0.5F, 0.0F },
                                  Vec3f { 1.0F, 0.0F, 0.0F }, box, 100.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->t, 4.0F, 1e-5F);
}

// -----------------------------------------------------------------------------
// 13) Degenerate ray: origin already INSIDE the box reports t == 0 (the
//     `tmin < 0 -> t_hit = 0` clamp in intersect_ray_aabb). Locks the
//     "ray starts inside" branch untouched by prior tests.
// -----------------------------------------------------------------------------
TEST(GameQuery, RayOriginInsideBoxReportsZeroT)
{
    const Aabb box = box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 2.0F);  // [-2,2]^3
    auto hit = intersect_ray_aabb(Vec3f { 0.0F, 0.0F, 0.0F },
                                  Vec3f { 1.0F, 0.0F, 0.0F }, box, 100.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->t, 0.0F, 1e-5F);  // entry behind origin -> clamped to 0
}

}  // namespace
