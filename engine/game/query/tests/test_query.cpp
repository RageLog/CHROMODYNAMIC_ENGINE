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

// =============================================================================
// New gap-closing tests (100% depth)
// =============================================================================

// -----------------------------------------------------------------------------
// 14) Ray with box FULLY BEHIND the origin:
//     Box at x in [-5,-3]; dir = +x. All slab exits have tmax < 0 → miss.
//     Covers the `tmax < 0.0F` early-return in intersect_ray_aabb.
// -----------------------------------------------------------------------------
TEST(GameQuery, RayBoxFullyBehindOriginMisses)
{
    // Box spans x in [-5,-3], y/z in [-1,1]. Ray from origin points +x.
    const Aabb behind { Vec3f { -5.0F, -1.0F, -1.0F },
                        Vec3f { -3.0F,  1.0F,  1.0F } };
    auto hit = intersect_ray_aabb(Vec3f { 0.0F, 0.0F, 0.0F },
                                  Vec3f { 1.0F, 0.0F, 0.0F }, behind, 100.0F);
    EXPECT_FALSE(hit.has_value());
}

// -----------------------------------------------------------------------------
// 15) Grazing edge / corner: ray tangent to the box face (tmin == tmax on one
//     axis). Williams' slab method counts this as a hit (entry == exit at t).
//     Uses a ray passing through the corner y=1, z=1 of the box.
// -----------------------------------------------------------------------------
TEST(GameQuery, RayGrazingEdgeIsHit)
{
    // Box centred at (5,0,0) with half = 1 → y in [-1,1].
    // Ray at y=1 (corner) pointing +x. The y-slab: t1 = t2, so tmin == tmax
    // on exit. Result should still be a valid hit.
    const Aabb box = box_at(Vec3f { 5.0F, 0.0F, 0.0F }, 1.0F);
    auto hit = intersect_ray_aabb(Vec3f { 0.0F, 1.0F, 0.0F },
                                  Vec3f { 1.0F, 0.0F, 0.0F }, box, 100.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->t, 4.0F, 1e-5F);  // x-face still at t=4
}

// -----------------------------------------------------------------------------
// 16) Non-unit direction: a ray with |dir| == 2 pointing at the same box.
//     The parametric t is halved (t = 2 instead of 4) but the world-space
//     hit point is the same. Confirms the caller's parametric-t / world-dist
//     bookkeeping in QueryWorld::raycast.
// -----------------------------------------------------------------------------
TEST(GameQuery, RaycastNonUnitDirectionWorldPointCorrect)
{
    World w;
    Entity e = w.create();

    const Vec3f centre { 5.0F, 0.0F, 0.0F };
    QueryWorld q { 4.0F };
    q.add_entity(e, centre, box_at(centre, 1.0F));
    q.rebuild();

    // dir = (2,0,0) → |dir| = 2; entry face at x=4 → t_param = 2
    auto hit = q.raycast(Vec3f { 0.0F, 0.0F, 0.0F },
                         Vec3f { 2.0F, 0.0F, 0.0F }, 100.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, e);
    // World-space distance should still be 4
    EXPECT_NEAR(hit->distance, 4.0F, 1e-4F);
    // Hit point should be (4, 0, 0)
    EXPECT_NEAR(hit->point.x, 4.0F, 1e-4F);
    EXPECT_NEAR(hit->point.y, 0.0F, 1e-4F);
    EXPECT_NEAR(hit->point.z, 0.0F, 1e-4F);
}

// -----------------------------------------------------------------------------
// 17) max_dist exactly at the entry t: should HIT (contract: `tmin > max_dist`
//     → miss; tmin == max_dist → not excluded).
// -----------------------------------------------------------------------------
TEST(GameQuery, RayMaxDistAtEntryFaceIsHit)
{
    // Box at x in [4,6] → entry face at t=4 for origin=(0,0,0) dir=(1,0,0).
    const Aabb box = box_at(Vec3f { 5.0F, 0.0F, 0.0F }, 1.0F);
    auto hit = intersect_ray_aabb(Vec3f { 0.0F, 0.0F, 0.0F },
                                  Vec3f { 1.0F, 0.0F, 0.0F }, box,
                                  /*max_dist=*/4.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->t, 4.0F, 1e-5F);
}

// -----------------------------------------------------------------------------
// 18) Frustum: entity that STRADDLES a frustum plane is still returned (the
//     p-vertex test reports it as intersecting). Tests the intermediate case
//     between "fully in" and "fully out".
// -----------------------------------------------------------------------------
TEST(GameQuery, FrustumQueryStraddlingEntityIsReturned)
{
    World w;
    Entity e = w.create();

    // Frustum: box [-10,10]^3. Entity AABB: half = 3, centred at (8,0,0).
    // AABB spans x in [5,11] — straddles the right plane (x = 10).
    QueryWorld q { 4.0F };
    const Vec3f ctr { 8.0F, 0.0F, 0.0F };
    q.add_entity(e, ctr, Aabb { Vec3f { 5.0F, -3.0F, -3.0F },
                                Vec3f { 11.0F, 3.0F, 3.0F } });
    q.rebuild();

    const Frustum f = box_frustum(-10.0F, 10.0F);
    auto hits = q.frustum_query(f);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0], e);
}

// -----------------------------------------------------------------------------
// 19) Frustum: entity whose AABB just touches a frustum plane (on the boundary)
//     is considered inside (inclusive p-vertex test: signed_distance >= 0).
// -----------------------------------------------------------------------------
TEST(GameQuery, FrustumQueryBoundaryTouchIsInside)
{
    World w;
    Entity e = w.create();

    // Frustum: box [-10,10]^3. Entity AABB just touches the right plane:
    // AABB max.x == 10 exactly.
    QueryWorld q { 4.0F };
    q.add_entity(e, Vec3f { 9.5F, 0.0F, 0.0F },
                 Aabb { Vec3f { 9.0F, -0.5F, -0.5F },
                        Vec3f { 10.0F,  0.5F,  0.5F } });
    q.rebuild();

    const Frustum f = box_frustum(-10.0F, 10.0F);
    auto hits = q.frustum_query(f);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0], e);
}

// -----------------------------------------------------------------------------
// 20) Frustum query on an empty world returns empty vector.
// -----------------------------------------------------------------------------
TEST(GameQuery, FrustumQueryEmptyWorldReturnsEmpty)
{
    QueryWorld q { 4.0F };
    const Frustum f = box_frustum(-10.0F, 10.0F);
    EXPECT_TRUE(q.frustum_query(f).empty());
    // Also the 6-plane array overload.
    const std::array<cd::scene::Plane, 6> planes { f.left, f.right, f.bottom,
                                                   f.top,  f.near_, f.far_ };
    EXPECT_TRUE(q.frustum_query(planes).empty());
}

// -----------------------------------------------------------------------------
// 21) 6-plane array overload of frustum_query produces the same results as the
//     Frustum struct overload on a non-empty world.
// -----------------------------------------------------------------------------
TEST(GameQuery, FrustumQueryArrayOverloadMatchesStructOverload)
{
    World w;
    Entity in1 = w.create();
    Entity in2 = w.create();
    Entity out1 = w.create();

    QueryWorld q { 4.0F };
    q.add_entity(in1, Vec3f { 0.0F, 0.0F,  0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 1.0F));
    q.add_entity(in2, Vec3f { 3.0F, 0.0F, -3.0F }, box_at(Vec3f { 3.0F, 0.0F, -3.0F }, 1.0F));
    q.add_entity(out1, Vec3f { 50.0F, 0.0F, 0.0F }, box_at(Vec3f { 50.0F, 0.0F, 0.0F }, 1.0F));
    q.rebuild();

    const Frustum f = box_frustum(-10.0F, 10.0F);
    const std::array<cd::scene::Plane, 6> planes { f.left, f.right, f.bottom,
                                                   f.top,  f.near_, f.far_ };
    auto by_struct = q.frustum_query(f);
    auto by_array  = q.frustum_query(planes);

    ASSERT_EQ(by_struct.size(), 2U);
    ASSERT_EQ(by_array.size(), 2U);
    // Both must contain in1 and in2, neither out1.
    std::ranges::sort(by_struct, [](Entity a, Entity b)
                      { return a.id < b.id; });
    std::ranges::sort(by_array,  [](Entity a, Entity b)
                      { return a.id < b.id; });
    EXPECT_EQ(by_struct, by_array);
}

// -----------------------------------------------------------------------------
// 22) Dedup across cells: the broad-phase sphere query iterates a 3D cell
//     range and appends each cell's entity list into candidates. When the
//     same entity was inserted into a cell that is enumerated multiple times
//     (e.g. from a very wide query radius touching adjacent cells that were
//     each separately filled), it could in theory appear twice. The `seen`
//     unordered_set in sphere_query / box_query / raycast guards against this.
//
//     We validate the deduplicated contract by building a world with several
//     entities, querying with a wide sphere, and confirming each entity appears
//     exactly once in the result — regardless of how many cells the broad phase
//     scanned that might contain duplicates.
// -----------------------------------------------------------------------------
TEST(GameQuery, DedupEntityAppearsOnceInResults)
{
    World w;
    Entity a = w.create();
    Entity b = w.create();
    Entity c = w.create();

    // cell_size = 1. Entities placed at x = 0.5, 1.5, 2.5 → cells 0, 1, 2.
    // Wide sphere of radius 10 touches all three cells (and many empty ones).
    QueryWorld q { 1.0F };
    q.add_entity(a, Vec3f { 0.5F, 0.0F, 0.0F }, box_at(Vec3f { 0.5F, 0.0F, 0.0F }, 0.2F));
    q.add_entity(b, Vec3f { 1.5F, 0.0F, 0.0F }, box_at(Vec3f { 1.5F, 0.0F, 0.0F }, 0.2F));
    q.add_entity(c, Vec3f { 2.5F, 0.0F, 0.0F }, box_at(Vec3f { 2.5F, 0.0F, 0.0F }, 0.2F));
    q.rebuild();

    auto sphere_hits = q.sphere_query(Vec3f { 1.5F, 0.0F, 0.0F }, 10.0F);
    // Each of a, b, c must appear exactly once.
    int ca = 0;
    int cb = 0;
    int cc = 0;
    for (const auto& h : sphere_hits)
    {
        if (h.entity == a) ++ca;
        if (h.entity == b) ++cb;
        if (h.entity == c) ++cc;
    }
    EXPECT_EQ(ca, 1);
    EXPECT_EQ(cb, 1);
    EXPECT_EQ(cc, 1);

    // box_query over the same area must also produce exactly one hit per entity.
    auto box_hits = q.box_query(box_at(Vec3f { 1.5F, 0.0F, 0.0F }, 5.0F));
    int ba = 0;
    int bb = 0;
    int bc = 0;
    for (auto e : box_hits)
    {
        if (e == a) ++ba;
        if (e == b) ++bb;
        if (e == c) ++bc;
    }
    EXPECT_EQ(ba, 1);
    EXPECT_EQ(bb, 1);
    EXPECT_EQ(bc, 1);
}

// -----------------------------------------------------------------------------
// 23) Dense same-cell: multiple distinct entities at the same world position
//     (same hash cell) — all are returned correctly by sphere_query and
//     box_query; none are dropped or duplicated.
// -----------------------------------------------------------------------------
TEST(GameQuery, DenseSameCellMultipleEntities)
{
    World w;
    Entity a = w.create();
    Entity b = w.create();
    Entity c = w.create();

    // All three at origin — same cell — cell_size = 4.
    QueryWorld q { 4.0F };
    q.add_entity(a, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.2F));
    q.add_entity(b, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.2F));
    q.add_entity(c, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.2F));
    q.rebuild();

    auto hits = q.sphere_query(Vec3f { 0.0F, 0.0F, 0.0F }, 1.0F);
    ASSERT_EQ(hits.size(), 3U);  // all three, each exactly once

    const Aabb qb = box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 1.0F);
    auto box_hits = q.box_query(qb);
    ASSERT_EQ(box_hits.size(), 3U);
}

// -----------------------------------------------------------------------------
// 24) Negative coordinates: entities at negative world positions. Tests that
//     SpatialHash::cell_of floors correctly for negative values (floor(-0.5)
//     == -1, not 0). Both insertion and query must land in the same cell.
// -----------------------------------------------------------------------------
TEST(GameQuery, NegativeWorldCoordinatesQueriedCorrectly)
{
    World w;
    Entity neg = w.create();
    Entity pos = w.create();

    QueryWorld q { 2.0F };
    q.add_entity(neg, Vec3f { -5.0F, -3.0F, -1.0F },
                 box_at(Vec3f { -5.0F, -3.0F, -1.0F }, 0.4F));
    q.add_entity(pos, Vec3f {  5.0F,  3.0F,  1.0F },
                 box_at(Vec3f {  5.0F,  3.0F,  1.0F }, 0.4F));
    q.rebuild();

    // Query centred on the negative entity — should find it, not the positive one.
    auto hits = q.sphere_query(Vec3f { -5.0F, -3.0F, -1.0F }, 1.0F);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].entity, neg);

    // Raycast from x=-20 towards +x should hit neg first.
    auto ray = q.raycast(Vec3f { -20.0F, -3.0F, -1.0F },
                         Vec3f {   1.0F,  0.0F,  0.0F }, 50.0F);
    ASSERT_TRUE(ray.has_value());
    EXPECT_EQ(ray->entity, neg);
}

// -----------------------------------------------------------------------------
// 25) Zero-size AABB query: box_query with a degenerate Aabb (min == max)
//     should return the entity whose AABB contains that point (inclusive
//     overlaps check: `a.min <= b.max && a.max >= b.min` holds when min==max
//     and the point is inside the other box).
// -----------------------------------------------------------------------------
TEST(GameQuery, BoxQueryZeroSizeQueryAabb)
{
    World w;
    Entity e = w.create();

    QueryWorld q { 4.0F };
    q.add_entity(e, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 2.0F));
    q.rebuild();

    // Zero-size query at the centre of the entity — should still match.
    const Aabb point_aabb { Vec3f { 0.0F, 0.0F, 0.0F }, Vec3f { 0.0F, 0.0F, 0.0F } };
    auto hits = q.box_query(point_aabb);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0], e);

    // Zero-size query outside the entity — no match.
    const Aabb outside { Vec3f { 10.0F, 0.0F, 0.0F }, Vec3f { 10.0F, 0.0F, 0.0F } };
    EXPECT_TRUE(q.box_query(outside).empty());
}

// -----------------------------------------------------------------------------
// 26) add_entity with a duplicate entity replaces the record (position +
//     AABB both updated). After rebuild, the old position is no longer
//     found and the new position is.
// -----------------------------------------------------------------------------
TEST(GameQuery, AddEntityReplacesExistingRecord)
{
    World w;
    Entity e = w.create();

    QueryWorld q { 2.0F };
    q.add_entity(e, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.5F));
    EXPECT_EQ(q.entity_count(), 1U);  // only one record despite two insertions

    // Re-insert at a different position.
    q.add_entity(e, Vec3f { 10.0F, 0.0F, 0.0F }, box_at(Vec3f { 10.0F, 0.0F, 0.0F }, 0.5F));
    EXPECT_EQ(q.entity_count(), 1U);
    q.rebuild();

    // Tight query at old pos → nothing.
    EXPECT_TRUE(q.sphere_query(Vec3f { 0.0F, 0.0F, 0.0F }, 1.0F).empty());
    // Query at new pos → finds entity.
    auto hits = q.sphere_query(Vec3f { 10.0F, 0.0F, 0.0F }, 1.0F);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].entity, e);
}

// -----------------------------------------------------------------------------
// 27) rebuild_from with a null/empty EnumerateFn must not crash and must
//     leave the world empty (entity_count == 0).
// -----------------------------------------------------------------------------
TEST(GameQuery, RebuildFromNullCallbackIsNoOp)
{
    World w;
    Entity e = w.create();

    QueryWorld q { 2.0F };
    q.add_entity(e, Vec3f { 0.0F, 0.0F, 0.0F }, box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 0.5F));
    q.rebuild();
    EXPECT_EQ(q.entity_count(), 1U);

    // Passing an empty std::function must clear the world and not crash.
    q.rebuild_from(QueryWorld::EnumerateFn {});
    EXPECT_EQ(q.entity_count(), 0U);
    EXPECT_TRUE(q.sphere_query(Vec3f { 0, 0, 0 }, 100.0F).empty());
}

// -----------------------------------------------------------------------------
// 28) box_query on an empty world returns empty vector.
// -----------------------------------------------------------------------------
TEST(GameQuery, BoxQueryEmptyWorldReturnsEmpty)
{
    QueryWorld q { 4.0F };
    const Aabb box = box_at(Vec3f { 0.0F, 0.0F, 0.0F }, 10.0F);
    EXPECT_TRUE(q.box_query(box).empty());
}

// -----------------------------------------------------------------------------
// 29) Raycast brute-force fallback: a single entity inserted at a position far
//     from the ray midpoint sphere so the spatial hash returns no candidates,
//     forcing the fallback scan. The ray still finds the entity.
//
//     This is the "very long ray, sparse hash" path in QueryWorld::raycast
//     (`if (!best.has_value() && candidates.empty())`).
// -----------------------------------------------------------------------------
TEST(GameQuery, RaycastBruteForcesFallbackForLongRay)
{
    World w;
    Entity e = w.create();

    // Place entity far from origin; cell_size = 1. The ray from origin in +x
    // towards x=1000 has a bounding sphere of radius ~500. The entity at
    // x=950 is well within that sphere so we need to be careful — use a VERY
    // small cell and a single entity that the sphere-broad-phase will catch.
    // Instead, test the fallback by placing the entity completely off the ray
    // axis but within brute-force reach:
    // Ray from (0,0,0) → (+x) max=1000, entity at (500, 0, 0) — the sphere
    // bounding the ray (centre=(500,0,0), r=500+cell) must include this cell.
    // The fallback is only hit when candidates is EMPTY, so we need a scenario
    // where the hash returns zero cells. That happens when max_dist is short
    // but we pass a long max_dist; OR the cell_size is very large relative to
    // the entity's distance. The simplest trigger: entity at (5,0,0), cell_size
    // very large (500) so the sphere query covers exactly 1 bucket. In that
    // case candidates will NOT be empty, so let's instead confirm via clear():
    // after rebuild() the hash has data, so candidates from query_sphere will
    // be non-empty for a properly-placed entity.
    //
    // The ACTUAL fallback trigger: call raycast() without prior rebuild() so
    // hash_ is empty but records_ is populated.
    QueryWorld q { 4.0F };
    q.add_entity(e, Vec3f { 5.0F, 0.0F, 0.0F }, box_at(Vec3f { 5.0F, 0.0F, 0.0F }, 1.0F));
    // Intentionally NOT calling rebuild() — hash is empty, records_ is populated.
    // The sphere query on the empty hash returns nothing, triggering fallback.
    auto hit = q.raycast(Vec3f { 0.0F, 0.0F, 0.0F }, Vec3f { 1.0F, 0.0F, 0.0F }, 100.0F);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, e);
    EXPECT_NEAR(hit->distance, 4.0F, 1e-4F);
}

// -----------------------------------------------------------------------------
// 30) Degenerate AABB in add_entity (min == max — a point-sized entity):
//     sphere_query still finds it when the query sphere contains the point;
//     box_query finds it when the query AABB contains it.
// -----------------------------------------------------------------------------
TEST(GameQuery, DegenerateEntityAabbIsQueryable)
{
    World w;
    Entity e = w.create();

    QueryWorld q { 2.0F };
    const Vec3f pt { 3.0F, 3.0F, 3.0F };
    q.add_entity(e, pt, Aabb { pt, pt });  // zero-extent AABB
    q.rebuild();

    // Sphere centred at that point with radius 0 should find it (d==0 <= r==0).
    auto sphere = q.sphere_query(pt, 0.0F);
    ASSERT_EQ(sphere.size(), 1U);
    EXPECT_EQ(sphere[0].entity, e);

    // box_query with a box that contains pt.
    auto bq = q.box_query(box_at(pt, 1.0F));
    ASSERT_EQ(bq.size(), 1U);
    EXPECT_EQ(bq[0], e);
}

}  // namespace
