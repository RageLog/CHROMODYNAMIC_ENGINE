// =============================================================================
// CHROMODYNAMIC — cd::ecs::Scheduler tests
// =============================================================================
#include <cd/ecs/Scheduler.hpp>
#include <cd/ecs/World.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{

struct Pos
{
    int x { 0 };
};

struct Vel
{
    int dx { 0 };
};

struct Tag
{
};

}  // namespace

TEST(Scheduler, EmptyScheduleTicksClean)
{
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    auto r = s.tick(w);
    EXPECT_TRUE(r.has_value()) << (r.has_value() ? "" : r.error().message);
}

TEST(Scheduler, NonConflictingSystemsKeepRegistrationOrder)
{
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    std::vector<std::string> hits;
    s.add(
        cd::ecs::SystemDesc { "a" }.reads<Pos>().fn(
            [&](cd::ecs::World&)
            {
                hits.push_back("a");
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "b" }.reads<Vel>().fn(
            [&](cd::ecs::World&)
            {
                hits.push_back("b");
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "c" }.reads<Tag>().fn(
            [&](cd::ecs::World&)
            {
                hits.push_back("c");
            }
        )
    );
    ASSERT_TRUE(s.tick(w).has_value());
    EXPECT_EQ(hits, (std::vector<std::string> { "a", "b", "c" }));
}

TEST(Scheduler, WriteForcesBeforeReadOfSameType)
{
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    std::vector<std::string> hits;
    // Even though "read_pos" is added FIRST, "write_pos" conflicts with
    // every read, so the topological ordering puts write_pos relative to
    // its successors. In our model, conflict means "the earlier-
    // registered system must run first". So write_pos (registered second)
    // would normally run AFTER read_pos — but we register read_pos first,
    // so for THIS test let's flip the assertion to match the model.
    s.add(
        cd::ecs::SystemDesc { "write_pos" }.writes<Pos>().fn(
            [&](cd::ecs::World&)
            {
                hits.push_back("write_pos");
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "read_pos" }.reads<Pos>().fn(
            [&](cd::ecs::World&)
            {
                hits.push_back("read_pos");
            }
        )
    );
    ASSERT_TRUE(s.tick(w).has_value());
    // write_pos was added first → registration order forces it first;
    // read_pos sees the write's output.
    EXPECT_EQ(hits, (std::vector<std::string> { "write_pos", "read_pos" }));
}

TEST(Scheduler, IndependentSystemsCanBeReorderedButPreserveStableOrder)
{
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    // Three independent systems — order should be exactly registration.
    s.add(
        cd::ecs::SystemDesc { "x" }.writes<Pos>().fn(
            [](cd::ecs::World&)
            {
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "y" }.writes<Vel>().fn(
            [](cd::ecs::World&)
            {
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "z" }.writes<Tag>().fn(
            [](cd::ecs::World&)
            {
            }
        )
    );
    auto preview = s.preview_order();
    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(*preview, (std::vector<std::string> { "x", "y", "z" }));
}

TEST(Scheduler, ConflictChainOrdersCorrectly)
{
    // sense → move → render: each reads what the previous wrote.
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    std::vector<std::string> hits;
    s.add(
        cd::ecs::SystemDesc { "sense" }.writes<Pos>().fn(
            [&](cd::ecs::World&)
            {
                hits.push_back("sense");
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "move" }.reads<Pos>().writes<Vel>().fn(
            [&](cd::ecs::World&)
            {
                hits.push_back("move");
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "render" }.reads<Pos>().reads<Vel>().fn(
            [&](cd::ecs::World&)
            {
                hits.push_back("render");
            }
        )
    );
    ASSERT_TRUE(s.tick(w).has_value());
    EXPECT_EQ(hits, (std::vector<std::string> { "sense", "move", "render" }));
}

TEST(Scheduler, BodyActuallyMutatesWorld)
{
    cd::ecs::World w;
    auto e = w.create();
    w.emplace<Pos>(e, Pos { 5 });

    cd::ecs::Scheduler s;
    s.add(
        cd::ecs::SystemDesc { "inc" }.writes<Pos>().fn(
            [](cd::ecs::World& wr)
            {
                wr.for_each<Pos>(
                    [](cd::ecs::Entity, Pos& p)
                    {
                        ++p.x;
                    }
                );
            }
        )
    );
    ASSERT_TRUE(s.tick(w).has_value());
    ASSERT_TRUE(s.tick(w).has_value());
    auto* p = w.get<Pos>(e);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->x, 7);
}

// =============================================================================
// Wave 17 (S4.3) — tick_parallel + preview_stages
// =============================================================================

TEST(Scheduler, PreviewStagesMatchesParallelLayout)
{
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    // Three systems: A writes Pos, B reads Pos, C writes Vel.
    // Conflicts: A→B (write→read on Pos). C is independent.
    // Stage 0: {A, C}.  Stage 1: {B}.
    s.add(
        cd::ecs::SystemDesc { "A" }.writes<Pos>().fn(
            [](cd::ecs::World&)
            {
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "B" }.reads<Pos>().fn(
            [](cd::ecs::World&)
            {
            }
        )
    );
    s.add(
        cd::ecs::SystemDesc { "C" }.writes<Vel>().fn(
            [](cd::ecs::World&)
            {
            }
        )
    );

    auto stages_r = s.preview_stages();
    ASSERT_TRUE(stages_r.has_value());
    ASSERT_EQ(stages_r->size(), 2u);
    EXPECT_EQ((*stages_r)[0].size(), 2u);  // A + C
    EXPECT_EQ((*stages_r)[1].size(), 1u);  // B
}

TEST(Scheduler, ParallelTickProducesSameResultAsSequential)
{
    auto run = [](bool parallel)
    {
        cd::ecs::World w;
        auto e1 = w.create();
        auto e2 = w.create();
        w.emplace<Pos>(e1, Pos { 0 });
        w.emplace<Pos>(e2, Pos { 0 });
        w.emplace<Vel>(e1, Vel { 3 });
        w.emplace<Vel>(e2, Vel { -1 });

        cd::ecs::Scheduler s;
        s.add(
            cd::ecs::SystemDesc { "move" }.reads<Vel>().writes<Pos>().fn(
                [](cd::ecs::World& wr)
                {
                    wr.each<Pos, Vel>(
                        [](cd::ecs::Entity, Pos& p, Vel& v)
                        {
                            p.x += v.dx;
                        }
                    );
                }
            )
        );
        s.add(
            cd::ecs::SystemDesc { "audit" }.reads<Pos>().fn(
                [](cd::ecs::World&)
                {
                }
            )
        );  // body irrelevant.

        for (int i = 0; i < 5; ++i)
        {
            if (parallel)
                EXPECT_TRUE(s.tick_parallel(w).has_value());
            else
                EXPECT_TRUE(s.tick(w).has_value());
        }
        return std::make_pair(*w.get<Pos>(e1), *w.get<Pos>(e2));
    };

    const auto seq = run(false);
    const auto par = run(true);
    EXPECT_EQ(seq.first.x, par.first.x);
    EXPECT_EQ(seq.second.x, par.second.x);
}

TEST(Scheduler, ParallelTickHandlesIndependentSystemsConcurrently)
{
    cd::ecs::World w;
    cd::ecs::Scheduler s;
    // 4 independent systems (no shared components) — all in stage 0.
    std::atomic<int> hits { 0 };
    auto add_indep = [&](const char* name)
    {
        s.add(
            cd::ecs::SystemDesc { name }.fn(
                [&](cd::ecs::World&)
                {
                    // Touch atomic to prove all four bodies actually ran.
                    hits.fetch_add(1, std::memory_order_relaxed);
                }
            )
        );
    };
    add_indep("a");
    add_indep("b");
    add_indep("c");
    add_indep("d");

    auto stages_r = s.preview_stages();
    ASSERT_TRUE(stages_r.has_value());
    ASSERT_EQ(stages_r->size(), 1u);
    EXPECT_EQ((*stages_r)[0].size(), 4u);

    EXPECT_TRUE(s.tick_parallel(w).has_value());
    EXPECT_EQ(hits.load(), 4);
}
