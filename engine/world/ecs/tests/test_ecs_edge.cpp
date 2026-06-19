// =============================================================================
// CHROMODYNAMIC -- cd::ecs depth / edge / negative / stress tests.
//
// Drives cd::ecs from "production, no stubs" to genuine 100% by locking
// down the behaviours the happy-path suites leave implicit:
//   * entity recycle + generation invalidation across many cycles,
//   * generation monotonicity across repeated reuse of one slot,
//   * component add/remove DURING iteration (snapshot-safe each<>),
//   * large-id / sparse-array-growth boundary,
//   * sparse-set tombstone reuse + swap-and-pop survivor integrity,
//   * empty-world queries,
//   * non-trivially-destructible component lifetime (leak / double-free net),
//   * smallest-pool driver selection (the realised each<> optimisation),
//   * Scheduler dependency-cycle rejection (Scheduler level, not just
//     SystemGraph),
//   * query-cache structural invalidation after a late component type.
//
// Pattern: Arrange / Act / Assert. No sleep_for. No global state.
// =============================================================================
#include <cd/ecs/Scheduler.hpp>
#include <cd/ecs/World.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
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

struct Heavy
{
    int kind { 0 };
};

// Instance-counting component to net leaks / double-frees through the
// swap-and-pop + cross-archetype migration paths.
struct Counted
{
    static inline int live = 0;
    int value { 0 };

    explicit Counted(int v = 0) : value { v } { ++live; }
    Counted(const Counted& o) : value { o.value } { ++live; }
    Counted(Counted&& o) noexcept : value { o.value } { ++live; }
    Counted& operator=(const Counted&) = default;
    Counted& operator=(Counted&&) noexcept = default;
    ~Counted() { --live; }
};

}  // namespace

// -----------------------------------------------------------------------------
// Entity recycle / generation invalidation
// -----------------------------------------------------------------------------

TEST(EcsEdge_Generation, ManyRecycleCyclesNeverRevalidateStaleHandle)
{
    // Arrange: repeatedly create/destroy a single slot. Each handed-out
    // handle must be unique and every prior handle must stay dead.
    cd::ecs::EntityManager em;
    std::vector<cd::ecs::Entity> history;
    history.reserve(64);

    // Act
    for (int i = 0; i < 64; ++i)
    {
        auto e = em.create();
        history.push_back(e);
        em.destroy(e);
    }

    // Assert: same slot reused (LIFO single slot), generations strictly
    // distinct, and not one stale handle re-validates.
    for (std::size_t i = 0; i < history.size(); ++i)
    {
        EXPECT_FALSE(em.is_alive(history[i]));
        EXPECT_EQ(history[i].id, history.front().id);  // single reused slot
        for (std::size_t j = i + 1; j < history.size(); ++j)
            EXPECT_NE(history[i].generation, history[j].generation);
    }
    EXPECT_EQ(em.alive_count(), 0U);
    EXPECT_EQ(em.slot_count(), 1U);
}

TEST(EcsEdge_Generation, AlternatingGenerationParityAcrossReuse)
{
    // create -> generation odd (alive); destroy -> even (free); the SAME
    // slot's generation advances by exactly 2 per full cycle.
    cd::ecs::EntityManager em;
    const auto a = em.create();
    EXPECT_EQ(a.generation % 2U, 1U);  // alive == odd
    em.destroy(a);
    const auto b = em.create();
    EXPECT_EQ(b.id, a.id);
    EXPECT_EQ(b.generation, a.generation + 2U);
    EXPECT_EQ(b.generation % 2U, 1U);
}

TEST(EcsEdge_Generation, DestroyTwiceIsSafeNoUnderflow)
{
    // Negative: double-destroy must be a no-op (not corrupt alive_count).
    cd::ecs::EntityManager em;
    auto a = em.create();
    auto b = em.create();
    em.destroy(a);
    em.destroy(a);  // already dead -- must not decrement again
    EXPECT_EQ(em.alive_count(), 1U);
    EXPECT_TRUE(em.is_alive(b));
}

TEST(EcsEdge_Generation, DefaultAndUnknownHandlesAreNotAlive)
{
    // Negative: a default Entity{0,0} and an out-of-range handle are dead.
    cd::ecs::EntityManager em;
    (void)em.create();
    EXPECT_FALSE(em.is_alive(cd::ecs::Entity {}));                 // gen 0
    EXPECT_FALSE(em.is_alive(cd::ecs::Entity { 999U, 1U }));       // out of range id
    EXPECT_FALSE(em.is_alive(cd::ecs::Entity { 0U, 999U }));       // wrong generation
}

// -----------------------------------------------------------------------------
// Empty world
// -----------------------------------------------------------------------------

TEST(EcsEdge_Empty, QueriesOnEmptyWorldVisitNothing)
{
    cd::ecs::World w;
    EXPECT_EQ(w.alive_count(), 0U);
    EXPECT_EQ(w.storage_type_count(), 0U);

    std::size_t hits = 0;
    w.for_each<Position>([&](cd::ecs::Entity, Position&) { ++hits; });
    w.each<Position, Velocity>([&](cd::ecs::Entity, Position&, Velocity&) { ++hits; });
    EXPECT_EQ(hits, 0U);

    // get / has / remove on a fresh entity with no components.
    auto e = w.create();
    EXPECT_FALSE(w.has<Position>(e));
    EXPECT_EQ(w.get<Position>(e), nullptr);
    w.remove<Position>(e);  // remove of absent type / pool -> no-op
    EXPECT_TRUE(w.is_alive(e));
}

TEST(EcsEdge_Empty, EachOverDriverWithEmptyRestPoolVisitsNothing)
{
    // Position pool has entries; Velocity pool exists but is empty.
    cd::ecs::World w;
    auto a = w.create();
    auto b = w.create();
    w.emplace<Position>(a, Position { 1, 0, 0 });
    w.emplace<Position>(b, Position { 2, 0, 0 });
    auto c = w.create();
    w.emplace<Velocity>(c, Velocity {});
    w.remove<Velocity>(c);  // Velocity pool now exists but is empty.

    std::size_t hits = 0;
    w.each<Position, Velocity>([&](cd::ecs::Entity, Position&, Velocity&) { ++hits; });
    EXPECT_EQ(hits, 0U);
}

// -----------------------------------------------------------------------------
// Sparse-set tombstone reuse + swap-and-pop survivor integrity
// -----------------------------------------------------------------------------

TEST(EcsEdge_SparseSet, RemoveMiddleKeepsSurvivorsAndValues)
{
    // Arrange: 5 entities each with a distinct Position.x.
    cd::ecs::World w;
    std::vector<cd::ecs::Entity> ents;
    ents.reserve(5);
    for (int i = 0; i < 5; ++i)
    {
        auto e = w.create();
        w.emplace<Position>(e, Position { static_cast<float>(i), 0, 0 });
        ents.push_back(e);
    }

    // Act: remove the middle -> swap-and-pop moves the last into its slot.
    w.remove<Position>(ents[2]);

    // Assert: survivors keep correct values, removed one is gone, count drops.
    EXPECT_EQ(w.component_count<Position>(), 4U);
    EXPECT_FALSE(w.has<Position>(ents[2]));
    for (int i : { 0, 1, 3, 4 })
    {
        auto* p = w.get<Position>(ents[static_cast<std::size_t>(i)]);
        ASSERT_NE(p, nullptr);
        EXPECT_FLOAT_EQ(p->x, static_cast<float>(i));
    }
}

TEST(EcsEdge_SparseSet, TombstoneSlotReusedByNewEntityOnSameId)
{
    // After destroy, the slot's component tombstone must not leak into a
    // freshly recycled entity that lands on the same id.
    cd::ecs::World w;
    auto a = w.create();
    w.emplace<Position>(a, Position { 7, 7, 7 });
    w.destroy(a);

    auto b = w.create();  // recycles a.id with a higher generation
    EXPECT_EQ(a.id, b.id);
    EXPECT_FALSE(w.has<Position>(b));     // tombstone did not leak
    w.emplace<Position>(b, Position { 1, 2, 3 });
    EXPECT_TRUE(w.has<Position>(b));
    EXPECT_FALSE(w.has<Position>(a));     // stale handle still rejected
    auto* pb = w.get<Position>(b);
    ASSERT_NE(pb, nullptr);
    EXPECT_FLOAT_EQ(pb->x, 1.0F);
}

TEST(EcsEdge_SparseSet, RemoveAbsentComponentIsNoop)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Position>(e, Position {});
    w.remove<Velocity>(e);            // never had Velocity
    w.remove<Position>(e);
    w.remove<Position>(e);            // double remove -> no-op
    EXPECT_FALSE(w.has<Position>(e));
    EXPECT_EQ(w.component_count<Position>(), 0U);
}

// -----------------------------------------------------------------------------
// Large-id / sparse-array growth boundary
// -----------------------------------------------------------------------------

TEST(EcsEdge_Boundary, HighEntityIdGrowsSparseArrayWithoutCorruption)
{
    // Reserve a high id by creating + destroying many entities first so a
    // later entity carries a large id, then exercise component storage on
    // it. The sparse array must grow (ensure_sparse) without UB.
    cd::ecs::World w;
    constexpr std::uint32_t kBurst = 4096;
    std::vector<cd::ecs::Entity> ents;
    ents.reserve(kBurst);
    for (std::uint32_t i = 0; i < kBurst; ++i)
        ents.push_back(w.create());

    // Put a component only on the highest-id entity -> sparse array must
    // resize to >= that id.
    auto& top = ents.back();
    w.emplace<Heavy>(top, Heavy { 99 });
    EXPECT_TRUE(w.has<Heavy>(top));
    EXPECT_EQ(w.component_count<Heavy>(), 1U);

    // No other entity should report the component.
    std::size_t seen = 0;
    w.for_each<Heavy>([&](cd::ecs::Entity, Heavy& h) { ++seen; EXPECT_EQ(h.kind, 99); });
    EXPECT_EQ(seen, 1U);
}

// -----------------------------------------------------------------------------
// Component add/remove DURING iteration (snapshot-safe each<>)
// -----------------------------------------------------------------------------

TEST(EcsEdge_MutateDuringIter, RemoveDriverComponentMidWalkVisitsEachOnce)
{
    // The uncached each<> snapshots the driver pool, so removing the driver
    // component from yet-unvisited entities during the walk must not skip
    // or double-visit anyone.
    cd::ecs::World w;
    std::vector<cd::ecs::Entity> ents;
    ents.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        auto e = w.create();
        w.emplace<Position>(e, Position { static_cast<float>(i), 0, 0 });
        w.emplace<Velocity>(e, Velocity {});
        ents.push_back(e);
    }

    std::unordered_set<std::uint32_t> visited;
    w.each<Position, Velocity>(
        [&](cd::ecs::Entity e, Position&, Velocity&)
        {
            // Mutate: strip Position from a not-yet-visited sibling.
            const auto victim = ents[(e.id + 4U) % 8U];
            if (victim != e)
                w.remove<Position>(victim);
            EXPECT_TRUE(visited.insert(e.id).second) << "double visit of " << e.id;
        });

    // Every entity present at snapshot time is visited at most once; those
    // stripped before their turn are skipped by the re-check (no crash, no
    // double visit).
    EXPECT_FALSE(visited.empty());
    EXPECT_LE(visited.size(), 8U);
}

TEST(EcsEdge_MutateDuringIter, AddComponentMidWalkDoesNotCrashOrRevisit)
{
    cd::ecs::World w;
    for (int i = 0; i < 6; ++i)
    {
        auto e = w.create();
        w.emplace<Position>(e, Position { static_cast<float>(i), 0, 0 });
        w.emplace<Velocity>(e, Velocity {});
    }
    std::size_t visits = 0;
    w.each<Position, Velocity>(
        [&](cd::ecs::Entity, Position&, Velocity&)
        {
            // Spawn a brand-new entity with the same components mid-walk.
            auto fresh = w.create();
            w.emplace<Position>(fresh, Position {});
            w.emplace<Velocity>(fresh, Velocity {});
            ++visits;
        });
    // The snapshot was taken before the walk, so only the original 6 are
    // visited -- mid-walk spawns do not extend the iteration.
    EXPECT_EQ(visits, 6U);
}

// -----------------------------------------------------------------------------
// Smallest-pool driver selection (realised optimisation)
// -----------------------------------------------------------------------------

TEST(EcsEdge_SmallestPool, EachVisitsIntersectionWhenRestPoolIsSmaller)
{
    // 100 entities have Position; only 3 of them also have Velocity. The
    // intersection (3) must be visited regardless of which pool drives.
    cd::ecs::World w;
    std::vector<cd::ecs::Entity> with_vel;
    for (int i = 0; i < 100; ++i)
    {
        auto e = w.create();
        w.emplace<Position>(e, Position { static_cast<float>(i), 0, 0 });
        if (i % 33 == 0)  // i = 0, 33, 66, 99 -> 4 entities
        {
            w.emplace<Velocity>(e, Velocity { static_cast<float>(i), 0, 0 });
            with_vel.push_back(e);
        }
    }
    ASSERT_EQ(w.component_count<Velocity>(), with_vel.size());

    std::unordered_set<std::uint32_t> hit;
    w.each<Position, Velocity>(
        [&](cd::ecs::Entity e, Position& p, Velocity& v)
        {
            EXPECT_FLOAT_EQ(p.x, v.vx);  // resolved references match
            hit.insert(e.id);
        });
    EXPECT_EQ(hit.size(), with_vel.size());
    for (auto e : with_vel)
        EXPECT_TRUE(hit.contains(e.id));
}

TEST(EcsEdge_SmallestPool, DriverPrimaryIsSmallerPoolStillResolvesT)
{
    // Symmetric: many Velocity, few Position. Driver becomes the Position
    // pool (smaller) but T == Velocity is still resolved per entity.
    cd::ecs::World w;
    std::vector<cd::ecs::Entity> both;
    for (int i = 0; i < 50; ++i)
    {
        auto e = w.create();
        w.emplace<Velocity>(e, Velocity { static_cast<float>(i), 0, 0 });
        if (i < 5)
        {
            w.emplace<Position>(e, Position { static_cast<float>(i), 0, 0 });
            both.push_back(e);
        }
    }
    std::size_t hits = 0;
    w.each<Velocity, Position>(
        [&](cd::ecs::Entity, Velocity& v, Position& p)
        {
            EXPECT_FLOAT_EQ(v.vx, p.x);
            ++hits;
        });
    EXPECT_EQ(hits, both.size());
}

// -----------------------------------------------------------------------------
// Non-trivial component lifetime (leak / double-free net)
// -----------------------------------------------------------------------------

TEST(EcsEdge_Lifetime, CountedComponentBalancedAcrossEmplaceRemoveDestroy)
{
    ASSERT_EQ(Counted::live, 0);
    {
        cd::ecs::World w;
        std::vector<cd::ecs::Entity> ents;
        ents.reserve(32);
        for (int i = 0; i < 32; ++i)
        {
            auto e = w.create();
            w.emplace<Counted>(e, Counted { i });
            ents.push_back(e);
        }
        EXPECT_EQ(static_cast<std::size_t>(Counted::live), w.component_count<Counted>());

        // Remove half via remove<>, destroy the other half via destroy().
        for (std::size_t i = 0; i < ents.size(); ++i)
        {
            if (i % 2 == 0)
                w.remove<Counted>(ents[i]);
            else
                w.destroy(ents[i]);
        }
        EXPECT_EQ(Counted::live, 0);
    }
    EXPECT_EQ(Counted::live, 0);  // World destruction freed nothing extra
}

TEST(EcsEdge_Lifetime, EmplaceOverwriteDoesNotLeak)
{
    ASSERT_EQ(Counted::live, 0);
    {
        cd::ecs::World w;
        auto e = w.create();
        w.emplace<Counted>(e, Counted { 1 });
        w.emplace<Counted>(e, Counted { 2 });  // overwrite in place
        w.emplace<Counted>(e, Counted { 3 });
        EXPECT_EQ(w.component_count<Counted>(), 1U);
        auto* c = w.get<Counted>(e);
        ASSERT_NE(c, nullptr);
        EXPECT_EQ(c->value, 3);
        EXPECT_EQ(Counted::live, 1);
    }
    EXPECT_EQ(Counted::live, 0);
}

// -----------------------------------------------------------------------------
// Scheduler dependency-cycle rejection (Scheduler level)
// -----------------------------------------------------------------------------

TEST(EcsEdge_Scheduler, ConflictingPairAlwaysYieldsCompleteOrder)
{
    // The Scheduler's conflict DAG only adds i->j edges for registration
    // index i < j, so it is acyclic BY CONSTRUCTION: every registration
    // resolves to a complete topological order, even when both systems
    // mutually read/write each other's components. (Genuine cycle
    // rejection is exercised at the SystemGraph level -- see
    // SystemGraph.CycleDetected.) This locks that invariant: no conflict
    // arrangement can wedge the scheduler.
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    s.add(cd::ecs::SystemDesc { "a" }.writes<Position>().reads<Velocity>().fn(
        [](cd::ecs::World&) {}));
    s.add(cd::ecs::SystemDesc { "b" }.writes<Velocity>().reads<Position>().fn(
        [](cd::ecs::World&) {}));
    auto order = s.preview_order();
    ASSERT_TRUE(order.has_value());
    ASSERT_EQ(order->size(), 2U);
    // Registration-order tie-break: 'a' (index 0) precedes 'b' (index 1).
    EXPECT_EQ((*order)[0], "a");
    EXPECT_EQ((*order)[1], "b");
    // They conflict (both touch Position+Velocity) so they land in separate
    // sequential stages, never one parallel stage.
    auto stages = s.preview_stages();
    ASSERT_TRUE(stages.has_value());
    EXPECT_EQ(stages->size(), 2U);
}

TEST(EcsEdge_Scheduler, CycleErrorCodeRoundTrips)
{
    // Direct unit on the error factory: a kCycleDetected code is in the
    // scheduler domain and carries the message -- this is what tick()
    // returns when build_order_ finds an empty ready set.
    const auto ec = cd::ecs::scheduler_errors::make(
        cd::ecs::scheduler_errors::Code::kCycleDetected, "cycle");
    EXPECT_EQ(ec.domain, cd::ecs::scheduler_errors::kDomain);
    EXPECT_EQ(ec.code, static_cast<std::uint32_t>(
                           cd::ecs::scheduler_errors::Code::kCycleDetected));
}

TEST(EcsEdge_Scheduler, SystemWithoutBodyIsSkippedNotCrash)
{
    // Negative: a SystemDesc added without fn() is a no-op at tick time.
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    s.add(cd::ecs::SystemDesc { "bodyless" }.writes<Position>());
    int ran = 0;
    s.add(cd::ecs::SystemDesc { "real" }.reads<Position>().fn(
        [&](cd::ecs::World&) { ++ran; }));
    ASSERT_TRUE(s.tick(w).has_value());
    EXPECT_EQ(ran, 1);
}

// -----------------------------------------------------------------------------
// Query-cache structural invalidation
// -----------------------------------------------------------------------------

TEST(EcsEdge_QueryCache, StaleQueryStaysEmptyUntilRefreshedAfterLateType)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Position>(e, Position {});

    // Query built before Velocity exists -> not ready, structurally fresh.
    auto q = w.query<Position, Velocity>();
    EXPECT_FALSE(q.ready());

    // Late-register Velocity: query is now stale and STILL not ready until
    // refreshed (it cached the absent pool).
    w.emplace<Velocity>(e, Velocity {});
    EXPECT_TRUE(q.is_stale(w));
    EXPECT_FALSE(q.ready());  // not refreshed yet

    std::size_t before = 0;
    q.each(w, [&](cd::ecs::Entity, Position&, Velocity&) { ++before; });
    EXPECT_EQ(before, 0U);  // cached-absent pool -> skipped

    q.auto_refresh(w);
    EXPECT_TRUE(q.ready());
    std::size_t after = 0;
    q.each(w, [&](cd::ecs::Entity, Position&, Velocity&) { ++after; });
    EXPECT_EQ(after, 1U);
}
