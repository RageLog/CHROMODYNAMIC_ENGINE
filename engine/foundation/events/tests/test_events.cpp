// =============================================================================
// CHROMODYNAMIC — cd::events tests (Sprint S2.2)
// =============================================================================
#include <cd/events/EventBus.hpp>
#include <cd/events/EventRecorder.hpp>
#include <cd/events/ScopedConnection.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct PingEvent
{
    int value { 0 };
};

struct PongEvent
{
    std::string label;
};

// --- EventBus basic ---------------------------------------------------------
TEST(EventBus, EmptyBusPublishReturnsZero)
{
    cd::events::EventBus bus;
    EXPECT_EQ(bus.publish(PingEvent { 42 }), 0u);
    EXPECT_EQ(bus.subscriber_count(), 0u);
}

TEST(EventBus, SubscribeAndPublish)
{
    cd::events::EventBus bus;
    int received = 0;
    auto conn = bus.subscribe<PingEvent>(
        [&](const PingEvent& e)
        {
            received = e.value;
        }
    );
    EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 1u);
    EXPECT_EQ(bus.publish(PingEvent { 7 }), 1u);
    EXPECT_EQ(received, 7);
}

TEST(EventBus, MultipleSubscribers)
{
    cd::events::EventBus bus;
    std::atomic<int> a { 0 };
    std::atomic<int> b { 0 };
    auto c1 = bus.subscribe<PingEvent>(
        [&](const PingEvent& e)
        {
            a += e.value;
        }
    );
    auto c2 = bus.subscribe<PingEvent>(
        [&](const PingEvent& e)
        {
            b += e.value;
        }
    );
    EXPECT_EQ(bus.publish(PingEvent { 5 }), 2u);
    EXPECT_EQ(a.load(), 5);
    EXPECT_EQ(b.load(), 5);
}

TEST(EventBus, TypeIsolation)
{
    cd::events::EventBus bus;
    std::atomic<int> pings { 0 };
    std::atomic<int> pongs { 0 };
    auto c1 = bus.subscribe<PingEvent>(
        [&](const PingEvent&)
        {
            ++pings;
        }
    );
    auto c2 = bus.subscribe<PongEvent>(
        [&](const PongEvent&)
        {
            ++pongs;
        }
    );
    bus.publish(PingEvent { 1 });
    EXPECT_EQ(pings.load(), 1);
    EXPECT_EQ(pongs.load(), 0);
    bus.publish(PongEvent { "hi" });
    EXPECT_EQ(pings.load(), 1);
    EXPECT_EQ(pongs.load(), 1);
}

// --- ScopedConnection ------------------------------------------------------
TEST(ScopedConnection, RaiiUnsubscribe)
{
    cd::events::EventBus bus;
    std::atomic<int> hits { 0 };
    {
        auto conn = bus.subscribe<PingEvent>(
            [&](const PingEvent&)
            {
                ++hits;
            }
        );
        EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 1u);
        bus.publish(PingEvent {});
        EXPECT_EQ(hits.load(), 1);
    }
    EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 0u);
    bus.publish(PingEvent {});  // no-op
    EXPECT_EQ(hits.load(), 1);
}

TEST(ScopedConnection, ReleaseEarly)
{
    cd::events::EventBus bus;
    std::atomic<int> hits { 0 };
    auto conn = bus.subscribe<PingEvent>(
        [&](const PingEvent&)
        {
            ++hits;
        }
    );
    conn.release();
    EXPECT_FALSE(conn.active());
    bus.publish(PingEvent {});
    EXPECT_EQ(hits.load(), 0);
}

TEST(ScopedConnection, MoveTransfersOwnership)
{
    cd::events::EventBus bus;
    std::atomic<int> hits { 0 };
    auto a = bus.subscribe<PingEvent>(
        [&](const PingEvent&)
        {
            ++hits;
        }
    );
    auto b = std::move(a);
    // Intentional use-after-move: the contract under test is precisely that a
    // moved-from ScopedConnection is left inert (active() == false). The
    // moved-from read is deliberate, not a bug.
    EXPECT_FALSE(a.active());  // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
    EXPECT_TRUE(b.active());
    bus.publish(PingEvent {});
    EXPECT_EQ(hits.load(), 1);
}

TEST(EventBus, HandlerCanSubscribeDuringPublish)
{
    cd::events::EventBus bus;
    std::vector<cd::events::ScopedConnection> kept;
    std::atomic<int> outer { 0 };
    std::atomic<int> inner { 0 };
    auto c0 = bus.subscribe<PingEvent>(
        [&](const PingEvent& e)
        {
            ++outer;
            if (e.value == 1)
            {
                kept.push_back(bus.subscribe<PingEvent>(
                    [&](const PingEvent&)
                    {
                        ++inner;
                    }
                ));
            }
        }
    );
    bus.publish(PingEvent { 1 });  // outer fires; nested subscribe happens
    EXPECT_EQ(outer.load(), 1);
    EXPECT_EQ(inner.load(), 0);    // nested handler not invoked retroactively
    bus.publish(PingEvent { 2 });
    EXPECT_EQ(inner.load(), 1);    // now both run
}

// --- EventRecorder ---------------------------------------------------------
TEST(EventRecorder, RecordAndSnapshot)
{
    cd::events::EventRecorder rec { 4 };
    rec.record("Ping", "v=1");
    rec.record("Ping", "v=2");
    EXPECT_EQ(rec.size(), 2u);
    auto snap = rec.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_EQ(snap[0].type, "Ping");
    EXPECT_EQ(snap[0].description, "v=1");
    EXPECT_EQ(snap[0].sequence, 1u);
    EXPECT_EQ(snap[1].sequence, 2u);
}

TEST(EventRecorder, RingBufferRetention)
{
    cd::events::EventRecorder rec { 3 };
    for (int i = 0; i < 10; ++i)
        rec.record("Tag", std::to_string(i));
    EXPECT_EQ(rec.size(), 3u);
    auto snap = rec.snapshot();
    EXPECT_EQ(snap.front().description, "7");
    EXPECT_EQ(snap.back().description, "9");
}

TEST(EventRecorder, Clear)
{
    cd::events::EventRecorder rec { 4 };
    rec.record("A", "x");
    rec.record("B", "y");
    rec.clear();
    EXPECT_EQ(rec.size(), 0u);
}

// --- Deferred publish (Wave 56) ---------------------------------------------

struct TickedEvent
{
    int seq { 0 };
};

TEST(EventBus, QueueAndDrainDeliversOnCallerThread)
{
    cd::events::EventBus bus;
    int hits = 0;
    auto sub = bus.subscribe<TickedEvent>([&](const TickedEvent& e) {
        ++hits;
        EXPECT_GE(e.seq, 0);
    });
    bus.queue_publish<TickedEvent>(TickedEvent { 1 });
    bus.queue_publish<TickedEvent>(TickedEvent { 2 });
    bus.queue_publish<TickedEvent>(TickedEvent { 3 });
    EXPECT_EQ(bus.queued_count(), 3U);
    EXPECT_EQ(hits, 0);  // not delivered yet

    const auto n = bus.drain();
    EXPECT_EQ(n, 3U);
    EXPECT_EQ(hits, 3);
    EXPECT_EQ(bus.queued_count(), 0U);
}

TEST(EventBus, DrainEmptyQueueIsZero)
{
    cd::events::EventBus bus;
    EXPECT_EQ(bus.drain(), 0U);
}

TEST(EventBus, QueueIsThreadSafe)
{
    cd::events::EventBus bus;
    std::atomic<int> hits { 0 };
    auto sub = bus.subscribe<TickedEvent>([&](const TickedEvent&) { ++hits; });

    constexpr int kProducers = 4;
    constexpr int kPerProducer = 250;
    std::vector<std::thread> threads;
    threads.reserve(kProducers);
    for (int t = 0; t < kProducers; ++t)
    {
        threads.emplace_back([&] {
            for (int i = 0; i < kPerProducer; ++i)
                bus.queue_publish<TickedEvent>(TickedEvent { i });
        });
    }
    for (auto& th : threads)
        th.join();
    EXPECT_EQ(bus.queued_count(), static_cast<std::size_t>(kProducers * kPerProducer));
    const auto drained = bus.drain();
    EXPECT_EQ(drained, static_cast<std::size_t>(kProducers * kPerProducer));
    EXPECT_EQ(hits.load(), kProducers * kPerProducer);
}

// --- Priority-ordered delivery (Band-2 foundation-to-100) -------------------

TEST(EventBus, HigherPriorityRunsFirst)
{
    cd::events::EventBus bus;
    std::vector<int> order;
    // Subscribe LOW first, then HIGH — priority, not subscription order,
    // must determine delivery sequence.
    auto low = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(1); }, 1);
    auto high = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(100); }, 100);
    auto mid = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(50); }, 50);

    EXPECT_EQ(bus.publish(PingEvent {}), 3u);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 100);
    EXPECT_EQ(order[1], 50);
    EXPECT_EQ(order[2], 1);
}

TEST(EventBus, EqualPriorityKeepsSubscriptionOrder)
{
    cd::events::EventBus bus;
    std::vector<int> order;
    // Three handlers at the SAME priority — must run in subscription order.
    auto a = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(1); }, 10);
    auto b = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(2); }, 10);
    auto c = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(3); }, 10);

    bus.publish(PingEvent {});
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(EventBus, DefaultOverloadIsPriorityZeroFifo)
{
    cd::events::EventBus bus;
    std::vector<int> order;
    // A default (no-priority) subscribe is priority 0; a positive-priority
    // handler must precede it, and a negative-priority handler must follow.
    auto def = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(0); });
    auto pos = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(5); }, 5);
    auto neg = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(-5); }, -5);

    bus.publish(PingEvent {});
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 5);
    EXPECT_EQ(order[1], 0);
    EXPECT_EQ(order[2], -5);
}

TEST(EventBus, PriorityUnsubscribeRemovesCorrectEntry)
{
    cd::events::EventBus bus;
    std::vector<int> order;
    auto high = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(100); }, 100);
    {
        auto mid = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(50); }, 50);
        EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 2u);
    }
    // mid unsubscribed via RAII; only the high-priority handler remains, and
    // the bucket ordering of the survivors is intact.
    EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 1u);
    bus.publish(PingEvent {});
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 100);
}

// ---------------------------------------------------------------------------
// Additional edge / negative / ordering tests (100% depth close-out)
// ---------------------------------------------------------------------------

// --- ScopedConnection extra coverage ----------------------------------------

TEST(ScopedConnection, DefaultConstructIsInactive)
{
    // A default-constructed ScopedConnection must not be active and must have id 0.
    cd::events::ScopedConnection conn;
    EXPECT_FALSE(conn.active());
    EXPECT_EQ(conn.id(), 0u);
}

TEST(ScopedConnection, DoubleRelease)
{
    // Calling release() twice must be safe (no crash, no double-unsubscribe).
    cd::events::EventBus bus;
    std::atomic<int> hits { 0 };
    auto conn = bus.subscribe<PingEvent>([&](const PingEvent&) { ++hits; });
    conn.release();
    conn.release();  // second release must be a no-op
    EXPECT_FALSE(conn.active());
    bus.publish(PingEvent {});
    EXPECT_EQ(hits.load(), 0);
}

TEST(ScopedConnection, MoveAssignTransfersAndReleasesOld)
{
    // Move-assign: old connection must be released; new connection takes ownership.
    cd::events::EventBus bus;
    std::atomic<int> hits_a { 0 };
    std::atomic<int> hits_b { 0 };
    auto conn_a = bus.subscribe<PingEvent>([&](const PingEvent&) { ++hits_a; });
    auto conn_b = bus.subscribe<PingEvent>([&](const PingEvent&) { ++hits_b; });

    // Overwrite conn_a with conn_b; conn_a's subscription must be released.
    conn_a = std::move(conn_b);
    // NOLINT: use-after-move is intentional — verifying the moved-from state.
    EXPECT_FALSE(conn_b.active());  // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
    EXPECT_TRUE(conn_a.active());

    // Only hits_b handler should fire now; hits_a was released by move-assign.
    bus.publish(PingEvent {});
    EXPECT_EQ(hits_a.load(), 0);
    EXPECT_EQ(hits_b.load(), 1);
}

TEST(ScopedConnection, MoveAssignSelf)
{
    // Self-assignment must be safe (no double-unsubscribe, stays active).
    cd::events::EventBus bus;
    std::atomic<int> hits { 0 };
    auto conn = bus.subscribe<PingEvent>([&](const PingEvent&) { ++hits; });
    // Suppress self-move warning: the test target is the operator== guard.
    auto& self = conn;
    conn = std::move(self);  // NOLINT(bugprone-use-after-move)
    bus.publish(PingEvent {});
    EXPECT_EQ(hits.load(), 1);
}

// --- Unsubscribe-during-emit ------------------------------------------------

TEST(EventBus, UnsubscribeDuringEmitSnapshotSafety)
{
    // A handler that releases its own ScopedConnection mid-publish must:
    //   (a) still complete its current invocation (snapshot isolation), and
    //   (b) not be invoked on the next publish().
    cd::events::EventBus bus;
    std::atomic<int> fires { 0 };
    cd::events::ScopedConnection conn;
    conn = bus.subscribe<PingEvent>(
        [&](const PingEvent&)
        {
            ++fires;
            conn.release();  // unsubscribe self
        }
    );
    EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 1u);

    bus.publish(PingEvent {});  // fires once, then self-removes
    EXPECT_EQ(fires.load(), 1);
    EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 0u);

    bus.publish(PingEvent {});  // must be a no-op
    EXPECT_EQ(fires.load(), 1);
}

TEST(EventBus, UnsubscribeOtherHandlerDuringEmit)
{
    // First handler unsubscribes a different handler during publish.
    // The removed handler must still fire for the current publish (snapshot) but
    // not for the next one.
    cd::events::EventBus bus;
    std::vector<int> order;
    cd::events::ScopedConnection victim;
    auto aggressor = bus.subscribe<PingEvent>(
        [&](const PingEvent&)
        {
            order.push_back(1);
            victim.release();  // kill the other handler mid-publish
        }
    );
    victim = bus.subscribe<PingEvent>([&](const PingEvent&) { order.push_back(2); });

    bus.publish(PingEvent {});           // both fire via snapshot
    EXPECT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 1u);  // victim gone

    order.clear();
    bus.publish(PingEvent {});           // only aggressor fires
    EXPECT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 1);
}

// --- Deferred drain ordering and reentrance ---------------------------------

TEST(EventBus, DrainOrderPreservesEnqueueOrder)
{
    // Events queued via queue_publish must be delivered in FIFO order during drain().
    cd::events::EventBus bus;
    std::vector<int> received;
    auto sub = bus.subscribe<TickedEvent>([&](const TickedEvent& e) { received.push_back(e.seq); });

    bus.queue_publish<TickedEvent>(TickedEvent { 10 });
    bus.queue_publish<TickedEvent>(TickedEvent { 20 });
    bus.queue_publish<TickedEvent>(TickedEvent { 30 });

    bus.drain();

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], 10);
    EXPECT_EQ(received[1], 20);
    EXPECT_EQ(received[2], 30);
}

TEST(EventBus, ReentrantPublishDuringDrain)
{
    // A handler invoked by drain() is allowed to call publish() synchronously.
    // The publish() must fire current live subscribers immediately; it must NOT
    // cause recursive drain() or deadlock.
    cd::events::EventBus bus;
    std::atomic<int> sync_hits { 0 };
    std::atomic<int> drain_hits { 0 };

    // Subscriber that publish() inside drain() will reach.
    auto sync_sub = bus.subscribe<PingEvent>([&](const PingEvent&) { ++sync_hits; });

    // Deferred handler fires a synchronous publish.
    auto ticked_sub = bus.subscribe<TickedEvent>(
        [&](const TickedEvent&)
        {
            ++drain_hits;
            bus.publish(PingEvent {});  // reentrant synchronous publish
        }
    );

    bus.queue_publish<TickedEvent>(TickedEvent { 1 });
    bus.queue_publish<TickedEvent>(TickedEvent { 2 });
    const auto drained = bus.drain();

    EXPECT_EQ(drained, 2u);
    EXPECT_EQ(drain_hits.load(), 2);
    EXPECT_EQ(sync_hits.load(), 2);  // one publish per deferred tick
}

TEST(EventBus, DrainWithNoSubscriberIsNoOp)
{
    // queue_publish then drain when nobody subscribed to that type must not crash
    // and must report the correct number of drained queue entries.
    cd::events::EventBus bus;
    bus.queue_publish<PingEvent>(PingEvent { 99 });
    EXPECT_EQ(bus.queued_count(), 1u);
    const auto n = bus.drain();
    EXPECT_EQ(n, 1u);
    EXPECT_EQ(bus.queued_count(), 0u);
}

// --- subscriber_count() total -----------------------------------------------

TEST(EventBus, SubscriberCountTotalAggregatesTypes)
{
    // subscriber_count() must sum across all event types.
    cd::events::EventBus bus;
    auto c1 = bus.subscribe<PingEvent>([](const PingEvent&) {});
    auto c2 = bus.subscribe<PingEvent>([](const PingEvent&) {});
    auto c3 = bus.subscribe<PongEvent>([](const PongEvent&) {});
    EXPECT_EQ(bus.subscriber_count(), 3u);
    EXPECT_EQ(bus.subscriber_count_for<PingEvent>(), 2u);
    EXPECT_EQ(bus.subscriber_count_for<PongEvent>(), 1u);
}

// --- Type isolation via deferred path ---------------------------------------

TEST(EventBus, DeferredTypeIsolation)
{
    // queue_publish<PingEvent> must NOT fire PongEvent subscribers after drain().
    cd::events::EventBus bus;
    std::atomic<int> pings { 0 };
    std::atomic<int> pongs { 0 };
    auto c1 = bus.subscribe<PingEvent>([&](const PingEvent&) { ++pings; });
    auto c2 = bus.subscribe<PongEvent>([&](const PongEvent&) { ++pongs; });

    bus.queue_publish<PingEvent>(PingEvent { 1 });
    bus.drain();

    EXPECT_EQ(pings.load(), 1);
    EXPECT_EQ(pongs.load(), 0);
}

// --- EventRecorder extra coverage -------------------------------------------

TEST(EventRecorder, CapacityReflectsCtorArgument)
{
    constexpr std::size_t kCap = 7u;
    cd::events::EventRecorder rec { kCap };
    EXPECT_EQ(rec.capacity(), kCap);
}

TEST(EventRecorder, SequenceMonotonicAfterClear)
{
    // After clear(), new records must continue the sequence counter (not restart
    // at 1). This preserves absolute ordering guarantees for external log
    // consumers that correlate sequence numbers.
    cd::events::EventRecorder rec { 16 };
    rec.record("A", "first");
    rec.record("A", "second");
    rec.clear();
    rec.record("A", "third");
    auto snap = rec.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_GT(snap[0].sequence, 2u);  // must be 3, not 1
}

}  // namespace
