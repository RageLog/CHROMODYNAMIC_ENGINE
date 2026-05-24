// =============================================================================
// CHROMODYNAMIC — cd::ecs tests
// =============================================================================
#include <cd/ecs/World.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>

namespace
{

struct Position
{
    float x {}, y {}, z {};
};

struct Velocity
{
    float vx {}, vy {}, vz {};
};

struct Tag
{
};

struct Name
{
    std::string value;
};

// ---- Entity lifecycle ------------------------------------------------------

TEST(EcsEntity, CreateProducesDistinctAliveEntities)
{
    cd::ecs::EntityManager em;
    auto a = em.create();
    auto b = em.create();
    EXPECT_TRUE(a.is_valid());
    EXPECT_TRUE(b.is_valid());
    EXPECT_NE(a, b);
    EXPECT_TRUE(em.is_alive(a));
    EXPECT_TRUE(em.is_alive(b));
    EXPECT_EQ(em.alive_count(), 2U);
}

TEST(EcsEntity, DestroyedHandleFailsIsAlive)
{
    cd::ecs::EntityManager em;
    auto a = em.create();
    em.destroy(a);
    EXPECT_FALSE(em.is_alive(a));
    EXPECT_EQ(em.alive_count(), 0U);
}

TEST(EcsEntity, SlotReusedWithDifferentGeneration)
{
    cd::ecs::EntityManager em;
    auto a = em.create();
    em.destroy(a);
    auto b = em.create();
    // Slot id should match (LIFO free list); generation must differ so the
    // stale handle `a` cannot accidentally validate against `b`.
    EXPECT_EQ(a.id, b.id);
    EXPECT_NE(a.generation, b.generation);
    EXPECT_FALSE(em.is_alive(a));
    EXPECT_TRUE(em.is_alive(b));
}

TEST(EcsEntity, ForEachVisitsAliveOnly)
{
    cd::ecs::EntityManager em;
    auto a = em.create();
    auto b = em.create();
    auto c = em.create();
    em.destroy(b);
    std::size_t seen = 0;
    em.for_each(
        [&](cd::ecs::Entity e)
        {
            EXPECT_TRUE(em.is_alive(e));
            EXPECT_TRUE(e == a || e == c);
            (void)e;
            ++seen;
        }
    );
    EXPECT_EQ(seen, 2U);
}

// ---- World + Components ----------------------------------------------------

TEST(EcsWorld, EmplaceGetRemove)
{
    cd::ecs::World w;
    auto e = w.create();
    auto& p = w.emplace<Position>(e, 1.0F, 2.0F, 3.0F);
    EXPECT_EQ(p.x, 1.0F);
    EXPECT_EQ(p.y, 2.0F);
    EXPECT_TRUE(w.has<Position>(e));
    auto* got = w.get<Position>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->z, 3.0F);
    w.remove<Position>(e);
    EXPECT_FALSE(w.has<Position>(e));
    EXPECT_EQ(w.get<Position>(e), nullptr);
}

TEST(EcsWorld, EmplaceOverwritesExisting)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Position>(e, 1.0F, 2.0F, 3.0F);
    w.emplace<Position>(e, 9.0F, 8.0F, 7.0F);
    auto* p = w.get<Position>(e);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->x, 9.0F);
    EXPECT_EQ(w.component_count<Position>(), 1U);  // not duplicated
}

TEST(EcsWorld, DestroyStripsAllComponents)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Position>(e, 0.0F, 0.0F, 0.0F);
    w.emplace<Velocity>(e, 1.0F, 0.0F, 0.0F);
    EXPECT_EQ(w.component_count<Position>(), 1U);
    EXPECT_EQ(w.component_count<Velocity>(), 1U);
    w.destroy(e);
    EXPECT_FALSE(w.is_alive(e));
    EXPECT_EQ(w.component_count<Position>(), 0U);
    EXPECT_EQ(w.component_count<Velocity>(), 0U);
}

TEST(EcsWorld, StaleHandleRejectedAfterReuse)
{
    cd::ecs::World w;
    auto a = w.create();
    w.emplace<Position>(a, 1.0F, 2.0F, 3.0F);
    w.destroy(a);
    auto b = w.create();
    EXPECT_EQ(a.id, b.id);
    // Stale handle `a` must not see component data from `b`'s lifetime.
    EXPECT_FALSE(w.has<Position>(a));
    w.emplace<Position>(b, 9.0F, 9.0F, 9.0F);
    EXPECT_TRUE(w.has<Position>(b));
    EXPECT_FALSE(w.has<Position>(a));
}

TEST(EcsWorld, NonPodComponentStoredCorrectly)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Name>(e, Name { "hero" });
    auto* n = w.get<Name>(e);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->value, "hero");
}

// ---- Iteration / queries ---------------------------------------------------

TEST(EcsWorld, ForEachVisitsExactlyOnceInDenseOrder)
{
    cd::ecs::World w;
    auto a = w.create();
    auto b = w.create();
    auto c = w.create();
    w.emplace<Position>(a, 1.0F, 0.0F, 0.0F);
    w.emplace<Position>(b, 2.0F, 0.0F, 0.0F);
    w.emplace<Position>(c, 3.0F, 0.0F, 0.0F);

    std::size_t hits = 0;
    float sum = 0.0F;
    w.for_each<Position>(
        [&](cd::ecs::Entity, Position& p)
        {
            ++hits;
            sum += p.x;
        }
    );
    EXPECT_EQ(hits, 3U);
    EXPECT_FLOAT_EQ(sum, 6.0F);
}

TEST(EcsWorld, MultiComponentEachGatesOnMissing)
{
    cd::ecs::World w;
    auto a = w.create();
    auto b = w.create();
    auto c = w.create();
    w.emplace<Position>(a, 1.0F, 0.0F, 0.0F);
    w.emplace<Velocity>(a, 0.5F, 0.0F, 0.0F);
    w.emplace<Position>(b, 2.0F, 0.0F, 0.0F);
    // b has no Velocity → must not be visited.
    w.emplace<Velocity>(c, 1.0F, 1.0F, 1.0F);
    // c has no Position → must not be visited.

    std::size_t hits = 0;
    w.each<Position, Velocity>(
        [&](cd::ecs::Entity e, Position& p, Velocity& v)
        {
            ++hits;
            EXPECT_EQ(e, a);
            EXPECT_FLOAT_EQ(p.x, 1.0F);
            EXPECT_FLOAT_EQ(v.vx, 0.5F);
        }
    );
    EXPECT_EQ(hits, 1U);
}

TEST(EcsWorld, MultiComponentEachEmptyWhenAnyPoolMissing)
{
    cd::ecs::World w;
    auto a = w.create();
    w.emplace<Position>(a, 1.0F, 0.0F, 0.0F);
    std::size_t hits = 0;
    w.each<Position, Velocity>(
        [&](cd::ecs::Entity, Position&, Velocity&)
        {
            ++hits;
        }
    );
    EXPECT_EQ(hits, 0U);  // Velocity pool never created
}

TEST(EcsWorld, BulkLifecycleStress)
{
    // Create/destroy thousands of entities and verify the slot/generation
    // bookkeeping stays correct. Sparse-set storage should keep size bounded
    // (no leaked component slots after destroy()).
    cd::ecs::World w;
    constexpr std::uint32_t kCount = 1024;
    std::vector<cd::ecs::Entity> ents;
    ents.reserve(kCount);
    for (std::uint32_t i = 0; i < kCount; ++i)
    {
        auto e = w.create();
        w.emplace<Position>(e, static_cast<float>(i), 0.0F, 0.0F);
        ents.push_back(e);
    }
    EXPECT_EQ(w.alive_count(), kCount);
    EXPECT_EQ(w.component_count<Position>(), kCount);

    // Destroy every other entity.
    for (std::uint32_t i = 0; i < kCount; i += 2)
    {
        w.destroy(ents[i]);
    }
    EXPECT_EQ(w.alive_count(), kCount / 2);
    EXPECT_EQ(w.component_count<Position>(), kCount / 2);

    // Iterating must only visit survivors.
    std::size_t seen = 0;
    w.for_each<Position>(
        [&](cd::ecs::Entity, Position&)
        {
            ++seen;
        }
    );
    EXPECT_EQ(seen, kCount / 2);
}

}  // namespace

#include <cd/ecs/TagHelpers.hpp>

namespace {
struct Selected {};
struct Disabled {};
}

TEST(TagHelpers, TagThenHasReportsTrue)
{
    cd::ecs::World w;
    auto e = w.create();
    EXPECT_FALSE(cd::ecs::has_tag<Selected>(w, e));
    cd::ecs::tag<Selected>(w, e);
    EXPECT_TRUE(cd::ecs::has_tag<Selected>(w, e));
}

TEST(TagHelpers, UntagRemovesTag)
{
    cd::ecs::World w;
    auto e = w.create();
    cd::ecs::tag<Disabled>(w, e);
    EXPECT_TRUE(cd::ecs::has_tag<Disabled>(w, e));
    cd::ecs::untag<Disabled>(w, e);
    EXPECT_FALSE(cd::ecs::has_tag<Disabled>(w, e));
}

TEST(TagHelpers, MultipleTagsCoexist)
{
    cd::ecs::World w;
    auto e = w.create();
    cd::ecs::tag<Selected>(w, e);
    cd::ecs::tag<Disabled>(w, e);
    EXPECT_TRUE(cd::ecs::has_tag<Selected>(w, e));
    EXPECT_TRUE(cd::ecs::has_tag<Disabled>(w, e));
}

#include <cd/ecs/EntityRange.hpp>

TEST(EntityRange, EmptyInputReturnsZeroPages)
{
    std::vector<cd::ecs::Entity> all;
    auto p = cd::ecs::paginate(all, 10, 0);
    EXPECT_EQ(p.total, 0u);
    EXPECT_EQ(p.page_count, 0u);
    EXPECT_TRUE(p.entities.empty());
}

TEST(EntityRange, SinglePageFits)
{
    std::vector<cd::ecs::Entity> all;
    for (std::uint32_t i = 0; i < 3; ++i) all.push_back(cd::ecs::Entity { i, 1 });
    auto p = cd::ecs::paginate(all, 10, 0);
    EXPECT_EQ(p.total, 3u);
    EXPECT_EQ(p.page_count, 1u);
    EXPECT_EQ(p.entities.size(), 3u);
}

TEST(EntityRange, MultiPagePartitions)
{
    std::vector<cd::ecs::Entity> all;
    for (std::uint32_t i = 0; i < 25; ++i) all.push_back(cd::ecs::Entity { i, 1 });
    auto p0 = cd::ecs::paginate(all, 10, 0);
    auto p1 = cd::ecs::paginate(all, 10, 1);
    auto p2 = cd::ecs::paginate(all, 10, 2);
    EXPECT_EQ(p0.entities.size(), 10u);
    EXPECT_EQ(p1.entities.size(), 10u);
    EXPECT_EQ(p2.entities.size(), 5u);
    EXPECT_EQ(p0.page_count, 3u);
}

TEST(EntityRange, OutOfRangeClampsToLastPage)
{
    std::vector<cd::ecs::Entity> all;
    for (std::uint32_t i = 0; i < 5; ++i) all.push_back(cd::ecs::Entity { i, 1 });
    auto p = cd::ecs::paginate(all, 2, 99);
    EXPECT_EQ(p.page_count, 3u);
    EXPECT_EQ(p.page_index, 2u);
}

#include <cd/ecs/Lifecycle.hpp>

TEST(LifecycleRegistry, OnCreatedFiresForEveryHandler)
{
    cd::ecs::LifecycleRegistry r;
    int hits = 0;
    r.on_created([&](cd::ecs::Entity) { ++hits; });
    r.on_created([&](cd::ecs::Entity) { ++hits; });
    r.notify_created(cd::ecs::Entity { 1, 1 });
    EXPECT_EQ(hits, 2);
}

TEST(LifecycleRegistry, OnDestroyedSeparateFromCreated)
{
    cd::ecs::LifecycleRegistry r;
    int created = 0;
    int destroyed = 0;
    r.on_created([&](cd::ecs::Entity) { ++created; });
    r.on_destroyed([&](cd::ecs::Entity) { ++destroyed; });
    r.notify_created(cd::ecs::Entity { 1, 1 });
    EXPECT_EQ(created, 1);
    EXPECT_EQ(destroyed, 0);
    r.notify_destroyed(cd::ecs::Entity { 1, 1 });
    EXPECT_EQ(created, 1);
    EXPECT_EQ(destroyed, 1);
}

TEST(LifecycleRegistry, RemoveByHandleId)
{
    cd::ecs::LifecycleRegistry r;
    int hits = 0;
    auto id = r.on_created([&](cd::ecs::Entity) { ++hits; });
    EXPECT_TRUE(r.remove(id));
    r.notify_created(cd::ecs::Entity { 1, 1 });
    EXPECT_EQ(hits, 0);
    EXPECT_FALSE(r.remove(id));  // already gone
}

TEST(LifecycleRegistry, HandlerCountsAccurate)
{
    cd::ecs::LifecycleRegistry r;
    r.on_created([](cd::ecs::Entity) {});
    r.on_destroyed([](cd::ecs::Entity) {});
    r.on_destroyed([](cd::ecs::Entity) {});
    EXPECT_EQ(r.created_handler_count(), 1u);
    EXPECT_EQ(r.destroyed_handler_count(), 2u);
}
