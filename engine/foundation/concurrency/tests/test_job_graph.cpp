// =============================================================================
// CHROMODYNAMIC - cd::concurrency::JobGraph tests (Phase 283 / W8 tail)
//
// Closes the JobGraph zero-tests gap identified by
// ADR-20260528-job-system-design section D4. Covers diamond DAG, linear
// chain, wide fan-out/fan-in, cycle rejection, out-of-range dep rejection,
// empty graph, self-loop.
// =============================================================================
#include <cd/concurrency/JobGraph.hpp>
#include <cd/concurrency/ThreadPool.hpp>
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

TEST(JobGraph, DiamondDependencyExecutesInOrder)
{
    using clock = std::chrono::steady_clock;
    cd::concurrency::ThreadPool pool { 4 };
    cd::concurrency::JobGraph   g;

    std::atomic<std::int64_t> start_a { 0 };
    std::atomic<std::int64_t> start_b { 0 };
    std::atomic<std::int64_t> start_c { 0 };
    std::atomic<std::int64_t> start_d { 0 };
    std::atomic<std::int64_t> end_a   { 0 };
    std::atomic<std::int64_t> end_b   { 0 };
    std::atomic<std::int64_t> end_c   { 0 };

    auto stamp = [](std::atomic<std::int64_t>& out)
    {
        out.store(clock::now().time_since_epoch().count(), std::memory_order_release);
    };

    const auto a = g.add(
        [&]
        {
            stamp(start_a);
            std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
            stamp(end_a);
        }
    );
    const auto b = g.add(
        [&]
        {
            stamp(start_b);
            std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
            stamp(end_b);
        },
        { a }
    );
    const auto c = g.add(
        [&]
        {
            stamp(start_c);
            std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
            stamp(end_c);
        },
        { a }
    );
    g.add([&] { stamp(start_d); }, { b, c });

    ASSERT_TRUE(g.run(pool));

    EXPECT_GE(start_b.load(), end_a.load());
    EXPECT_GE(start_c.load(), end_a.load());
    EXPECT_GE(start_d.load(), end_b.load());
    EXPECT_GE(start_d.load(), end_c.load());

    EXPECT_EQ(g.failed_nodes(), 0u);
}

}  // namespace

TEST(JobGraph, LinearChainPreservesOrder)
{
    cd::concurrency::ThreadPool pool { 4 };
    cd::concurrency::JobGraph   g;

    std::mutex             order_mutex;
    std::vector<int>       order;

    const auto a = g.add(
        [&] { std::lock_guard k { order_mutex }; order.push_back(0); }
    );
    const auto b = g.add(
        [&] { std::lock_guard k { order_mutex }; order.push_back(1); },
        { a }
    );
    const auto c = g.add(
        [&] { std::lock_guard k { order_mutex }; order.push_back(2); },
        { b }
    );
    g.add([&] { std::lock_guard k { order_mutex }; order.push_back(3); }, { c });

    ASSERT_TRUE(g.run(pool));

    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], 0);
    EXPECT_EQ(order[1], 1);
    EXPECT_EQ(order[2], 2);
    EXPECT_EQ(order[3], 3);
}

namespace
{
TEST(JobGraph, WideFanOutFanInCompletes)
{
    constexpr std::size_t kLeaves = 100;
    cd::concurrency::ThreadPool pool { 4 };
    cd::concurrency::JobGraph   g;

    std::atomic<int> leaf_count { 0 };
    std::atomic<int> sink_seen  { -1 };

    const auto src = g.add([] {});
    std::vector<cd::concurrency::JobId> leaves;
    leaves.reserve(kLeaves);
    for (std::size_t i = 0; i < kLeaves; ++i)
    {
        leaves.push_back(g.add(
            [&leaf_count] { leaf_count.fetch_add(1, std::memory_order_relaxed); },
            { src }
        ));
    }
    g.add([&] { sink_seen.store(leaf_count.load(), std::memory_order_release); }, leaves);

    ASSERT_TRUE(g.run(pool));

    EXPECT_EQ(leaf_count.load(), static_cast<int>(kLeaves));
    EXPECT_EQ(sink_seen.load(), static_cast<int>(kLeaves));
}

TEST(JobGraph, CycleIsRejected)
{
    cd::concurrency::ThreadPool pool { 2 };
    cd::concurrency::JobGraph   g;

    std::atomic<int> ran { 0 };
    g.add([&] { ran.fetch_add(1); }, { static_cast<cd::concurrency::JobId>(1) });
    g.add([&] { ran.fetch_add(1); }, { static_cast<cd::concurrency::JobId>(0) });

    EXPECT_FALSE(g.run(pool));
    EXPECT_EQ(ran.load(), 0);
}

TEST(JobGraph, InvalidDepIsRejected)
{
    cd::concurrency::ThreadPool pool { 2 };
    cd::concurrency::JobGraph   g;

    std::atomic<int> ran { 0 };
    g.add([&] { ran.fetch_add(1); });
    g.add([&] { ran.fetch_add(1); }, { static_cast<cd::concurrency::JobId>(99) });

    EXPECT_FALSE(g.run(pool));
    EXPECT_EQ(ran.load(), 0);
}

TEST(JobGraph, EmptyGraphReturnsTrueImmediately)
{
    cd::concurrency::ThreadPool pool { 2 };
    cd::concurrency::JobGraph   g;
    EXPECT_TRUE(g.run(pool));
    EXPECT_EQ(g.size(), 0u);
}

TEST(JobGraph, SelfLoopIsRejected)
{
    cd::concurrency::ThreadPool pool { 2 };
    cd::concurrency::JobGraph   g;

    std::atomic<int> ran { 0 };
    g.add([&] { ran.fetch_add(1); }, { static_cast<cd::concurrency::JobId>(0) });

    EXPECT_FALSE(g.run(pool));
    EXPECT_EQ(ran.load(), 0);
}

TEST(JobGraph, DisjointChainsAllComplete)
{
    cd::concurrency::ThreadPool pool { 4 };
    cd::concurrency::JobGraph   g;

    constexpr int kChains = 8;
    constexpr int kLen    = 4;
    std::atomic<int> completed { 0 };

    for (int c = 0; c < kChains; ++c)
    {
        cd::concurrency::JobId prev = g.add(
            [&] { completed.fetch_add(1, std::memory_order_relaxed); }
        );
        for (int i = 1; i < kLen; ++i)
        {
            prev = g.add(
                [&] { completed.fetch_add(1, std::memory_order_relaxed); },
                { prev }
            );
        }
    }

    ASSERT_TRUE(g.run(pool));
    EXPECT_EQ(completed.load(), kChains * kLen);
}

// ============================================================================
// X1C (phase 285) integration test: hello_engine boot DAG topology.
// Mirrors the 7-node bake graph:
//   A env_cube -> { B diff_irradiance, C spec_prefilter }
//   D brdf_lut, E earth_albedo, F earth_normal, G earth_mr   (independent roots)
// Uses cd::concurrency::WorkStealingThreadPool to exercise the templated
// JobGraph::run<PoolT> path introduced by X1C (was hard-bound to ThreadPool).
// ============================================================================
TEST(JobGraph, BootBakeTopologyOnWorkStealingPool)
{
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    cd::concurrency::JobGraph               g;

    std::atomic<int>  env_done { 0 };
    std::atomic<int>  ran_b_after_env { 0 };
    std::atomic<int>  ran_c_after_env { 0 };
    std::atomic<int>  d_count { 0 };
    std::atomic<int>  e_count { 0 };
    std::atomic<int>  f_count { 0 };
    std::atomic<int>  g_count { 0 };

    const auto a = g.add(
        [&]
        {
            // Simulate env_cube bake.
            std::this_thread::sleep_for(std::chrono::milliseconds { 4 });
            env_done.store(1, std::memory_order_release);
        });
    const auto b = g.add(
        [&]
        {
            // Diff must observe env_done==1.
            ran_b_after_env.store(env_done.load(std::memory_order_acquire),
                                  std::memory_order_release);
        },
        { a });
    const auto c = g.add(
        [&]
        {
            ran_c_after_env.store(env_done.load(std::memory_order_acquire),
                                  std::memory_order_release);
        },
        { a });
    const auto d = g.add([&] { d_count.fetch_add(1, std::memory_order_relaxed); });
    const auto e = g.add([&] { e_count.fetch_add(1, std::memory_order_relaxed); });
    const auto fnode = g.add([&] { f_count.fetch_add(1, std::memory_order_relaxed); });
    const auto gn = g.add([&] { g_count.fetch_add(1, std::memory_order_relaxed); });
    (void)b; (void)c; (void)d; (void)e; (void)fnode; (void)gn;

    ASSERT_TRUE(g.run(pool));
    EXPECT_EQ(g.failed_nodes(), 0U);
    EXPECT_EQ(ran_b_after_env.load(), 1);
    EXPECT_EQ(ran_c_after_env.load(), 1);
    EXPECT_EQ(d_count.load(), 1);
    EXPECT_EQ(e_count.load(), 1);
    EXPECT_EQ(f_count.load(), 1);
    EXPECT_EQ(g_count.load(), 1);
}

TEST(JobGraph, TemplatedRunWorksOnBothPoolTypes)
{
    // Compile-time validation that the templated run<PoolT> resolves
    // for both pool types added in X1C (phase 285).
    cd::concurrency::ThreadPool                 tp { 2 };
    cd::concurrency::WorkStealingThreadPool     wsp { 2 };
    cd::concurrency::JobGraph                   g1;
    cd::concurrency::JobGraph                   g2;
    std::atomic<int> tp_ran { 0 };
    std::atomic<int> wsp_ran { 0 };
    (void)g1.add([&] { tp_ran.fetch_add(1, std::memory_order_relaxed); });
    (void)g2.add([&] { wsp_ran.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(g1.run(tp));
    ASSERT_TRUE(g2.run(wsp));
    EXPECT_EQ(tp_ran.load(), 1);
    EXPECT_EQ(wsp_ran.load(), 1);
}

}  // namespace
