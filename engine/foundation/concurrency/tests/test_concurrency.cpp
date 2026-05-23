// =============================================================================
// CHROMODYNAMIC — cd::concurrency tests (Sprint S2.1.c)
// =============================================================================
#include <cd/concurrency/Atomics.hpp>
#include <cd/concurrency/DataChannel.hpp>
#include <cd/concurrency/RingBuffer.hpp>
#include <cd/concurrency/SpinLock.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace
{

// --- RingBuffer single-threaded -----------------------------------------
TEST(RingBuffer, EmptyOnInit)
{
    cd::concurrency::RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.read_available(), 0u);
    EXPECT_EQ(rb.write_available(), 4u);
}

TEST(RingBuffer, PushPopRoundTrip)
{
    cd::concurrency::RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.push(1));
    EXPECT_TRUE(rb.push(2));
    EXPECT_TRUE(rb.push(3));
    EXPECT_EQ(rb.read_available(), 3u);

    int x = 0;
    ASSERT_TRUE(rb.pop(x));
    EXPECT_EQ(x, 1);
    ASSERT_TRUE(rb.pop(x));
    EXPECT_EQ(x, 2);
    ASSERT_TRUE(rb.pop(x));
    EXPECT_EQ(x, 3);
    EXPECT_FALSE(rb.pop(x));
}

TEST(RingBuffer, FullOnCapacityPlusOneReject)
{
    cd::concurrency::RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.push(1));
    EXPECT_TRUE(rb.push(2));
    EXPECT_TRUE(rb.push(3));
    EXPECT_TRUE(rb.push(4));
    EXPECT_FALSE(rb.push(5));
    EXPECT_EQ(rb.write_available(), 0u);
}

TEST(RingBuffer, MoveOnlyType)
{
    cd::concurrency::RingBuffer<std::unique_ptr<int>, 4> rb;
    EXPECT_TRUE(rb.push(std::make_unique<int>(42)));
    auto popped = rb.try_pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(**popped, 42);
}

TEST(RingBuffer, ConsumeAllDrains)
{
    cd::concurrency::RingBuffer<int, 8> rb;
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_TRUE(rb.push(i));
    }
    int sum = 0;
    auto n = rb.consume_all(
        [&](int v)
        {
            sum += v;
        }
    );
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(sum, 0 + 1 + 2 + 3 + 4);
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, ResetClearsState)
{
    cd::concurrency::RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.push(1));
    EXPECT_TRUE(rb.push(2));
    rb.reset();
    EXPECT_TRUE(rb.empty());
}

// --- RingBuffer SPSC threaded stress -------------------------------------
TEST(RingBuffer, SpscStressUnderProducerConsumer)
{
    cd::concurrency::RingBuffer<int, 1024> rb;
    constexpr int kItems = 100'000;
    std::atomic<int> produced { 0 };
    std::atomic<std::int64_t> consumed_sum { 0 };

    std::thread producer { [&]
                           {
                               for (int i = 0; i < kItems; ++i)
                               {
                                   while (!rb.push(i))
                                   {
                                       cd::concurrency::cpu_pause();
                                   }
                                   produced.fetch_add(1, std::memory_order_relaxed);
                               }
                           } };

    std::thread consumer { [&]
                           {
                               int seen = 0;
                               while (seen < kItems)
                               {
                                   int x = 0;
                                   if (rb.pop(x))
                                   {
                                       consumed_sum.fetch_add(x, std::memory_order_relaxed);
                                       ++seen;
                                   }
                                   else
                                   {
                                       cd::concurrency::cpu_pause();
                                   }
                               }
                           } };

    producer.join();
    consumer.join();
    EXPECT_EQ(produced.load(), kItems);

    // Sum of 0..kItems-1 = (kItems * (kItems - 1)) / 2.
    const std::int64_t expected = static_cast<std::int64_t>(kItems) * (kItems - 1) / 2;
    EXPECT_EQ(consumed_sum.load(), expected);
}

// --- DataChannel ---------------------------------------------------------
TEST(DataChannel, NameAndCapacity)
{
    cd::concurrency::DataChannel<int> ch { "telemetry", 8 };
    EXPECT_EQ(ch.name(), "telemetry");
    EXPECT_EQ(ch.capacity(), 8u);
    EXPECT_TRUE(ch.empty());
}

TEST(DataChannel, RoundTrip)
{
    cd::concurrency::DataChannel<int> ch { "audio_mix", 4 };
    ASSERT_TRUE(ch.push(1));
    ASSERT_TRUE(ch.push(2));
    ASSERT_TRUE(ch.push(3));
    EXPECT_EQ(ch.size(), 3u);

    int sum = 0;
    ch.consume_all(
        [&](int v)
        {
            sum += v;
        }
    );
    EXPECT_EQ(sum, 6);
    EXPECT_TRUE(ch.empty());
}

TEST(DataChannel, ZeroCapacityIsAtLeastOne)
{
    cd::concurrency::DataChannel<int> ch { "weird", 0 };
    EXPECT_EQ(ch.capacity(), 1u);
}

// --- SpinLock ------------------------------------------------------------
TEST(SpinLock, LockUnlockBasic)
{
    cd::concurrency::SpinLock m;
    m.lock();
    EXPECT_FALSE(m.try_lock());
    m.unlock();
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST(SpinLock, GuardRaii)
{
    cd::concurrency::SpinLock m;
    {
        cd::concurrency::SpinLockGuard g { m };
        EXPECT_FALSE(m.try_lock());
    }
    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST(SpinLock, MutualExclusionUnderContention)
{
    cd::concurrency::SpinLock m;
    long long counter = 0;
    constexpr int kIters = 50'000;
    constexpr int kThreads = 4;

    std::vector<std::thread> ts;
    ts.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        ts.emplace_back(
            [&]
            {
                for (int j = 0; j < kIters; ++j)
                {
                    cd::concurrency::SpinLockGuard g { m };
                    ++counter;
                }
            }
        );
    }
    for (auto& t : ts)
        t.join();
    EXPECT_EQ(counter, static_cast<long long>(kIters) * kThreads);
}

// --- Atomics helpers -----------------------------------------------------
TEST(Atomics, CpuPauseCompilesAndRuns)
{
    // No observable behaviour to assert; the goal is to exercise the intrinsic
    // on every architecture so a port regression is caught at link time.
    for (int i = 0; i < 8; ++i)
    {
        cd::concurrency::cpu_pause();
    }
    SUCCEED();
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase 20.E — Stopwatch tests (Wave 182)
// ---------------------------------------------------------------------------
#include <cd/concurrency/Stopwatch.hpp>
#include <thread>

TEST(Stopwatch, ElapsedAdvances)
{
    cd::concurrency::Stopwatch sw;
    std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
    EXPECT_GE(sw.elapsed_ms(), 4.0);
    EXPECT_GE(sw.elapsed().count(), 0);
}

TEST(Stopwatch, RestartReturnsPriorInterval)
{
    cd::concurrency::Stopwatch sw;
    std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
    const auto first = sw.restart();
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(first).count(), 4);
    // After restart, fresh elapsed starts from zero.
    EXPECT_LT(sw.elapsed_ms(), 5.0);
}
