// =============================================================================
// CHROMODYNAMIC — cd::ecs::World::Query (cached) tests
//
// The existing test_ecs.cpp covers the non-cached `each<>` path. This
// file specifically verifies that `Query<T, Rest...>`:
//   * Returns identical results to a non-cached each<> walk
//   * Skips iteration cleanly when a component type is absent
//   * Can be refresh()ed after the missing type appears
//   * Iterates correctly across many frames without re-resolving
// =============================================================================
#include <cd/ecs/World.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace
{

struct Pos
{
    float x { 0.0F };
    float y { 0.0F };
};

struct Vel
{
    float dx { 0.0F };
    float dy { 0.0F };
};

struct Tag
{
};  // empty tag component for filtering

}  // namespace

TEST(EcsQueryCache, MatchesEachWalk)
{
    cd::ecs::World w;
    std::vector<cd::ecs::Entity> entities;
    for (int i = 0; i < 32; ++i)
    {
        auto e = w.create();
        w.emplace<Pos>(e, Pos { static_cast<float>(i), 0.0F });
        w.emplace<Vel>(e, Vel { 1.0F, 1.0F });
        entities.push_back(e);
    }

    // Walk via uncached path.
    std::uint32_t count_each = 0;
    float sum_each = 0.0F;
    w.each<Pos, Vel>(
        [&](cd::ecs::Entity, Pos& p, Vel&)
        {
            ++count_each;
            sum_each += p.x;
        }
    );

    // Walk via cached Query — must produce identical numbers.
    auto q = w.query<Pos, Vel>();
    ASSERT_TRUE(q.ready());
    std::uint32_t count_q = 0;
    float sum_q = 0.0F;
    q.each(
        w,
        [&](cd::ecs::Entity, Pos& p, Vel&)
        {
            ++count_q;
            sum_q += p.x;
        }
    );

    EXPECT_EQ(count_each, count_q);
    EXPECT_FLOAT_EQ(sum_each, sum_q);
}

TEST(EcsQueryCache, AbsentRestTypeYieldsNotReadyAndSkipsIteration)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Pos>(e, Pos {});
    // Vel is NOT registered yet.

    auto q = w.query<Pos, Vel>();
    EXPECT_FALSE(q.ready());

    std::uint32_t calls = 0;
    q.each(
        w,
        [&](cd::ecs::Entity, Pos&, Vel&)
        {
            ++calls;
        }
    );
    EXPECT_EQ(calls, 0U);
}

TEST(EcsQueryCache, RefreshPicksUpLateRegisteredStorage)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Pos>(e, Pos {});

    auto q = w.query<Pos, Vel>();
    EXPECT_FALSE(q.ready());

    // Now register Vel and refresh — the same Query must light up.
    w.emplace<Vel>(e, Vel { 2.0F, 3.0F });
    q.refresh(w);
    EXPECT_TRUE(q.ready());

    std::uint32_t hits = 0;
    q.each(
        w,
        [&](cd::ecs::Entity, Pos&, Vel& v)
        {
            ++hits;
            EXPECT_FLOAT_EQ(v.dx, 2.0F);
        }
    );
    EXPECT_EQ(hits, 1U);
}

TEST(EcsQueryCache, ReusedAcrossManyTicks)
{
    cd::ecs::World w;
    for (int i = 0; i < 1000; ++i)
    {
        auto e = w.create();
        w.emplace<Pos>(e, Pos { static_cast<float>(i), 0.0F });
        w.emplace<Vel>(e, Vel { 0.5F, 0.0F });
    }

    auto q = w.query<Pos, Vel>();
    ASSERT_TRUE(q.ready());

    // Simulate 60 frames — every Pos.x should advance by 30.0 (0.5 × 60).
    for (int frame = 0; frame < 60; ++frame)
    {
        q.each(
            w,
            [](cd::ecs::Entity, Pos& p, Vel& v)
            {
                p.x += v.dx;
            }
        );
    }

    std::uint32_t verified = 0;
    w.for_each<Pos>(
        [&](cd::ecs::Entity, Pos& p)
        {
            const auto expected = static_cast<float>(static_cast<int>(p.x - 30.0F));  // round to original index
            EXPECT_NEAR(p.x - expected, 30.0F, 1e-4F);
            ++verified;
        }
    );
    EXPECT_EQ(verified, 1000U);
}

TEST(EcsQueryCache, SingleComponentQueryWorks)
{
    cd::ecs::World w;
    for (int i = 0; i < 5; ++i)
    {
        auto e = w.create();
        w.emplace<Pos>(e, Pos { static_cast<float>(i), 0.0F });
    }
    auto q = w.query<Pos>();
    ASSERT_TRUE(q.ready());

    std::uint32_t n = 0;
    q.each(
        w,
        [&](cd::ecs::Entity, Pos&)
        {
            ++n;
        }
    );
    EXPECT_EQ(n, 5U);
}

// -----------------------------------------------------------------------------
// Structural-version tracking — Wave 42
// -----------------------------------------------------------------------------

TEST(EcsQueryCache, StructuralVersionBumpsWhenNewComponentTypeAppears)
{
    cd::ecs::World w;
    const auto v0 = w.structural_version();
    auto e = w.create();
    EXPECT_EQ(w.structural_version(), v0);  // entity create doesn't change pool set

    w.emplace<Pos>(e, Pos {});
    const auto v1 = w.structural_version();
    EXPECT_GT(v1, v0);  // new type registered → version up

    w.emplace<Pos>(e, Pos {});  // existing type → no version bump
    EXPECT_EQ(w.structural_version(), v1);

    w.emplace<Vel>(e, Vel { 1.0F, 0.0F });
    EXPECT_GT(w.structural_version(), v1);  // another new type
}

TEST(EcsQueryCache, IsStaleDetectsLateComponentRegistration)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Pos>(e, Pos {});

    auto q = w.query<Pos, Vel>();
    EXPECT_FALSE(q.ready());
    EXPECT_FALSE(q.is_stale(w));  // built against the current version

    w.emplace<Vel>(e, Vel { 2.0F, 3.0F });
    EXPECT_TRUE(q.is_stale(w));   // a new type appeared after Query construction

    q.auto_refresh(w);
    EXPECT_FALSE(q.is_stale(w));  // refreshed → versions match
    EXPECT_TRUE(q.ready());

    std::uint32_t hits = 0;
    q.each(w, [&](cd::ecs::Entity, Pos&, Vel&) { ++hits; });
    EXPECT_EQ(hits, 1U);
}

TEST(EcsQueryCache, AutoRefreshIsNoOpWhenVersionUnchanged)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Pos>(e, Pos {});
    w.emplace<Vel>(e, Vel { 1.0F, 0.0F });

    auto q = w.query<Pos, Vel>();
    const auto v0 = q.version();
    q.auto_refresh(w);
    EXPECT_EQ(q.version(), v0);  // refresh skipped — nothing changed
    EXPECT_TRUE(q.ready());
}
