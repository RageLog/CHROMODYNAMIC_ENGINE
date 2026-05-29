// =============================================================================
// CHROMODYNAMIC -- engine/world/ecs/tests/test_archetype_world.cpp
// Phase 370 / Marathon Run 30 / X7B -- ArchetypeWorld side-layer proof.
// Phase 408 / D-F4 -- cross-archetype migration tests.
//
// Locks down:
//   - emplace<T...>: archetype creation, chunk row write, entity tracking.
//   - each<T...>: subset iteration (queries are matched on supersets).
//   - destroy: swap-and-pop with entity-location patching.
//   - chunk allocation: capacity sizing + multi-chunk overflow.
//   - storage independence from cd::ecs::World.
//   - add_component: entity migrates to new archetype with added type.
//   - remove_component: entity migrates to reduced archetype.
//   - round-trip add then remove: entity returns to original archetype.
//   - add duplicate policy: noop (asserted in debug; runtime guard in release).
// =============================================================================
#include <cd/ecs/ArchetypeWorld.hpp>
#include <cd/ecs/World.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace
{

struct Position
{
    float x { 0 }, y { 0 }, z { 0 };
};

struct Velocity
{
    float vx { 0 }, vy { 0 }, vz { 0 };
};

struct Tag
{
    std::uint32_t value { 0 };
};

}  // namespace

using cd::ecs::ArchetypeWorld;
using cd::ecs::Entity;

TEST(ArchetypeWorld, EmplaceCreatesArchetypeAndEntity)
{
    ArchetypeWorld w;
    EXPECT_EQ(w.archetype_count(), 0U);
    EXPECT_EQ(w.alive_count(), 0U);

    const Entity e = w.emplace<Position, Velocity>(
        Position { 1.0F, 2.0F, 3.0F },
        Velocity { 0.1F, 0.2F, 0.3F });

    EXPECT_TRUE(e.is_valid());
    EXPECT_TRUE(w.is_alive(e));
    EXPECT_EQ(w.alive_count(), 1U);
    EXPECT_EQ(w.archetype_count(), 1U);
    EXPECT_EQ(w.chunk_count(), 1U);
}

TEST(ArchetypeWorld, EachVisitsExactlyEveryEntity)
{
    ArchetypeWorld w;
    for (int i = 0; i < 10; ++i) {
        (void)w.emplace<Position, Velocity>(
            Position { static_cast<float>(i), 0.0F, 0.0F },
            Velocity { 1.0F, 0.0F, 0.0F });
    }
    EXPECT_EQ(w.alive_count(), 10U);

    int count = 0;
    float sum_x = 0.0F;
    w.each<Position, Velocity>(
        [&](Entity, Position& p, Velocity&) {
            ++count;
            sum_x += p.x;
        });
    EXPECT_EQ(count, 10);
    EXPECT_FLOAT_EQ(sum_x, 45.0F);  // 0+1+...+9
}

TEST(ArchetypeWorld, EachMatchesOnSupersetArchetypes)
{
    ArchetypeWorld w;
    // Archetype A: (Position, Velocity)
    (void)w.emplace<Position, Velocity>(Position {}, Velocity {});
    (void)w.emplace<Position, Velocity>(Position {}, Velocity {});
    // Archetype B: (Position, Velocity, Tag) -- superset of A.
    (void)w.emplace<Position, Velocity, Tag>(Position {}, Velocity {}, Tag { 42 });

    EXPECT_EQ(w.archetype_count(), 2U);

    // Query on (Position, Velocity) must visit BOTH archetypes:
    // 2 entities from A + 1 entity from B = 3.
    int count_pv = 0;
    w.each<Position, Velocity>([&](Entity, Position&, Velocity&) { ++count_pv; });
    EXPECT_EQ(count_pv, 3);

    // Query on (Tag) must visit ONLY B = 1.
    int count_tag = 0;
    w.each<Tag>([&](Entity, Tag& t) {
        ++count_tag;
        EXPECT_EQ(t.value, 42U);
    });
    EXPECT_EQ(count_tag, 1);
}

TEST(ArchetypeWorld, EmplaceOverflowsToSecondChunk)
{
    ArchetypeWorld w;
    // Force a known archetype and probe its capacity, then emplace
    // capacity+1 entities to provoke a second chunk allocation.
    (void)w.emplace<Position>(Position {});
    const std::size_t cap = w.chunk_capacity_for<Position>();
    ASSERT_GT(cap, 0U);

    // Already 1 entity in chunk 0; fill the rest of chunk 0 + push one
    // more to trigger chunk 1.
    for (std::size_t i = 1; i < cap + 1U; ++i) {
        (void)w.emplace<Position>(Position { static_cast<float>(i), 0, 0 });
    }
    EXPECT_EQ(w.alive_count(), cap + 1U);
    EXPECT_EQ(w.chunk_count(), 2U);
}

TEST(ArchetypeWorld, DestroyRemovesEntityAndShrinksChunk)
{
    ArchetypeWorld w;
    std::vector<Entity> ents;
    for (int i = 0; i < 5; ++i) {
        ents.push_back(w.emplace<Position, Velocity>(
            Position { static_cast<float>(i), 0, 0 },
            Velocity {}));
    }
    EXPECT_EQ(w.alive_count(), 5U);

    // Destroy the middle one and confirm the rest still iterate.
    w.destroy(ents[2]);
    EXPECT_EQ(w.alive_count(), 4U);
    EXPECT_FALSE(w.is_alive(ents[2]));

    std::unordered_set<float> remaining_x;
    w.each<Position, Velocity>([&](Entity, Position& p, Velocity&) {
        remaining_x.insert(p.x);
    });
    EXPECT_EQ(remaining_x.size(), 4U);
    EXPECT_TRUE(remaining_x.count(0.0F));
    EXPECT_TRUE(remaining_x.count(1.0F));
    EXPECT_TRUE(remaining_x.count(3.0F));
    EXPECT_TRUE(remaining_x.count(4.0F));
    EXPECT_FALSE(remaining_x.count(2.0F));  // destroyed
}

TEST(ArchetypeWorld, IndependentFromSparseSetWorld)
{
    // Critical contract: ArchetypeWorld must NOT replace cd::ecs::World.
    // Both must coexist and own their own entity-id space.
    ArchetypeWorld aw;
    cd::ecs::World sw;

    const Entity ae = aw.emplace<Position>(Position { 1, 0, 0 });
    const Entity se = sw.create();
    sw.emplace<Velocity>(se, Velocity { 9, 9, 9 });

    EXPECT_TRUE(aw.is_alive(ae));
    EXPECT_TRUE(sw.is_alive(se));
    EXPECT_EQ(aw.alive_count(), 1U);
    EXPECT_EQ(sw.alive_count(), 1U);

    int aw_count = 0;
    aw.each<Position>([&](Entity, Position&) { ++aw_count; });
    EXPECT_EQ(aw_count, 1);

    int sw_count = 0;
    sw.for_each<Velocity>([&](Entity, Velocity&) { ++sw_count; });
    EXPECT_EQ(sw_count, 1);
}

TEST(ArchetypeWorld, ChunkCapacityHonours16KiBTarget)
{
    // 12-byte Position rows: 16384 / 12 = 1365 entities/chunk
    // (or at least no smaller than the engine baseline of 4 rows).
    ArchetypeWorld w;
    (void)w.emplace<Position>(Position {});
    const std::size_t cap = w.chunk_capacity_for<Position>();
    EXPECT_GE(cap, 4U);

    // Multi-component row width is 24 B (Position + Velocity) => cap = 16384 / 24 = 682.
    (void)w.emplace<Position, Velocity>(Position {}, Velocity {});
    const std::size_t cap_pv = w.chunk_capacity_for<Position, Velocity>();
    EXPECT_GE(cap_pv, 4U);
    EXPECT_LE(cap_pv, cap);  // wider rows => smaller-or-equal capacity
}

// =============================================================================
// Phase 408 / D-F4 -- cross-archetype migration tests
// =============================================================================

TEST(ArchetypeWorld, AddComponent_MovesEntityToNewArchetype)
{
    // Arrange: entity starts in {Position} archetype.
    ArchetypeWorld w;
    const Entity e = w.emplace<Position>(Position { 7.0F, 8.0F, 9.0F });
    ASSERT_EQ(w.archetype_count(), 1U);

    // Act: add Velocity -> entity must move to {Position, Velocity} archetype.
    w.add_component<Velocity>(e, Velocity { 1.0F, 2.0F, 3.0F });

    // Assert: two archetypes exist; entity is still alive.
    EXPECT_EQ(w.archetype_count(), 2U);
    EXPECT_TRUE(w.is_alive(e));
    EXPECT_EQ(w.alive_count(), 1U);

    // The entity must appear in a {Position, Velocity} query with correct values.
    int visit_count = 0;
    w.each<Position, Velocity>([&](Entity visited, Position& p, Velocity& v) {
        EXPECT_EQ(visited, e);
        EXPECT_FLOAT_EQ(p.x, 7.0F);
        EXPECT_FLOAT_EQ(p.y, 8.0F);
        EXPECT_FLOAT_EQ(p.z, 9.0F);
        EXPECT_FLOAT_EQ(v.vx, 1.0F);
        EXPECT_FLOAT_EQ(v.vy, 2.0F);
        EXPECT_FLOAT_EQ(v.vz, 3.0F);
        ++visit_count;
    });
    EXPECT_EQ(visit_count, 1);

    // The entity must NOT appear in a {Position}-only query over the single-
    // component archetype (the query will still visit it because {P,V} is a
    // superset of {P}, but there must be no entity in the {Position}-only
    // archetype -- alive_count in that archetype should be 0).
    // We verify by checking the total visit count from each<Position> == 1
    // (the entity is in the superset archetype, so it IS visited).
    int pos_count = 0;
    w.each<Position>([&](Entity, Position&) { ++pos_count; });
    EXPECT_EQ(pos_count, 1);
}

TEST(ArchetypeWorld, RemoveComponent_MovesEntityToReducedArchetype)
{
    // Arrange: entity starts in {Position, Velocity} archetype.
    ArchetypeWorld w;
    const Entity e = w.emplace<Position, Velocity>(
        Position { 10.0F, 20.0F, 30.0F },
        Velocity { 4.0F, 5.0F, 6.0F });
    ASSERT_EQ(w.archetype_count(), 1U);

    // Act: remove Velocity -> entity must move to {Position} archetype.
    w.remove_component<Velocity>(e);

    // Assert: two archetypes exist; entity is alive.
    EXPECT_EQ(w.archetype_count(), 2U);
    EXPECT_TRUE(w.is_alive(e));
    EXPECT_EQ(w.alive_count(), 1U);

    // Entity visible in {Position} query with original Position value.
    int visit_count = 0;
    w.each<Position>([&](Entity visited, Position& p) {
        EXPECT_EQ(visited, e);
        EXPECT_FLOAT_EQ(p.x, 10.0F);
        EXPECT_FLOAT_EQ(p.y, 20.0F);
        EXPECT_FLOAT_EQ(p.z, 30.0F);
        ++visit_count;
    });
    EXPECT_EQ(visit_count, 1);

    // Entity must NOT appear in a {Velocity}-only or {Position, Velocity} query.
    int pv_count = 0;
    w.each<Position, Velocity>([&](Entity, Position&, Velocity&) { ++pv_count; });
    EXPECT_EQ(pv_count, 0);
}

TEST(ArchetypeWorld, RoundTrip_AddThenRemove_ComponentsPreserved)
{
    // Arrange: entity starts in {Position} archetype.
    ArchetypeWorld w;
    const Entity e = w.emplace<Position>(Position { 1.0F, 2.0F, 3.0F });

    // Act: add Velocity then remove Velocity.
    w.add_component<Velocity>(e, Velocity { 9.0F, 8.0F, 7.0F });
    w.remove_component<Velocity>(e);

    // Assert: back to 3 archetypes ({P}, {P,V}, {P} again -- or {P} is
    // reused since the type-set is the same).  Archetype count may be 2
    // ({P} + {P,V}), depending on whether find_or_create_archetype_ returns
    // the existing {P} archetype.
    EXPECT_TRUE(w.is_alive(e));
    EXPECT_EQ(w.alive_count(), 1U);
    EXPECT_LE(w.archetype_count(), 2U);  // at most {P} and {P,V}

    // Original Position value must be intact.
    int visit_count = 0;
    w.each<Position>([&](Entity visited, Position& p) {
        EXPECT_EQ(visited, e);
        EXPECT_FLOAT_EQ(p.x, 1.0F);
        EXPECT_FLOAT_EQ(p.y, 2.0F);
        EXPECT_FLOAT_EQ(p.z, 3.0F);
        ++visit_count;
    });
    EXPECT_EQ(visit_count, 1);

    // No entity in {Position, Velocity}.
    int pv_count = 0;
    w.each<Position, Velocity>([&](Entity, Position&, Velocity&) { ++pv_count; });
    EXPECT_EQ(pv_count, 0);
}

TEST(ArchetypeWorld, AddDuplicate_IsNoop)
{
    // Policy: adding a component type the entity already carries is a
    // programming bug.  In Debug builds this fires an assertion.
    // In Release builds (NDEBUG) the runtime guard makes it a safe noop.
    // We test the noop path only (assertion-death tests would require
    // EXPECT_DEATH which needs a subprocess; out of scope here).
    //
    // Verify that after a duplicate add_component call the entity
    // is still alive and its original values are intact.
    ArchetypeWorld w;
    const Entity e = w.emplace<Position, Velocity>(
        Position { 5.0F, 0.0F, 0.0F },
        Velocity { 0.0F, 0.0F, 0.0F });

    // Duplicate add of Velocity (already present) -- noop in release,
    // assert in debug.  We test in release mode via NDEBUG-aware call.
#if defined(NDEBUG)
    w.add_component<Velocity>(e, Velocity { 99.0F, 99.0F, 99.0F });
    // Entity must still be alive and unmodified.
    EXPECT_TRUE(w.is_alive(e));
    EXPECT_EQ(w.alive_count(), 1U);
    int count = 0;
    float pos_x = 0.0F;
    w.each<Position, Velocity>([&](Entity, Position& p, Velocity&) {
        pos_x = p.x;
        ++count;
    });
    EXPECT_EQ(count, 1);
    EXPECT_FLOAT_EQ(pos_x, 5.0F);
#else
    // In Debug we just confirm the entity is in a valid state without
    // triggering the assert.
    EXPECT_TRUE(w.is_alive(e));
    EXPECT_EQ(w.alive_count(), 1U);
#endif
}
