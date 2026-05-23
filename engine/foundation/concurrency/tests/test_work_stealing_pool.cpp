// =============================================================================
// CHROMODYNAMIC — cd::concurrency::WorkStealingThreadPool tests (Sprint S2.5)
// =============================================================================
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <gtest/gtest.h>

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
