// =============================================================================
// CHROMODYNAMIC — cd::net::QosDispatcher tests (Phase 468 / M0)
// =============================================================================
#include <cd/net/QosDispatcher.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::net
{
extern const char* qos_dispatcher_translation_unit() noexcept;
}

namespace
{

using namespace std::chrono_literals;
using cd::net::QosBudget;
using cd::net::QosDispatcher;
using cd::net::QosTier;

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view s)
{
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

TEST(NetQos, PriorityOrderingReliableBeatsUnreliable)
{
    QosDispatcher d;
    EXPECT_TRUE(d.enqueue(QosTier::kUnreliable,          bytes_of("U1")).has_value());
    EXPECT_TRUE(d.enqueue(QosTier::kUnreliableSequenced, bytes_of("S1")).has_value());
    EXPECT_TRUE(d.enqueue(QosTier::kReliable,            bytes_of("R1")).has_value());

    std::vector<std::pair<QosTier, std::string>> seen;
    const auto now = QosDispatcher::Clock::now();
    auto n = d.tick(now, [&](QosTier t, std::span<const std::byte> p) {
        seen.emplace_back(t, std::string {
            reinterpret_cast<const char*>(p.data()), p.size() });
        return true;
    });
    EXPECT_EQ(n, 3U);
    ASSERT_EQ(seen.size(), 3U);
    EXPECT_EQ(seen[0].first, QosTier::kReliable);
    EXPECT_EQ(seen[0].second, "R1");
    EXPECT_EQ(seen[1].first, QosTier::kUnreliableSequenced);
    EXPECT_EQ(seen[2].first, QosTier::kUnreliable);
}

TEST(NetQos, TokenBucketGatesBurst)
{
    QosDispatcher d;
    // 1 token capacity, 1 token / sec refill — only 1 message per tick
    // burst then has to wait.
    d.set_budget(QosTier::kUnreliable, QosBudget { /*refill*/ 1.0, /*burst*/ 1.0, 0 });

    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(d.enqueue(QosTier::kUnreliable, bytes_of("x")).has_value());

    const auto t0 = QosDispatcher::Clock::now();
    auto n0 = d.tick(t0, [](QosTier, std::span<const std::byte>) { return true; });
    EXPECT_EQ(n0, 1U);  // burst budget exhausted after 1.
    EXPECT_EQ(d.pending(QosTier::kUnreliable), 4U);

    // After 2 seconds elapsed → 2 tokens added (clamped to burst=1) so
    // only 1 more dispatches.
    auto n1 = d.tick(t0 + 2s, [](QosTier, std::span<const std::byte>) { return true; });
    EXPECT_EQ(n1, 1U);
    EXPECT_EQ(d.pending(QosTier::kUnreliable), 3U);
}

TEST(NetQos, InfiniteBudgetWhenRefillRateZero)
{
    QosDispatcher d;
    for (int i = 0; i < 10; ++i)
        EXPECT_TRUE(d.enqueue(QosTier::kReliable, bytes_of("r")).has_value());

    auto n = d.tick(QosDispatcher::Clock::now(),
                    [](QosTier, std::span<const std::byte>) { return true; });
    EXPECT_EQ(n, 10U);
    EXPECT_EQ(d.pending(QosTier::kReliable), 0U);
}

TEST(NetQos, SinkRejectionPausesDispatchPreservesOrder)
{
    QosDispatcher d;
    EXPECT_TRUE(d.enqueue(QosTier::kReliable, bytes_of("A")).has_value());
    EXPECT_TRUE(d.enqueue(QosTier::kReliable, bytes_of("B")).has_value());
    EXPECT_TRUE(d.enqueue(QosTier::kReliable, bytes_of("C")).has_value());

    int delivered = 0;
    auto n = d.tick(QosDispatcher::Clock::now(),
                    [&](QosTier, std::span<const std::byte>) {
                        ++delivered;
                        return delivered < 2;  // accept first, reject second
                    });
    EXPECT_EQ(n, 1U);  // only "A" got through; "B" rejected, "C" not tried
    EXPECT_EQ(d.pending(QosTier::kReliable), 2U);

    // Resume — remaining two should come through in order.
    std::string seq;
    n = d.tick(QosDispatcher::Clock::now(),
               [&](QosTier, std::span<const std::byte> p) {
                   seq.append(reinterpret_cast<const char*>(p.data()), p.size());
                   return true;
               });
    EXPECT_EQ(n, 2U);
    EXPECT_EQ(seq, "BC");
}

TEST(NetQos, ReliableQueueFullReturnsError)
{
    QosDispatcher d { /*max_queue_per_tier=*/2 };
    EXPECT_TRUE(d.enqueue(QosTier::kReliable, bytes_of("1")).has_value());
    EXPECT_TRUE(d.enqueue(QosTier::kReliable, bytes_of("2")).has_value());
    auto r = d.enqueue(QosTier::kReliable, bytes_of("3"));
    EXPECT_FALSE(r.has_value());
}

TEST(NetQos, UnreliableQueueFullEvictsOldest)
{
    QosDispatcher d { /*max_queue_per_tier=*/2 };
    EXPECT_TRUE(d.enqueue(QosTier::kUnreliable, bytes_of("1")).has_value());
    EXPECT_TRUE(d.enqueue(QosTier::kUnreliable, bytes_of("2")).has_value());
    // Third enqueue must succeed but evict the oldest.
    EXPECT_TRUE(d.enqueue(QosTier::kUnreliable, bytes_of("3")).has_value());
    EXPECT_EQ(d.pending(QosTier::kUnreliable), 2U);
    EXPECT_EQ(d.stats(QosTier::kUnreliable).dropped_queue_overflow, 1U);

    std::string seq;
    (void)d.tick(QosDispatcher::Clock::now(),
                 [&](QosTier, std::span<const std::byte> p) {
                     seq.append(reinterpret_cast<const char*>(p.data()), p.size());
                     return true;
                 });
    EXPECT_EQ(seq, "23");  // "1" evicted
}

TEST(NetQos, SymbolPresentInLibrary)
{
    EXPECT_NE(cd::net::qos_dispatcher_translation_unit(), nullptr);
}

}  // namespace
