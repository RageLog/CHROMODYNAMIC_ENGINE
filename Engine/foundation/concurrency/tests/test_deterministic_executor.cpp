// =============================================================================
// CHROMODYNAMIC — cd::concurrency::DeterministicExecutor tests (Sprint S2.5)
// =============================================================================
#include <cd/concurrency/DeterministicExecutor.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

TEST(DeterministicExecutor, EmptyStepReturnsFalse)
{
    cd::concurrency::DeterministicExecutor ex;
    EXPECT_FALSE(ex.step());
    EXPECT_EQ(ex.pending(), 0u);
    EXPECT_EQ(ex.executed_count(), 0u);
}

TEST(DeterministicExecutor, FifoOrderPreserved)
{
    cd::concurrency::DeterministicExecutor ex;
    std::vector<int> order;
    for (int i = 0; i < 5; ++i)
    {
        ex.submit(
            [&order, i]
            {
                order.push_back(i);
            }
        );
    }
    EXPECT_EQ(ex.pending(), 5u);
    EXPECT_EQ(ex.drain(), 5u);
    ASSERT_EQ(order.size(), 5u);
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
    }
    EXPECT_EQ(ex.executed_count(), 5u);
    EXPECT_EQ(ex.pending(), 0u);
}

TEST(DeterministicExecutor, StepRunsExactlyOne)
{
    cd::concurrency::DeterministicExecutor ex;
    std::atomic<int> count { 0 };
    ex.submit(
        [&]
        {
            count.fetch_add(1);
        }
    );
    ex.submit(
        [&]
        {
            count.fetch_add(1);
        }
    );
    EXPECT_TRUE(ex.step());
    EXPECT_EQ(count.load(), 1);
    EXPECT_EQ(ex.pending(), 1u);
    EXPECT_TRUE(ex.step());
    EXPECT_EQ(count.load(), 2);
    EXPECT_FALSE(ex.step());
}

TEST(DeterministicExecutor, DrainDoesNotRunReentrantSubmissions)
{
    cd::concurrency::DeterministicExecutor ex;
    std::atomic<int> ran { 0 };
    ex.submit(
        [&]
        {
            ++ran;
            // submitted during drain — must NOT run in the same drain() pass
            ex.submit(
                [&]
                {
                    ++ran;
                }
            );
        }
    );
    EXPECT_EQ(ex.drain(), 1u);
    EXPECT_EQ(ran.load(), 1);
    EXPECT_EQ(ex.pending(), 1u);
    EXPECT_EQ(ex.drain(), 1u);
    EXPECT_EQ(ran.load(), 2);
}

TEST(DeterministicExecutor, PumpUntilDrainsCascades)
{
    cd::concurrency::DeterministicExecutor ex;
    std::atomic<int> ran { 0 };
    std::function<void()> chain = [&]
    {
        ++ran;
        if (ran.load() < 4)
            ex.submit(chain);
    };
    ex.submit(chain);
    const auto total = ex.pump_until(
        []
        {
            return true;
        }
    );
    EXPECT_EQ(total, 4u);
    EXPECT_EQ(ran.load(), 4);
    EXPECT_EQ(ex.pending(), 0u);
}

TEST(DeterministicExecutor, PumpUntilStopsOnPredicate)
{
    cd::concurrency::DeterministicExecutor ex;
    std::atomic<int> ran { 0 };
    std::function<void()> chain = [&]
    {
        ++ran;
        ex.submit(chain);  // always keeps going if pump_until allowed
    };
    ex.submit(chain);
    const auto total = ex.pump_until(
        [&]
        {
            return ran.load() < 3;
        }
    );
    EXPECT_GE(ran.load(), 3);
    EXPECT_LE(total, 8u);  // bound by either pred flip or first drain pass
}

TEST(DeterministicExecutor, SubmitReturnsMonotonicIds)
{
    cd::concurrency::DeterministicExecutor ex;
    const auto a = ex.submit(
        []
        {
        }
    );
    const auto b = ex.submit(
        []
        {
        }
    );
    const auto c = ex.submit(
        []
        {
        }
    );
    EXPECT_LT(a, b);
    EXPECT_LT(b, c);
    EXPECT_EQ(ex.last_assigned_id(), c);
}

TEST(DeterministicExecutor, FutureCarriesReturnValue)
{
    cd::concurrency::DeterministicExecutor ex;
    auto fut = ex.submit_future(
        []
        {
            return std::string { "deterministic" };
        }
    );
    ex.drain();
    EXPECT_EQ(fut.get(), "deterministic");
}

TEST(DeterministicExecutor, FuturePropagatesException)
{
    cd::concurrency::DeterministicExecutor ex;
    auto fut = ex.submit_future(
        []() -> int
        {
            throw std::runtime_error { "boom" };
        }
    );
    ex.drain();
    EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST(DeterministicExecutor, ClearDropsPendingWithoutRunning)
{
    cd::concurrency::DeterministicExecutor ex;
    std::atomic<int> ran { 0 };
    ex.submit(
        [&]
        {
            ++ran;
        }
    );
    ex.submit(
        [&]
        {
            ++ran;
        }
    );
    EXPECT_EQ(ex.pending(), 2u);
    ex.clear();
    EXPECT_EQ(ex.pending(), 0u);
    EXPECT_FALSE(ex.step());
    EXPECT_EQ(ran.load(), 0);
}

}  // namespace
