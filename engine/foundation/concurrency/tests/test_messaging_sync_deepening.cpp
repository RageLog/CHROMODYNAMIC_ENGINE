// =============================================================================
// CHROMODYNAMIC — cd::concurrency messaging + sync deepening (≥80→100 marathon)
//
// Edge / negative / boundary / MPSC-stress coverage missing from the existing
// suites:
//   - Channel: multi-producer-single-consumer stress (exact sum), try_send on a
//     closed channel, blocking send unblocked by close, size accuracy
//   - EventBus: multi-subscriber fire order, unsubscribe-nonexistent, clear,
//     handler_count across types
//   - Future: multiple concurrent waiters all observe the set value; try_get
//     transitions empty -> value
//   - JobToken: complete() also unblocks wait(); double-cancel is idempotent
//   - DeterministicExecutor: pump_until respects max_iterations cap (infinite
//     resubmit can't wedge)
//   - RingBuffer: wrap-around across the modulo boundary keeps FIFO ordering
//   - DataChannel: non-default capacity wrap-around + write_available accounting
//   - Flag: try_raise reports previous, cross-thread wait_until_raised
//   - Once: concurrent call() runs the body exactly once
//   - SpinLock: try_lock contention from another thread
//
// No sleep_for for synchronisation — atomic/latch/cv based. Bounded loops keep
// every case fast under the 120 s ctest TIMEOUT.
// =============================================================================
#include <cd/concurrency/Channel.hpp>
#include <cd/concurrency/DataChannel.hpp>
#include <cd/concurrency/DeterministicExecutor.hpp>
#include <cd/concurrency/EventBus.hpp>
#include <cd/concurrency/Flag.hpp>
#include <cd/concurrency/Future.hpp>
#include <cd/concurrency/JobToken.hpp>
#include <cd/concurrency/Latch.hpp>
#include <cd/concurrency/Once.hpp>
#include <cd/concurrency/RingBuffer.hpp>
#include <cd/concurrency/SpinLock.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

namespace
{

// --- Channel: multi-producer single-consumer stress -------------------------
TEST(ChannelDeepening, MultiProducerSingleConsumerExactSum)
{
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 10'000;
    cd::concurrency::Channel<int> ch { 64 };

    std::atomic<int> sent { 0 };
    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back(
            [&]
            {
                for (int i = 0; i < kPerProducer; ++i)
                {
                    (void)ch.send(1);
                    sent.fetch_add(1, std::memory_order_relaxed);
                }
            }
        );
    }

    // Single consumer drains until it has counted every item.
    constexpr int kTotal = kProducers * kPerProducer;
    std::int64_t sum = 0;
    int received = 0;
    std::thread consumer {
        [&]
        {
            while (received < kTotal)
            {
                if (auto v = ch.receive())
                {
                    sum += *v;
                    ++received;
                }
            }
        }
    };

    for (auto& t : producers)
        t.join();
    consumer.join();
    EXPECT_EQ(sent.load(), kTotal);
    EXPECT_EQ(sum, kTotal) << "MPSC channel lost or duplicated a message";
}

// --- Channel: try_send on a closed channel returns false --------------------
TEST(ChannelDeepening, TrySendOnClosedReturnsFalse)
{
    cd::concurrency::Channel<int> ch { 4 };
    ch.close();
    EXPECT_FALSE(ch.try_send(1));
    // receive() on a closed empty channel returns nullopt.
    EXPECT_FALSE(ch.receive().has_value());
}

// --- Channel: blocking send is unblocked by close --------------------------
// Fill the channel, then a producer blocks in send(); close() must release it
// and the send must report failure (false) without inserting.
TEST(ChannelDeepening, BlockingSendUnblockedByClose)
{
    cd::concurrency::Channel<int> ch { 1 };
    ASSERT_TRUE(ch.try_send(1));  // now full

    cd::concurrency::Latch entered { 1 };
    std::atomic<bool> send_result { true };
    std::thread producer {
        [&]
        {
            entered.count_down();
            // Blocks: channel full. close() must wake it; result must be false.
            send_result.store(ch.send(2), std::memory_order_release);
        }
    };
    entered.wait();
    ch.close();
    producer.join();
    EXPECT_FALSE(send_result.load()) << "send into a closed channel must return false";
}

// --- Channel: size reflects queued count ------------------------------------
TEST(ChannelDeepening, SizeReflectsQueuedCount)
{
    cd::concurrency::Channel<int> ch { 8 };
    EXPECT_EQ(ch.size(), 0u);
    ASSERT_TRUE(ch.try_send(1));
    ASSERT_TRUE(ch.try_send(2));
    EXPECT_EQ(ch.size(), 2u);
    (void)ch.try_receive();
    EXPECT_EQ(ch.size(), 1u);
}

// --- EventBus: multiple subscribers fire in registration order --------------
TEST(EventBusDeepening, MultipleSubscribersFireInOrder)
{
    struct Ev { int x { 0 }; };
    cd::concurrency::EventBus bus;
    std::vector<int> order;
    bus.subscribe<Ev>([&](const Ev& e) { order.push_back(e.x + 0); });
    bus.subscribe<Ev>([&](const Ev& e) { order.push_back(e.x + 1); });
    bus.subscribe<Ev>([&](const Ev& e) { order.push_back(e.x + 2); });
    EXPECT_EQ(bus.handler_count(), 3u);

    bus.publish(Ev { 10 });
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 10);
    EXPECT_EQ(order[1], 11);
    EXPECT_EQ(order[2], 12);
}

// --- EventBus: unsubscribe a nonexistent handle returns false ---------------
TEST(EventBusDeepening, UnsubscribeNonexistentReturnsFalse)
{
    cd::concurrency::EventBus bus;
    EXPECT_FALSE(bus.unsubscribe(99999u));
}

// --- EventBus: clear removes all handlers across types ----------------------
TEST(EventBusDeepening, ClearRemovesAllHandlers)
{
    struct A { };
    struct B { };
    cd::concurrency::EventBus bus;
    bus.subscribe<A>([](const A&) {});
    bus.subscribe<B>([](const B&) {});
    bus.subscribe<B>([](const B&) {});
    EXPECT_EQ(bus.handler_count(), 3u);
    bus.clear();
    EXPECT_EQ(bus.handler_count(), 0u);
    // Publishing after clear hits nothing (no crash).
    bus.publish(A {});
    SUCCEED();
}

// --- Future: many concurrent waiters all observe the set value --------------
TEST(FutureDeepening, ManyWaitersObserveSetValue)
{
    constexpr int kWaiters = 8;
    cd::concurrency::Promise<int> p;
    auto f = p.future();

    cd::concurrency::Latch ready { kWaiters };
    std::atomic<int> correct { 0 };
    std::vector<std::thread> ts;
    ts.reserve(kWaiters);
    for (int i = 0; i < kWaiters; ++i)
    {
        ts.emplace_back(
            [&]
            {
                ready.count_down();
                if (f.get() == 99)
                    correct.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    ready.wait();             // all waiters parked (or about to park) on wait()
    p.set(99);
    for (auto& t : ts)
        t.join();
    EXPECT_EQ(correct.load(), kWaiters);
    EXPECT_TRUE(f.is_ready());
}

// --- Future: try_get transitions empty -> value -----------------------------
TEST(FutureDeepening, TryGetTransitionsOnSet)
{
    cd::concurrency::Promise<int> p;
    auto f = p.future();
    EXPECT_FALSE(f.try_get().has_value());
    p.set(7);
    auto v = f.try_get();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 7);
}

// --- JobToken: complete() also unblocks wait() ------------------------------
TEST(JobTokenDeepening, CompleteUnblocksWait)
{
    cd::concurrency::JobToken t;
    cd::concurrency::Latch entered { 1 };
    std::thread waiter {
        [&]
        {
            entered.count_down();
            t.wait();
        }
    };
    entered.wait();
    t.complete();
    waiter.join();
    EXPECT_TRUE(t.is_complete());
    EXPECT_FALSE(t.is_cancelled());
    EXPECT_TRUE(t.is_settled());
}

// --- JobToken: double-cancel is idempotent ----------------------------------
TEST(JobTokenDeepening, DoubleCancelIdempotent)
{
    cd::concurrency::JobToken t;
    t.cancel();
    t.cancel();  // no-op
    EXPECT_TRUE(t.is_cancelled());
    EXPECT_TRUE(t.is_settled());
    t.wait();  // already settled => returns immediately
    SUCCEED();
}

// --- DeterministicExecutor: pump_until respects the iteration cap -----------
// A body that always resubmits would loop forever; max_iterations must bound it.
TEST(DeterministicExecutorDeepening, PumpUntilRespectsMaxIterations)
{
    cd::concurrency::DeterministicExecutor ex;
    std::atomic<int> ran { 0 };
    std::function<void()> chain = [&]
    {
        ran.fetch_add(1, std::memory_order_relaxed);
        ex.submit(chain);  // never-ending resubmission
    };
    ex.submit(chain);
    // pred always true => only the iteration cap stops it. Each drain pass runs
    // exactly one body (it resubmits one), so total == cap.
    const auto total = ex.pump_until([] { return true; }, /*max_iterations=*/16);
    EXPECT_EQ(total, 16u) << "max_iterations did not bound the pump";
    EXPECT_EQ(ran.load(), 16);
    EXPECT_EQ(ex.pending(), 1u) << "one resubmitted body should remain queued";
}

// --- RingBuffer: wrap-around across the modulo boundary keeps FIFO ----------
// Capacity 4 (kSlots = 5, non-pow2 => modulo path). Cycle fill/drain several
// times so head/tail wrap past kSlots; FIFO order must hold every cycle.
TEST(RingBufferDeepening, WrapAroundPreservesFifo)
{
    cd::concurrency::RingBuffer<int, 4> rb;
    int next = 0;
    for (int cycle = 0; cycle < 7; ++cycle)
    {
        // Fill to capacity.
        for (int i = 0; i < 4; ++i)
            ASSERT_TRUE(rb.push(next++));
        EXPECT_FALSE(rb.push(-1)) << "buffer should be full";
        // Drain in FIFO order.
        for (int i = 0; i < 4; ++i)
        {
            int out = -999;
            ASSERT_TRUE(rb.pop(out));
            EXPECT_EQ(out, next - 4 + i) << "FIFO broken at cycle " << cycle;
        }
        EXPECT_TRUE(rb.empty());
    }
}

// --- DataChannel: wrap-around + write_available accounting -------------------
TEST(DataChannelDeepening, WrapAroundAndWriteAvailable)
{
    cd::concurrency::DataChannel<int> ch { "wrap", 4 };
    EXPECT_EQ(ch.write_available(), 4u);
    int next = 0;
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        for (int i = 0; i < 4; ++i)
            ASSERT_TRUE(ch.push(next++));
        EXPECT_EQ(ch.write_available(), 0u);
        EXPECT_FALSE(ch.push(-1));
        for (int i = 0; i < 4; ++i)
        {
            int out = -999;
            ASSERT_TRUE(ch.pop(out));
            EXPECT_EQ(out, next - 4 + i);
        }
        EXPECT_EQ(ch.write_available(), 4u);
    }
}

// --- Flag: try_raise reports previous; cross-thread wait_until_raised --------
TEST(FlagDeepening, TryRaiseReportsPreviousAndWaitUnblocks)
{
    cd::concurrency::Flag f;
    EXPECT_FALSE(f.try_raise());  // was false
    EXPECT_TRUE(f.try_raise());   // now true
    f.lower();

    cd::concurrency::Latch entered { 1 };
    std::thread raiser {
        [&]
        {
            entered.count_down();
            f.raise();
        }
    };
    entered.wait();
    f.wait_until_raised();  // spins via Backoff until the raiser stores true
    EXPECT_TRUE(f.is_raised());
    raiser.join();
}

// --- Once: concurrent call() runs the body exactly once ---------------------
TEST(OnceDeepening, ConcurrentCallRunsBodyOnce)
{
    constexpr int kThreads = 8;
    cd::concurrency::Once once;
    std::atomic<int> n { 0 };
    cd::concurrency::Latch start { kThreads };
    std::vector<std::thread> ts;
    ts.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        ts.emplace_back(
            [&]
            {
                start.count_down();
                start.wait();  // release all threads together to maximise the race
                once.call([&] { n.fetch_add(1, std::memory_order_relaxed); });
            }
        );
    }
    for (auto& t : ts)
        t.join();
    EXPECT_EQ(n.load(), 1) << "Once::call ran the body more than once under contention";
    EXPECT_TRUE(once.is_done());
}

// --- SpinLock: try_lock from another thread fails while held ----------------
TEST(SpinLockDeepening, TryLockFailsWhileHeldByOtherThread)
{
    cd::concurrency::SpinLock m;
    m.lock();

    cd::concurrency::Latch done { 1 };
    std::atomic<bool> acquired { true };
    std::thread other {
        [&]
        {
            acquired.store(m.try_lock(), std::memory_order_release);
            done.count_down();
        }
    };
    done.wait();
    other.join();
    EXPECT_FALSE(acquired.load()) << "try_lock succeeded on a held lock";
    m.unlock();
    EXPECT_TRUE(m.try_lock());  // now free
    m.unlock();
}

}  // namespace
