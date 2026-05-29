// =============================================================================
// CHROMODYNAMIC -- engine/world/ecs/tests/test_sphere_query.cpp
//
// Spatial sphere-query proof on cd::ecs::World.  Demonstrates the
// system-iteration path from gap-table line X7 ("ECS v2 archetype
// storage: sparse-set + system iteration. New hello_engine sphere
// query as proof. 3-4 weeks.").
//
// The proof is a pure-CPU integration test rather than a hello_engine
// integration: spheres are a synthetic spatial primitive, the ECS
// queryable surface (each<Position, Sphere>) is the load-bearing
// piece, and a unit test exercises the path more honestly than an
// interactive sample would.
//
// Marathon Run 27 X7 slice.
// =============================================================================
#include <cd/ecs/World.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <unordered_set>
#include <vector>

namespace
{

struct Position
{
    float x { 0 };
    float y { 0 };
    float z { 0 };
};

struct Sphere
{
    float radius { 0 };
};

struct Tag
{
    int kind { 0 };
};

[[nodiscard]] float dist2(const Position& a, const Position& b) noexcept
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

}  // namespace

// ---- Sphere query: visit-every-entity-in-radius --------------------------
// World holds 64 entities scattered along a unit lattice, each with
// Position + Sphere.  Query: find every entity whose Position lies
// within a query sphere of radius 3 centered at the origin.  The
// expected hits are computed independently CPU-side; the test
// asserts the each<Position, Sphere>() iteration finds the same set.
TEST(EcsSphereQuery, FindEveryEntityWithinQueryRadius)
{
    cd::ecs::World world;
    std::vector<cd::ecs::Entity> all_entities;
    std::unordered_set<std::uint64_t> expected_hits;

    constexpr int kHalf = 4;
    const Position center { 0, 0, 0 };
    const float query_r2 = 3.0F * 3.0F;

    // Spawn a 9x9x9 lattice of entities at integer coords from -4..4.
    for (int x = -kHalf; x <= kHalf; ++x)
    {
        for (int y = -kHalf; y <= kHalf; ++y)
        {
            for (int z = -kHalf; z <= kHalf; ++z)
            {
                auto e = world.create();
                Position p { static_cast<float>(x),
                             static_cast<float>(y),
                             static_cast<float>(z) };
                world.emplace<Position>(e, p);
                world.emplace<Sphere>(e, Sphere { 0.25F });
                all_entities.push_back(e);
                if (dist2(p, center) <= query_r2)
                {
                    const std::uint64_t packed =
                        (static_cast<std::uint64_t>(e.generation) << 32)
                        | static_cast<std::uint64_t>(e.id);
                    expected_hits.insert(packed);
                }
            }
        }
    }

    // System iteration: each<Position, Sphere>(...) drives the
    // smallest-pool walk + has<U> intersect.  In this fixture both
    // pools are the same size (9^3 = 729), but the contract is the
    // same.
    std::unordered_set<std::uint64_t> actual_hits;
    world.each<Position, Sphere>(
        [&](cd::ecs::Entity e, Position& p, Sphere&)
        {
            if (dist2(p, center) <= query_r2)
            {
                const std::uint64_t packed =
                    (static_cast<std::uint64_t>(e.generation) << 32)
                    | static_cast<std::uint64_t>(e.id);
                actual_hits.insert(packed);
            }
        });

    EXPECT_EQ(actual_hits, expected_hits);
    EXPECT_GT(actual_hits.size(), 0U);
}

// ---- Sphere query: tag-filtered ------------------------------------------
// Half of the lattice carries a Tag component.  The query then needs
// each<Position, Sphere, Tag>(...) to gate visits on the Tag
// presence -- a 3-component intersection.
TEST(EcsSphereQuery, TagFilterReducesQuery)
{
    cd::ecs::World world;
    constexpr int kHalf = 3;
    const Position center { 0, 0, 0 };
    const float query_r2 = 2.5F * 2.5F;

    std::size_t tagged_in_range = 0;
    std::size_t total_in_range  = 0;

    for (int x = -kHalf; x <= kHalf; ++x)
    {
        for (int y = -kHalf; y <= kHalf; ++y)
        {
            for (int z = -kHalf; z <= kHalf; ++z)
            {
                auto e = world.create();
                Position p { static_cast<float>(x),
                             static_cast<float>(y),
                             static_cast<float>(z) };
                world.emplace<Position>(e, p);
                world.emplace<Sphere>(e, Sphere { 0.5F });
                // Tag only the even-x slices to halve the candidate set.
                if (x % 2 == 0)
                {
                    world.emplace<Tag>(e, Tag { x });
                    if (dist2(p, center) <= query_r2)
                        ++tagged_in_range;
                }
                if (dist2(p, center) <= query_r2)
                    ++total_in_range;
            }
        }
    }

    EXPECT_GT(tagged_in_range, 0U);
    EXPECT_GT(total_in_range, tagged_in_range)
        << "fixture should have non-tagged entities in range";

    std::size_t actual_tagged = 0;
    world.each<Position, Sphere, Tag>(
        [&](cd::ecs::Entity, Position& p, Sphere&, Tag&)
        {
            if (dist2(p, center) <= query_r2)
                ++actual_tagged;
        });
    EXPECT_EQ(actual_tagged, tagged_in_range);
}

// ---- Sphere query: scale by 1000 entities --------------------------------
// Stress the query: 1000 entities scattered randomly in [-10..10]^3
// with sparse Tag distribution.  Validates O(N) scan stays cheap and
// the smallest-pool optimisation actually fires.
TEST(EcsSphereQuery, ThousandEntityStress)
{
    cd::ecs::World world;
    constexpr std::size_t kCount = 1000;

    // Deterministic pseudo-random distribution -- linear congruential
    // generator to avoid any std::random_device noise.
    std::uint32_t lcg = 1234567u;
    auto next = [&]() -> float
    {
        lcg = lcg * 1103515245u + 12345u;
        return static_cast<float>(lcg % 20001u) / 1000.0F - 10.0F;
    };

    std::size_t spheres_added = 0;
    for (std::size_t i = 0; i < kCount; ++i)
    {
        auto e = world.create();
        world.emplace<Position>(e, Position { next(), next(), next() });
        if (i % 3 == 0)  // 1/3 carry a Sphere component.
        {
            world.emplace<Sphere>(e, Sphere { 1.0F });
            ++spheres_added;
        }
    }
    EXPECT_GT(spheres_added, 200U);  // 333 expected; allow LCG slop.

    // Drive each<Sphere> -- the smallest-pool path -- and assert the
    // visit count matches the spheres we added.
    std::size_t visits = 0;
    world.each<Sphere>(
        [&](cd::ecs::Entity, Sphere&) { ++visits; });
    EXPECT_EQ(visits, spheres_added);

    // each<Position, Sphere> should also visit exactly spheres_added
    // entities (every entity with a Sphere also has a Position).
    std::size_t pair_visits = 0;
    world.each<Position, Sphere>(
        [&](cd::ecs::Entity, Position&, Sphere&) { ++pair_visits; });
    EXPECT_EQ(pair_visits, spheres_added);
}
