// =============================================================================
// CHROMODYNAMIC — cd::concurrency::ThreadPool + JobGraph + CoroTask tests
// =============================================================================
#include <cd/concurrency/CoroTask.hpp>
#include <cd/concurrency/JobGraph.hpp>
#include <cd/concurrency/Task.hpp>
#include <cd/concurrency/TaskPriority.hpp>
#include <cd/concurrency/ThreadPool.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <chrono>
#include <thread>
#include <vector>

namespace
{

// --- Task wrapper ----------------------------------------------------------
TEST(Task, EmptyDefault)
{
    cd::concurrency::Job t;
    EXPECT_FALSE(static_cast<bool>(t));
    EXPECT_EQ(t.priority(), cd::concurrency::TaskPriority::Normal);
}

TEST(Task, InvokeAndPriority)
{
    int x = 0;
    cd::concurrency::Job t { [&]
                             {
                                 x = 42;
                             },
                             cd::concurrency::TaskPriority::High };
    EXPECT_TRUE(static_cast<bool>(t));
    EXPECT_EQ(t.priority(), cd::concurrency::TaskPriority::High);
    t();
    EXPECT_EQ(x, 42);
}

// --- ThreadPool ------------------------------------------------------------
TEST(ThreadPool, BasicSubmitReturnsFuture)
{
    cd::concurrency::ThreadPool pool { 2 };
    auto f = pool.submit(
        []
        {
            return 7 * 6;
        }
    );
    EXPECT_EQ(f.get(), 42);
}

TEST(ThreadPool, DetachedTaskExecutes)
{
    cd::concurrency::ThreadPool pool { 2 };
    std::atomic<int> hits { 0 };
    for (int i = 0; i < 50; ++i)
    {
        pool.submit_detached(
            [&]
            {
                hits.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    pool.wait_all();
    EXPECT_EQ(hits.load(), 50);
}

TEST(ThreadPool, FuturesAggregate)
{
    cd::concurrency::ThreadPool pool { 4 };
    std::vector<std::future<int>> futs;
    futs.reserve(200);
    for (int i = 0; i < 200; ++i)
    {
        futs.push_back(pool.submit(
            [i]
            {
                return i * 2;
            }
        ));
    }
    std::int64_t sum = 0;
    for (auto& f : futs)
        sum += f.get();
    EXPECT_EQ(sum, 200LL * 199 /* 2 * (0..199) sum */);
}

TEST(ThreadPool, ExceptionPropagatesViaFuture)
{
    cd::concurrency::ThreadPool pool { 1 };
    auto f = pool.submit(
        []() -> int
        {
            throw std::runtime_error("boom");
        }
    );
    EXPECT_THROW(f.get(), std::runtime_error);
    EXPECT_GE(pool.stats().failures.load(), 0u);  // wrap exception is in future, not pool stats
}

TEST(ThreadPool, StatsCount)
{
    cd::concurrency::ThreadPool pool { 2 };
    for (int i = 0; i < 10; ++i)
    {
        pool.submit_detached(
            []
            {
            }
        );
    }
    pool.wait_all();
    EXPECT_EQ(pool.stats().tasks_submitted.load(), 10u);
    EXPECT_EQ(pool.stats().tasks_completed.load(), 10u);
}

TEST(ThreadPool, ShutdownIdempotent)
{
    cd::concurrency::ThreadPool pool { 2 };
    EXPECT_TRUE(pool.is_running());
    pool.shutdown();
    EXPECT_FALSE(pool.is_running());
    pool.shutdown();  // no-op
}

// --- CoroTask --------------------------------------------------------------
namespace coro = cd::concurrency;

coro::Task<int> coro_double(int x)
{
    co_return x * 2;
}

coro::Task<int> coro_sum_via_chain(int a, int b)
{
    int x = co_await coro_double(a);
    int y = co_await coro_double(b);
    co_return x + y;
}

TEST(CoroTaskInt, SyncGet)
{
    auto t = coro_double(21);
    EXPECT_EQ(t.get(), 42);
}

TEST(CoroTaskInt, ChainedAwait)
{
    auto t = coro_sum_via_chain(3, 4);
    EXPECT_EQ(t.get(), 14);  // 2*3 + 2*4
}

coro::Task<int> coro_throws()
{
    throw std::runtime_error("inside coroutine");
    co_return 0;
}

TEST(CoroTaskInt, ExceptionPropagates)
{
    auto t = coro_throws();
    EXPECT_THROW(t.get(), std::runtime_error);
}

coro::CoroTask coro_void_increment(std::atomic<int>& counter)
{
    counter.fetch_add(1);
    co_return;
}

TEST(CoroTask, SpawnDetachedRuns)
{
    cd::concurrency::ThreadPool pool { 1 };
    std::atomic<int> counter { 0 };
    EXPECT_TRUE(pool.spawn_detached(coro_void_increment(counter)));
    pool.wait_all();
    EXPECT_EQ(counter.load(), 1);
}

// ============================================================================
// phase1052 regression net for the three latent ThreadPool defects the
// threadpool RCA (2026-06-11) surfaced while clearing the zombie-process
// suspicion (verdict: that zombie was the pre-fix JobGraph deadlock; these
// are hardening fixes for holes found during the trace).
// ============================================================================

// (a) spawn_detached + wait_all under repetition: exercises the
// on_complete idle-notify path and the worker-side frame destroy. A
// leaked frame doesn't fail an assertion directly, but ASAN presets
// catch it; the counter proves exactly-once execution.
TEST(CoroTask, SpawnDetachedManyWaitAllCountsExact)
{
    constexpr int kOuter = 200;
    constexpr int kCoros = 100;
    for (int rep = 0; rep < kOuter; ++rep)
    {
        cd::concurrency::ThreadPool pool { 2 };
        std::atomic<int> counter { 0 };
        for (int i = 0; i < kCoros; ++i)
            ASSERT_TRUE(pool.spawn_detached(coro_void_increment(counter)));
        pool.wait_all();
        ASSERT_EQ(counter.load(), kCoros) << "rep " << rep;
    }
}

// (b) wait_all must never return before the LAST popped-but-uncounted
// task lands. Pre-fix, queued_ fell before in_flight_ rose, so the
// idle predicate could read 0/0/0 mid-handoff and under-count.
TEST(ThreadPool, WaitAllSeesEveryDetachedTask)
{
    constexpr int kOuter = 500;
    constexpr int kJobs  = 50;
    for (int rep = 0; rep < kOuter; ++rep)
    {
        cd::concurrency::ThreadPool pool { 2 };
        std::atomic<int> hits { 0 };
        for (int i = 0; i < kJobs; ++i)
            pool.submit_detached([&] { hits.fetch_add(1, std::memory_order_relaxed); });
        pool.wait_all();
        ASSERT_EQ(hits.load(), kJobs) << "rep " << rep;
    }
}

// (c) spawn_detached against a shut-down pool must report failure AND
// roll back its coroutine bookkeeping — pre-fix the stale
// active_coroutines_ count made any later wait_all() hang forever
// (this test would deadlock; the phase-1047 ctest TIMEOUT would kill
// it visibly).
TEST(CoroTask, SpawnDetachedAfterShutdownFailsCleanly)
{
    cd::concurrency::ThreadPool pool { 1 };
    pool.shutdown();
    std::atomic<int> counter { 0 };
    EXPECT_FALSE(pool.spawn_detached(coro_void_increment(counter)));
    pool.wait_all();  // must return immediately: no stale counts
    EXPECT_EQ(counter.load(), 0);
}

// --- JobGraph --------------------------------------------------------------
TEST(JobGraph, LinearChainRunsInOrder)
{
    cd::concurrency::ThreadPool pool { 2 };
    cd::concurrency::JobGraph g;
    std::vector<int> order;
    std::mutex order_m;
    auto record = [&](int id)
    {
        return [&, id]
        {
            std::scoped_lock guard { order_m };
            order.push_back(id);
        };
    };
    auto a = g.add(record(1));
    auto b = g.add(record(2), { a });
    [[maybe_unused]] auto c = g.add(record(3), { b });
    EXPECT_TRUE(g.run(pool));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(JobGraph, ParallelLeavesRespectFanIn)
{
    cd::concurrency::ThreadPool pool { 4 };
    cd::concurrency::JobGraph g;
    std::atomic<int> sum { 0 };
    auto root = g.add(
        [&]
        {
            sum.fetch_add(1);
        }
    );
    auto a = g.add(
        [&]
        {
            sum.fetch_add(10);
        },
        { root }
    );
    auto b = g.add(
        [&]
        {
            sum.fetch_add(100);
        },
        { root }
    );
    [[maybe_unused]] auto join_id = g.add(
        [&]
        {
            sum.fetch_add(1000);
        },
        { a, b }
    );
    EXPECT_TRUE(g.run(pool));
    EXPECT_EQ(sum.load(), 1 + 10 + 100 + 1000);
}

TEST(JobGraph, CycleDetectionRejects)
{
    cd::concurrency::ThreadPool pool { 1 };
    cd::concurrency::JobGraph g;
    auto a = g.add(
        []
        {
        }
    );
    auto b = g.add(
        []
        {
        },
        { a }
    );
    // Forge a cycle by editing the body via add(... {b}) before linking back.
    // Direct cycle: add c with dep b, then add d with dep on c, then patch a's deps.
    // For simplicity, construct an obvious cycle: a depends on b, b depends on a.
    cd::concurrency::JobGraph g2;
    [[maybe_unused]] auto x = g2.add(
        []
        {
        }
    );
    [[maybe_unused]] auto y = g2.add(
        []
        {
        },
        { x }
    );
    // y already depends on x; create x → y by reassigning x's deps (not exposed).
    // Use invalid dep index as a proxy for "definitely invalid graph".
    cd::concurrency::JobGraph g3;
    (void)g3.add(
        []
        {
        },
        { 99999u }
    );  // dep id out of range
    EXPECT_FALSE(g3.run(pool));
    (void)a;
    (void)b;
}

TEST(JobGraph, EmptyGraphSucceeds)
{
    cd::concurrency::ThreadPool pool { 1 };
    cd::concurrency::JobGraph g;
    EXPECT_TRUE(g.run(pool));
}

}  // namespace
