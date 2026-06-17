// =============================================================================
// CHROMODYNAMIC — cd::concurrency::WorkStealingThreadPool tests (Sprint S2.5)
// =============================================================================
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <ranges>
#include <atomic>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{

TEST(WorkStealingThreadPool, ConstructsAndShutsDown)
{
    cd::concurrency::WorkStealingThreadPool pool { 2 };
    EXPECT_TRUE(pool.is_running());
    EXPECT_EQ(pool.thread_count(), 2u);
    pool.shutdown();
    EXPECT_FALSE(pool.is_running());
}

TEST(WorkStealingThreadPool, SingleFutureReturnsValue)
{
    cd::concurrency::WorkStealingThreadPool pool { 2 };
    auto fut = pool.submit(
        []
        {
            return 42;
        }
    );
    EXPECT_EQ(fut.get(), 42);
}

TEST(WorkStealingThreadPool, FuturePropagatesException)
{
    cd::concurrency::WorkStealingThreadPool pool { 2 };
    auto fut = pool.submit(
        []() -> int
        {
            throw std::runtime_error { "boom" };
        }
    );
    EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST(WorkStealingThreadPool, ManyTasksAllComplete)
{
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    constexpr int kN = 5'000;
    std::atomic<int> counter { 0 };
    for (int i = 0; i < kN; ++i)
    {
        pool.submit_detached(
            [&counter]
            {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    pool.wait_all();
    EXPECT_EQ(counter.load(), kN);
    EXPECT_EQ(pool.pending(), 0u);
    EXPECT_EQ(pool.stats().tasks_submitted.load(), pool.stats().tasks_completed.load());
}

TEST(WorkStealingThreadPool, SumViaFutures)
{
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    constexpr int kN = 500;
    std::vector<std::future<int>> futures;
    futures.reserve(static_cast<std::size_t>(kN));
    for (int i = 0; i < kN; ++i)
    {
        futures.emplace_back(pool.submit(
            [i]
            {
                return i + 1;
            }
        ));
    }
    int sum = 0;
    for (auto& f : futures)
        sum += f.get();
    EXPECT_EQ(sum, kN * (kN + 1) / 2);
}

TEST(WorkStealingThreadPool, ImbalancedWorkloadCompletes)
{
    // Two workers, but one is held briefly busy with a "slow" job so the other
    // must drain a flood of fast jobs by stealing from queue[0]'s deque (which
    // never gets drained by worker 0 since it's blocked).
    if (std::thread::hardware_concurrency() < 2)
    {
        GTEST_SKIP() << "single-core host";
    }
    cd::concurrency::WorkStealingThreadPool pool { 2 };
    std::atomic<int> ran { 0 };
    std::atomic<bool> release { false };
    pool.submit_detached(
        [&]
        {
            while (!release.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        }
    );
    for (int i = 0; i < 500; ++i)
    {
        pool.submit_detached(
            [&ran]
            {
                ran.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    release.store(true, std::memory_order_release);
    pool.wait_all();
    EXPECT_EQ(ran.load(), 500);
    EXPECT_EQ(pool.stats().tasks_submitted.load(), pool.stats().tasks_completed.load());
}

TEST(WorkStealingThreadPool, NestedSubmitFromWorker)
{
    cd::concurrency::WorkStealingThreadPool pool { 3 };
    std::atomic<int> ran { 0 };
    pool.submit_detached(
        [&]
        {
            ran.fetch_add(1);
            pool.submit_detached(
                [&]
                {
                    ran.fetch_add(1);
                    pool.submit_detached(
                        [&]
                        {
                            ran.fetch_add(1);
                        }
                    );
                }
            );
        }
    );
    pool.wait_all();
    EXPECT_EQ(ran.load(), 3);
}

}  // namespace

// =============================================================================
// Phase 283 / W8 tail - augmentations per ADR-20260528-job-system-design D4
// =============================================================================

namespace
{

TEST(WorkStealingThreadPool, StressTenThousandJobsCompletes)
{
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    constexpr int kN = 10'000;
    std::atomic<int> counter { 0 };
    for (int i = 0; i < kN; ++i)
    {
        pool.submit_detached(
            [&counter]
            {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    pool.wait_all();
    EXPECT_EQ(counter.load(), kN);
    EXPECT_EQ(pool.pending(), 0u);
    EXPECT_EQ(pool.stats().tasks_submitted.load(), pool.stats().tasks_completed.load());
    EXPECT_EQ(pool.stats().detached_exceptions.load(), 0u);
}

TEST(WorkStealingThreadPool, StealsActuallyFireWhenImbalanced)
{
    // With 4 workers and round-robin injection, a heavily front-loaded
    // submission burst combined with one worker held briefly busy must drive
    // the other workers to steal. We assert stats().steals > 0 to catch
    // regressions where the steal path silently breaks (the existing
    // ImbalancedWorkloadCompletes test only checks completion).
    if (std::thread::hardware_concurrency() < 2)
    {
        GTEST_SKIP() << "single-core host cannot exercise stealing";
    }
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    std::atomic<bool> release { false };
    pool.submit_detached(
        [&release]
        {
            while (!release.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        }
    );
    constexpr int kBurst = 2000;
    std::atomic<int> ran { 0 };
    for (int i = 0; i < kBurst; ++i)
    {
        pool.submit_detached(
            [&ran]
            {
                ran.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    release.store(true, std::memory_order_release);
    pool.wait_all();
    EXPECT_EQ(ran.load(), kBurst);
    EXPECT_GT(pool.stats().steals.load(), 0u)
        << "no steals fired under imbalanced workload - work-stealing path regression";
}


// ---------------------------------------------------------------------------
// phase1078 (X1-FU-A) regression net: the worker wake path moved from a
// cv + 2 ms polling wait_for to C++20 atomic wait/notify on a wake
// epoch. A lost wakeup now hangs forever instead of being papered over
// by the next poll tick, so these tests + the ctest TIMEOUT are the
// tripwire.
// ---------------------------------------------------------------------------

// Many submit/wait_all ROUNDS: each round puts every worker to sleep
// (queue drained), then the next round's enqueue must wake them via the
// epoch bump alone. 200 rounds x 64 jobs makes a lost-wake practically
// certain to trip the TIMEOUT if the protocol regresses.
TEST(WorkStealingThreadPool, AtomicWakeSurvivesRepeatedSleepWakeRounds)
{
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    std::atomic<int> ran { 0 };
    constexpr int kRounds = 200;
    constexpr int kJobsPerRound = 64;
    for (int r = 0; r < kRounds; ++r)
    {
        for (int j = 0; j < kJobsPerRound; ++j)
        {
            pool.submit_detached(
                [&ran]
                {
                    ran.fetch_add(1, std::memory_order_relaxed);
                }
            );
        }
        pool.wait_all();
    }
    EXPECT_EQ(ran.load(), kRounds * kJobsPerRound);
}

// Multi-producer burst: 8 external threads hammer submit_detached
// concurrently while workers sleep/wake. Exercises the
// epoch-bump-between-load-and-wait window from many wakers at once.
TEST(WorkStealingThreadPool, AtomicWakeMultiProducerBurst)
{
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    std::atomic<int> ran { 0 };
    constexpr int kProducers = 8;
    constexpr int kJobsPerProducer = 500;
    {
        std::vector<std::jthread> producers;
        producers.reserve(kProducers);
        for (int t = 0; t < kProducers; ++t)
        {
            producers.emplace_back(
                [&pool, &ran]
                {
                    for (int j = 0; j < kJobsPerProducer; ++j)
                    {
                        pool.submit_detached(
                            [&ran]
                            {
                                ran.fetch_add(1, std::memory_order_relaxed);
                            }
                        );
                    }
                }
            );
        }
    }  // producers joined
    pool.wait_all();
    EXPECT_EQ(ran.load(), kProducers * kJobsPerProducer);
}

// Shutdown must wake sleeping workers via the epoch (no cv to poke any
// more): construct pools, let workers reach the sleep state, destroy.
// 50 cycles; a missed shutdown wake = join hang = TIMEOUT trip.
TEST(WorkStealingThreadPool, AtomicWakeShutdownWakesSleepers)
{
    for (int cycle = 0; cycle < 50; ++cycle)
    {
        cd::concurrency::WorkStealingThreadPool pool { 3 };
        // One tiny job per cycle so workers transition run -> sleep.
        std::atomic<int> ran { 0 };
        pool.submit_detached(
            [&ran]
            {
                ran.fetch_add(1, std::memory_order_relaxed);
            }
        );
        pool.wait_all();
        EXPECT_EQ(ran.load(), 1);
        // dtor -> shutdown() -> epoch bumps must rouse all 3 sleepers.
    }
}


// ---------------------------------------------------------------------------
// phase1079 (X1-FU-D) regression net: priority-aware pop + steal.
// ---------------------------------------------------------------------------

// Single worker => deterministic: gate the worker, queue Low jobs THEN
// High jobs, release. The per-level deque scan must run every High
// before any Low regardless of submission order.
TEST(WorkStealingThreadPool, HighPriorityPopsBeforeLowOnOneWorker)
{
    cd::concurrency::WorkStealingThreadPool pool { 1 };
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
    constexpr int kEach = 16;
    for (int i = 0; i < kEach; ++i)
    {
        pool.submit_detached_with_priority(
            cd::concurrency::TaskPriority::Low,
            [&order_mutex, &order]
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
            [&order_mutex, &order]
            {
                std::scoped_lock g { order_mutex };
                order.push_back(1);
            }
        );
    }
    release.store(true, std::memory_order_release);
    pool.wait_all();
    ASSERT_EQ(order.size(), static_cast<std::size_t>(2 * kEach));
    // Every High (1) must precede every Low (0).
    const auto first_low = std::ranges::find(order, 0);
    const auto last_high = std::ranges::find(order | std::views::reverse, 1);
    const auto last_high_idx =
        static_cast<std::size_t>(std::distance(order.begin(), last_high.base()) - 1);
    const auto first_low_idx =
        static_cast<std::size_t>(std::distance(order.begin(), first_low));
    EXPECT_LT(last_high_idx, first_low_idx)
        << "a Low job ran before the High backlog drained";
}

// All four levels mixed under contention across 4 workers: ordering is
// only guaranteed per worker, so this is a completion/leak stress, plus
// the Critical bucket must drain no later than the others complete.
TEST(WorkStealingThreadPool, MixedPriorityStressAllComplete)
{
    cd::concurrency::WorkStealingThreadPool pool { 4 };
    std::atomic<int> ran { 0 };
    constexpr int kPerLevel = 600;
    using P = cd::concurrency::TaskPriority;
    for (int i = 0; i < kPerLevel; ++i)
    {
        for (const auto p : { P::Low, P::Normal, P::High, P::Critical })
        {
            pool.submit_detached_with_priority(
                p,
                [&ran]
                {
                    ran.fetch_add(1, std::memory_order_relaxed);
                }
            );
        }
    }
    pool.wait_all();
    EXPECT_EQ(ran.load(), kPerLevel * 4);
    EXPECT_EQ(pool.stats().detached_exceptions.load(), 0u);
}

}  // namespace
