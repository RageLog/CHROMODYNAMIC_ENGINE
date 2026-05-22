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
    std::atomic<int> a { 0 }, b { 0 };
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
    EXPECT_FALSE(a.active());
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

}  // namespace
