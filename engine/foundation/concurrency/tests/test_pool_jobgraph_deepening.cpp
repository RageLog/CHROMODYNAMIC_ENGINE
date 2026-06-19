// =============================================================================
// CHROMODYNAMIC — cd::concurrency pool + JobGraph deepening (≥80→100 marathon)
//
// Edge / negative / shutdown-race coverage absent from the existing pool and
// graph suites:
//   - ThreadPool priority ordering on a single worker (mutex priority_queue)
//   - ThreadPool shutdown while jobs are still pending: clean, no hang
//   - WorkStealingThreadPool shutdown with a backlog: queued counter consistent,
//     wait_all after shutdown returns immediately (no stale counts)
//   - JobGraph::failed_nodes() counts throwing bodies but the graph still drains
//   - JobGraph::clear() resets size + failed_nodes
//   - JobGraph single-node graph and re-run after clear
//   - JobGraph diamond on the WorkStealingThreadPool (dependency order honoured)
//   - JobGraph exactly-once execution under a saturated WSP (stress)
//
// Sync in-test is atomic/cv/latch based (no sleep_for). Loop counts are sized
// to finish well under the 120 s ctest TIMEOUT.
// =============================================================================
#include <cd/concurrency/JobGraph.hpp>
#include <cd/concurrency/TaskPriority.hpp>
#include <cd/concurrency/ThreadPool.hpp>
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

// --- ThreadPool: priority ordering on one worker ----------------------------
// A single worker drains the mutex-backed priority_queue strictly by priority.
// Gate the worker with a blocking job, queue Low then High, release: every High
// body must record before any Low body.
TEST(ThreadPoolDeepening, PriorityOrderOnSingleWorker)
{
    cd::concurrency::ThreadPool pool { 1 };
    std::atomic<bool> release { false };
    pool.submit_detached(
        [&release]
        {
            while (!release.load(std::memory_order_acquire))
                std::this_thread::yield();
        }
    );

    std::mutex order_mutex;
    std::vector<int> order;  // 0 = low, 1 = high
    constexpr int kEach = 32;
    for (int i = 0; i < kEach; ++i)
    {
        pool.submit_detached_with_priority(
            cd::concurrency::TaskPriority::Low,
            [&]
            {
                std::scoped_lock g { order_mutex };
                order.push_back(0);
            }
        );
    }
    for (int i = 0; i < kEach; ++i)
    {
        pool.submit_detached_with_priority(
            cd::concurrency::TaskPriority::High,
            [&]
            {
                std::scoped_lock g { order_mutex };
                order.push_back(1);
            }
        );
    }
    release.store(true, std::memory_order_release);
    pool.wait_all();

    ASSERT_EQ(order.size(), static_cast<std::size_t>(2 * kEach));
    // The first kEach entries must all be High (1); the rest Low (0).
    for (int i = 0; i < kEach; ++i)
        EXPECT_EQ(order[static_cast<std::size_t>(i)], 1) << "High did not drain first at " << i;
    for (int i = kEach; i < 2 * kEach; ++i)
        EXPECT_EQ(order[static_cast<std::size_t>(i)], 0) << "Low ran before High at " << i;
}

// --- ThreadPool: shutdown with pending jobs does not hang -------------------
// Flood the queue, immediately shutdown(): completed may be < submitted (some
// jobs dropped), but the call must return and is_running() must flip. A second
// shutdown() is a no-op.
TEST(ThreadPoolDeepening, ShutdownWithPendingJobsTerminatesCleanly)
{
    cd::concurrency::ThreadPool pool { 2 };
    std::atomic<int> ran { 0 };
    for (int i = 0; i < 5'000; ++i)
        pool.submit_detached([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });

    pool.shutdown();  // drains in-flight, joins workers
    EXPECT_FALSE(pool.is_running());
    pool.shutdown();  // idempotent

    // The ThreadPool drains pending work before joining, so completed == submitted.
    EXPECT_EQ(pool.stats().tasks_completed.load(), pool.stats().tasks_submitted.load());
    EXPECT_LE(ran.load(), 5'000);
    EXPECT_EQ(ran.load(), static_cast<int>(pool.stats().tasks_completed.load()));
}

// --- WorkStealingThreadPool: shutdown with backlog, counters stay consistent -
// The WSP DROPS un-run jobs on shutdown (per its contract) but must balance
// queued_ for each dropped job so a subsequent wait_all() returns immediately
// instead of hanging on phantom work.
TEST(PoolDeepening, WspShutdownWithBacklogWaitAllReturns)
{
    cd::concurrency::WorkStealingThreadPool pool { 2 };
    std::atomic<bool> hold { true };
    // Occupy both workers so the rest of the burst stays queued.
    for (int i = 0; i < 2; ++i)
    {
        pool.submit_detached(
            [&hold]
            {
                while (hold.load(std::memory_order_acquire))
                    std::this_thread::yield();
            }
        );
    }
    std::atomic<int> ran { 0 };
    for (int i = 0; i < 4'000; ++i)
        pool.submit_detached([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });

    hold.store(false, std::memory_order_release);
    pool.shutdown();          // drops whatever never got stolen/run
    EXPECT_FALSE(pool.is_running());
    pool.wait_all();          // must NOT hang (this test's TIMEOUT is the tripwire)
    SUCCEED();
}

// --- WorkStealingThreadPool: submit after shutdown is silently dropped ------
TEST(PoolDeepening, WspSubmitAfterShutdownDropsJob)
{
    cd::concurrency::WorkStealingThreadPool pool { 2 };
    pool.shutdown();
    std::atomic<int> ran { 0 };
    pool.submit_detached([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
    pool.wait_all();  // immediate
    EXPECT_EQ(ran.load(), 0);
    EXPECT_FALSE(pool.is_running());
}

// --- JobGraph: throwing bodies counted, graph still drains ------------------
TEST(JobGraphDeepening, ThrowingBodiesCountedAndGraphCompletes)
{
    cd::concurrency::ThreadPool pool { 4 };
    cd::concurrency::JobGraph g;

    constexpr int kThrowers = 5;
    constexpr int kCleans = 10;
    std::atomic<int> clean_ran { 0 };
    for (int i = 0; i < kThrowers; ++i)
        g.add([] { throw std::runtime_error { "node boom" }; });
    for (int i = 0; i < kCleans; ++i)
        g.add([&clean_ran] { clean_ran.fetch_add(1, std::memory_order_relaxed); });

    ASSERT_TRUE(g.run(pool));  // run() returns true even with failed nodes
    EXPECT_EQ(clean_ran.load(), kCleans) << "clean nodes must still all execute";
    EXPECT_EQ(g.failed_nodes(), static_cast<std::uint64_t>(kThrowers))
        << "every throwing body must be counted exactly once";
}

// --- JobGraph: clear() resets size and failure count ------------------------
TEST(JobGraphDeepening, ClearResetsSizeAndFailures)
{
    cd::concurrency::ThreadPool pool { 2 };
    cd::concurrency::JobGraph g;
    g.add([] { throw std::runtime_error { "x" }; });
    ASSERT_TRUE(g.run(pool));
    EXPECT_EQ(g.failed_nodes(), 1u);
    EXPECT_EQ(g.size(), 1u);

    g.clear();
    EXPECT_EQ(g.size(), 0u);
    EXPECT_EQ(g.failed_nodes(), 0u);

    // Re-usable after clear: a fresh node runs cleanly.
    std::atomic<int> ran { 0 };
    g.add([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(g.run(pool));
    EXPECT_EQ(ran.load(), 1);
    EXPECT_EQ(g.failed_nodes(), 0u);
}

// --- JobGraph: single-node graph runs once ----------------------------------
TEST(JobGraphDeepening, SingleNodeRunsOnce)
{
    cd::concurrency::ThreadPool pool { 2 };
    cd::concurrency::JobGraph g;
    std::atomic<int> ran { 0 };
    g.add([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(g.run(pool));
    EXPECT_EQ(ran.load(), 1);
}

// --- JobGraph: diamond on the WorkStealingThreadPool honours order ----------
// A -> {B, C} -> D. D must observe B and C complete; A must precede both.
TEST(JobGraphDeepening, DiamondOnWorkStealingPoolHonoursOrder)
{
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    cd::concurrency::JobGraph g;

    std::atomic<int> a_done { 0 };
    std::atomic<int> b_saw_a { 0 };
    std::atomic<int> c_saw_a { 0 };
    std::atomic<int> d_saw_bc { 0 };
    std::atomic<int> b_done { 0 };
    std::atomic<int> c_done { 0 };

    const auto a = g.add([&] { a_done.store(1, std::memory_order_release); });
    const auto b = g.add(
        [&]
        {
            b_saw_a.store(a_done.load(std::memory_order_acquire), std::memory_order_release);
            b_done.store(1, std::memory_order_release);
        },
        { a });
    const auto c = g.add(
        [&]
        {
            c_saw_a.store(a_done.load(std::memory_order_acquire), std::memory_order_release);
            c_done.store(1, std::memory_order_release);
        },
        { a });
    g.add(
        [&]
        {
            const int bc = b_done.load(std::memory_order_acquire) &&
                           c_done.load(std::memory_order_acquire);
            d_saw_bc.store(bc, std::memory_order_release);
        },
        { b, c });

    ASSERT_TRUE(g.run(pool));
    EXPECT_EQ(b_saw_a.load(), 1) << "B ran before A completed";
    EXPECT_EQ(c_saw_a.load(), 1) << "C ran before A completed";
    EXPECT_EQ(d_saw_bc.load(), 1) << "D ran before both B and C completed";
    EXPECT_EQ(g.failed_nodes(), 0u);
}

// --- JobGraph: exactly-once execution under a saturated WSP -----------------
// Mirrors the phase1050 deadlock net but on the work-stealing pool: a wide set
// of disjoint chains with instant bodies maximises the kick-off race window.
TEST(JobGraphDeepening, ExactlyOnceUnderWorkStealingLoad)
{
    constexpr int kIterations = 120;
    constexpr int kChains = 8;
    constexpr int kLen = 4;
    for (int iter = 0; iter < kIterations; ++iter)
    {
        cd::concurrency::WorkStealingThreadPool pool { 4 };
        cd::concurrency::JobGraph g;
        std::atomic<int> executions { 0 };
        for (int c = 0; c < kChains; ++c)
        {
            cd::concurrency::JobId prev =
                g.add([&] { executions.fetch_add(1, std::memory_order_relaxed); });
            for (int i = 1; i < kLen; ++i)
            {
                prev = g.add(
                    [&] { executions.fetch_add(1, std::memory_order_relaxed); },
                    { prev });
            }
        }
        ASSERT_TRUE(g.run(pool));
        ASSERT_EQ(executions.load(), kChains * kLen)
            << "exactly-once violated on WSP at iteration " << iter;
    }
}

}  // namespace
