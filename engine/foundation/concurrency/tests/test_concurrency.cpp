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

// ---------------------------------------------------------------------------
// Phase 23.D — AtomicCounter tests (Wave 188)
// ---------------------------------------------------------------------------
#include <cd/concurrency/AtomicCounter.hpp>

TEST(AtomicCounter, NextReturnsPreviousValueAndIncrements)
{
    cd::concurrency::AtomicCounter c;
    EXPECT_EQ(c.value(), 0u);
    EXPECT_EQ(c.next(), 0u);
    EXPECT_EQ(c.next(), 1u);
    EXPECT_EQ(c.next(), 2u);
    EXPECT_EQ(c.value(), 3u);
}

TEST(AtomicCounter, ResetRestoresGivenValue)
{
    cd::concurrency::AtomicCounter c { 100 };
    EXPECT_EQ(c.value(), 100u);
    c.next();
    c.reset(50);
    EXPECT_EQ(c.value(), 50u);
}

// ---------------------------------------------------------------------------
// Phase 26.D — Once tests (Wave 194)
// ---------------------------------------------------------------------------
#include <cd/concurrency/Once.hpp>

TEST(Once, RunsExactlyOnce)
{
    cd::concurrency::Once once;
    int n = 0;
    once.call([&] { ++n; });
    once.call([&] { ++n; });
    once.call([&] { ++n; });
    EXPECT_EQ(n, 1);
    EXPECT_TRUE(once.is_done());
}

TEST(Once, ResetForTestReruns)
{
    cd::concurrency::Once once;
    int n = 0;
    once.call([&] { ++n; });
    once.reset_for_test();
    once.call([&] { ++n; });
    EXPECT_EQ(n, 2);
}

#include <cd/concurrency/Latch.hpp>
#include <thread>
#include <vector>

TEST(Latch, ReadyAfterCountdownToZero)
{
    cd::concurrency::Latch l { 3 };
    EXPECT_FALSE(l.is_ready());
    l.count_down();
    l.count_down();
    l.count_down();
    EXPECT_TRUE(l.is_ready());
}

TEST(Latch, ParallelWorkersConverge)
{
    constexpr std::ptrdiff_t kN = 8;
    cd::concurrency::Latch  l { kN };
    std::atomic<int> hits { 0 };
    std::vector<std::thread> ts;
    ts.reserve(kN);
    for (std::ptrdiff_t i = 0; i < kN; ++i)
    {
        ts.emplace_back([&] {
            hits.fetch_add(1);
            l.count_down();
        });
    }
    l.wait();
    EXPECT_EQ(hits.load(), kN);
    for (auto& t : ts) t.join();
}

TEST(Latch, ArriveAndWaitJoinsAllWorkers)
{
    constexpr std::ptrdiff_t kN = 4;
    cd::concurrency::Latch  l { kN };
    std::vector<std::thread> ts;
    ts.reserve(kN);
    for (std::ptrdiff_t i = 0; i < kN; ++i)
        ts.emplace_back([&] { l.arrive_and_wait(); });
    for (auto& t : ts) t.join();
    EXPECT_TRUE(l.is_ready());
}

#include <cd/concurrency/Backoff.hpp>

TEST(Backoff, StepCountsUp)
{
    cd::concurrency::Backoff b;
    EXPECT_EQ(b.step(), 0u);
    b.pause();
    EXPECT_EQ(b.step(), 1u);
    b.pause();
    EXPECT_EQ(b.step(), 2u);
}

TEST(Backoff, ResetRestoresZero)
{
    cd::concurrency::Backoff b;
    b.pause(); b.pause(); b.pause();
    b.reset();
    EXPECT_EQ(b.step(), 0u);
}

TEST(Backoff, StepCappedAt20)
{
    cd::concurrency::Backoff b;
    for (int i = 0; i < 30; ++i) b.pause();
    EXPECT_LE(b.step(), 20u);
}

#include <cd/concurrency/Barrier.hpp>
#include <thread>
#include <atomic>
#include <vector>

TEST(Barrier, ReusableAcrossPhases)
{
    constexpr std::ptrdiff_t kN = 4;
    cd::concurrency::Barrier b { kN };
    std::atomic<int> phase_a { 0 };
    std::atomic<int> phase_b { 0 };
    std::vector<std::thread> ts;
    ts.reserve(kN);
    for (std::ptrdiff_t i = 0; i < kN; ++i)
    {
        ts.emplace_back([&]
        {
            phase_a.fetch_add(1);
            b.arrive_and_wait();
            // After release of phase A, every thread sees phase_a == kN.
            EXPECT_EQ(phase_a.load(), kN);
            phase_b.fetch_add(1);
            b.arrive_and_wait();
            EXPECT_EQ(phase_b.load(), kN);
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(b.count(), kN);
}

TEST(Barrier, CountReadable)
{
    cd::concurrency::Barrier b { 3 };
    EXPECT_EQ(b.count(), 3);
}

#include <cd/concurrency/ParallelFor.hpp>

TEST(ParallelFor, EveryIndexVisitedExactlyOnce)
{
    constexpr std::size_t kN = 64;
    std::vector<std::atomic<int>> hits(kN);
    for (auto& h : hits) h.store(0);
    cd::concurrency::parallel_for(0, kN, [&](std::size_t i)
    {
        hits[i].fetch_add(1);
    });
    for (std::size_t i = 0; i < kN; ++i)
        EXPECT_EQ(hits[i].load(), 1) << "index " << i;
}

TEST(ParallelFor, EmptyRangeReturnsImmediately)
{
    std::atomic<int> hits { 0 };
    cd::concurrency::parallel_for(5, 5, [&](std::size_t) { hits.fetch_add(1); });
    EXPECT_EQ(hits.load(), 0);
}

TEST(ParallelFor, SingleWorkerDegradesToSerial)
{
    constexpr std::size_t kN = 16;
    std::vector<int> seen(kN, 0);
    cd::concurrency::parallel_for(0, kN,
        [&](std::size_t i) { seen[i] = static_cast<int>(i + 1); }, 1u);
    for (std::size_t i = 0; i < kN; ++i)
        EXPECT_EQ(seen[i], static_cast<int>(i + 1));
}

#include <cd/concurrency/Channel.hpp>

TEST(Channel, TrySendThenTryReceive)
{
    cd::concurrency::Channel<int> ch { 4 };
    EXPECT_TRUE(ch.try_send(42));
    auto v = ch.try_receive();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
}

TEST(Channel, TrySendFullReturnsFalse)
{
    cd::concurrency::Channel<int> ch { 2 };
    EXPECT_TRUE(ch.try_send(1));
    EXPECT_TRUE(ch.try_send(2));
    EXPECT_FALSE(ch.try_send(3));
}

TEST(Channel, ProducerConsumerRoundTrip)
{
    cd::concurrency::Channel<int> ch { 4 };
    std::thread prod([&] {
        for (int i = 0; i < 10; ++i) (void)ch.send(i);
        ch.close();
    });
    int sum = 0;
    while (auto v = ch.receive()) sum += *v;
    prod.join();
    EXPECT_EQ(sum, 45);   // 0+1+...+9
}

TEST(Channel, CloseUnblocksReceiver)
{
    cd::concurrency::Channel<int> ch { 4 };
    std::thread closer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ch.close();
    });
    auto v = ch.receive();
    EXPECT_FALSE(v.has_value());
    closer.join();
}

#include <cd/concurrency/EventBus.hpp>

namespace {
struct TestEvent { int value { 0 }; };
struct AnotherEvent { float score { 0.0F }; };
}

TEST(EventBus, SubscribeAndPublishMatch)
{
    cd::concurrency::EventBus bus;
    int sum = 0;
    bus.subscribe<TestEvent>([&](const TestEvent& e) { sum += e.value; });
    bus.publish(TestEvent { 5 });
    bus.publish(TestEvent { 10 });
    EXPECT_EQ(sum, 15);
}

TEST(EventBus, DifferentEventTypesIsolated)
{
    cd::concurrency::EventBus bus;
    int hits_a = 0;
    int hits_b = 0;
    bus.subscribe<TestEvent>([&](const TestEvent&) { ++hits_a; });
    bus.subscribe<AnotherEvent>([&](const AnotherEvent&) { ++hits_b; });
    bus.publish(TestEvent { 0 });
    bus.publish(TestEvent { 0 });
    bus.publish(AnotherEvent {});
    EXPECT_EQ(hits_a, 2);
    EXPECT_EQ(hits_b, 1);
}

TEST(EventBus, UnsubscribeStopsCallback)
{
    cd::concurrency::EventBus bus;
    int hits = 0;
    auto id = bus.subscribe<TestEvent>([&](const TestEvent&) { ++hits; });
    bus.publish(TestEvent {});
    EXPECT_EQ(hits, 1);
    EXPECT_TRUE(bus.unsubscribe(id));
    bus.publish(TestEvent {});
    EXPECT_EQ(hits, 1);
}

#include <cd/concurrency/JobToken.hpp>

TEST(JobToken, NewTokenIsNotSettled)
{
    cd::concurrency::JobToken t;
    EXPECT_FALSE(t.is_cancelled());
    EXPECT_FALSE(t.is_complete());
    EXPECT_FALSE(t.is_settled());
}

TEST(JobToken, CancelPropagatesToCopy)
{
    cd::concurrency::JobToken a;
    cd::concurrency::JobToken b = a;   // shared state
    a.cancel();
    EXPECT_TRUE(b.is_cancelled());
    EXPECT_TRUE(b.is_settled());
}

TEST(JobToken, CompleteDistinctFromCancel)
{
    cd::concurrency::JobToken t;
    t.complete();
    EXPECT_TRUE(t.is_complete());
    EXPECT_FALSE(t.is_cancelled());
}

TEST(JobToken, WaitUnblocksOnCancel)
{
    cd::concurrency::JobToken t;
    std::thread worker([t] { t.cancel(); });
    t.wait();
    EXPECT_TRUE(t.is_cancelled());
    worker.join();
}

#include <cd/concurrency/Future.hpp>

TEST(Future, SetMakesReady)
{
    cd::concurrency::Promise<int> p;
    auto f = p.future();
    EXPECT_FALSE(f.is_ready());
    p.set(42);
    EXPECT_TRUE(f.is_ready());
    EXPECT_EQ(f.get(), 42);
}

TEST(Future, TryGetBeforeSetIsEmpty)
{
    cd::concurrency::Promise<int> p;
    auto f = p.future();
    auto v = f.try_get();
    EXPECT_FALSE(v.has_value());
}

TEST(Future, WaitUnblocksOnSet)
{
    cd::concurrency::Promise<int> p;
    auto f = p.future();
    std::thread setter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        p.set(7);
    });
    f.wait();
    EXPECT_EQ(f.get(), 7);
    setter.join();
}
