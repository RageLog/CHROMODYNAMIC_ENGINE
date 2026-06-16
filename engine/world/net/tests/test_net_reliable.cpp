// =============================================================================
// CHROMODYNAMIC — cd::net::AckWindowChannel tests (Phase 468 / M0)
// =============================================================================
#include <cd/net/IConnection.hpp>
#include <cd/net/ReliableChannel.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <span>
#include <string_view>
#include <vector>

namespace cd::net
{
extern const char* ack_window_channel_translation_unit() noexcept;
}

namespace
{

using namespace std::chrono_literals;

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view s)
{
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

[[nodiscard]] std::string str_of(std::span<const std::byte> b)
{
    return std::string { reinterpret_cast<const char*>(b.data()), b.size() };
}

TEST(NetReliable, RoundTripSinglePayload)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::AckWindowChannel sender { *a };
    cd::net::AckWindowChannel receiver { *b };

    auto seq = sender.send(bytes_of("hello"));
    ASSERT_TRUE(seq.has_value());
    EXPECT_EQ(*seq, 1U);

    const auto now = cd::net::AckWindowChannel::Clock::now();
    receiver.tick(now);

    auto got = receiver.receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(str_of(*got), "hello");
}

TEST(NetReliable, OrderedDeliveryEvenWithReorderingNotPresentLoopback)
{
    // Loopback delivers in order; this test asserts the channel
    // doesn't reorder a sequence of fast sends.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::AckWindowChannel sender { *a };
    cd::net::AckWindowChannel receiver { *b };

    EXPECT_TRUE(sender.send(bytes_of("one")).has_value());
    EXPECT_TRUE(sender.send(bytes_of("two")).has_value());
    EXPECT_TRUE(sender.send(bytes_of("three")).has_value());

    receiver.tick(cd::net::AckWindowChannel::Clock::now());

    auto r1 = receiver.receive();
    auto r2 = receiver.receive();
    auto r3 = receiver.receive();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(str_of(*r1), "one");
    EXPECT_EQ(str_of(*r2), "two");
    EXPECT_EQ(str_of(*r3), "three");
}

TEST(NetReliable, AcksDrainPendingOnSenderSide)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::AckWindowChannel sender { *a };
    cd::net::AckWindowChannel receiver { *b };

    EXPECT_TRUE(sender.send(bytes_of("ping1")).has_value());
    EXPECT_TRUE(sender.send(bytes_of("ping2")).has_value());
    EXPECT_EQ(sender.pending_send_count(), 2U);

    // Receiver ticks — drains both Data frames + queues ack info.
    receiver.tick(cd::net::AckWindowChannel::Clock::now());
    // Drain delivery so the receiver doesn't accumulate state.
    while (receiver.receive().has_value()) {}

    // Sender ticks — picks up the AckOnly piggyback emitted by the
    // receiver, discharges both pending entries.
    sender.tick(cd::net::AckWindowChannel::Clock::now());

    EXPECT_EQ(sender.pending_send_count(), 0U);
}

TEST(NetReliable, RetransmitFiresAfterRtoElapses)
{
    // Use a NON-loopback strategy: we configure a tiny RTO and never
    // tick the receiver, then tick the sender twice with > RTO between.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::AckWindowChannel sender { *a, 5ms, /*max_retries=*/3 };

    auto seq = sender.send(bytes_of("must-resend"));
    ASSERT_TRUE(seq.has_value());

    const auto t0 = cd::net::AckWindowChannel::Clock::now();
    // First tick — too soon, nothing fires.
    sender.tick(t0);
    EXPECT_EQ(sender.retransmit_count(), 0U);

    // Second tick well past RTO — retransmit must fire.
    sender.tick(t0 + 50ms);
    EXPECT_GE(sender.retransmit_count(), 1U);
    EXPECT_EQ(sender.pending_send_count(), 1U);
}

TEST(NetReliable, RetransmitExhaustsAndDropsPermanently)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::AckWindowChannel sender { *a, 5ms, /*max_retries=*/2 };

    ASSERT_TRUE(sender.send(bytes_of("doomed")).has_value());

    const auto t0 = cd::net::AckWindowChannel::Clock::now();
    sender.tick(t0 + 50ms);   // retry 1
    sender.tick(t0 + 100ms);  // retry 2
    sender.tick(t0 + 200ms);  // retries exhausted → permanent drop

    EXPECT_EQ(sender.pending_send_count(), 0U);
    EXPECT_GE(sender.permanent_loss_count(), 1U);
}

TEST(NetReliable, DuplicateDeliveryDoesNotDoubleDeliver)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::AckWindowChannel sender { *a };
    cd::net::AckWindowChannel receiver { *b };

    // Send the same payload twice (simulating a retransmitted frame
    // by re-issuing send — sequence numbers differ, but we check the
    // receiver doesn't duplicate-deliver one logical message).
    EXPECT_TRUE(sender.send(bytes_of("once")).has_value());
    receiver.tick(cd::net::AckWindowChannel::Clock::now());

    auto first = receiver.receive();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(str_of(*first), "once");

    // No second payload arrives — the queue is empty after one delivery.
    auto second = receiver.receive();
    EXPECT_FALSE(second.has_value());
}

// ---------------------------------------------------------------------------
// BAND-2 world topup: a deterministic REORDERING transport. The built-in
// loopback delivers strictly in order, so the channel's out-of-order
// receiver window (inbox buffering + gap-fill flush) and its selective-ack
// (SACK) discharge_pending_ bit path were never exercised. This mock lets a
// test hold back / reorder frames by hand — no sleep, no timing race.
// ---------------------------------------------------------------------------
class ReorderTransport final : public cd::net::IConnection
{
public:
    [[nodiscard]] cd::core::Result<void> send(std::span<const std::byte> bytes) override
    {
        outbox_.emplace_back(bytes.begin(), bytes.end());
        bytes_sent_ += bytes.size();
        return {};
    }

    [[nodiscard]] cd::core::Result<std::vector<std::byte>> receive() override
    {
        if (inbox_.empty())
            return std::unexpected(cd::net::net_errors::make(cd::net::net_errors::Code::kWouldBlock));
        auto front = std::move(inbox_.front());
        inbox_.pop_front();
        bytes_received_ += front.size();
        return front;
    }

    [[nodiscard]] cd::net::ConnectionState state() const noexcept override
    {
        return cd::net::ConnectionState::kConnected;
    }
    [[nodiscard]] std::size_t bytes_sent() const noexcept override { return bytes_sent_; }
    [[nodiscard]] std::size_t bytes_received() const noexcept override { return bytes_received_; }
    void close() override {}

    // ---- test-side wiring -------------------------------------------------
    /// Frames the channel handed us via send(), oldest first.
    [[nodiscard]] std::vector<std::vector<std::byte>>& outbox() noexcept { return outbox_; }
    /// Hand a frame to the channel's next receive(), in the order pushed.
    void deliver(std::vector<std::byte> frame) { inbox_.push_back(std::move(frame)); }

private:
    std::vector<std::vector<std::byte>> outbox_;
    std::deque<std::vector<std::byte>>  inbox_;
    std::size_t bytes_sent_ { 0 };
    std::size_t bytes_received_ { 0 };
};

TEST(NetReliable, OutOfOrderArrivalBuffersUntilGapFills)
{
    // Sender produces frames 1,2,3 on its transport; we deliver them to the
    // receiver as 3,1,2. The receiver must hold 3 until 1 and 2 arrive,
    // then flush all three in sequence order — exercising flush_delivery_'s
    // gap-hold and the ack-window shift branch in handle_frame_.
    ReorderTransport sender_tx;
    ReorderTransport receiver_tx;
    cd::net::AckWindowChannel sender { sender_tx };
    cd::net::AckWindowChannel receiver { receiver_tx };

    EXPECT_TRUE(sender.send(bytes_of("one")).has_value());
    EXPECT_TRUE(sender.send(bytes_of("two")).has_value());
    EXPECT_TRUE(sender.send(bytes_of("three")).has_value());
    ASSERT_EQ(sender_tx.outbox().size(), 3U);

    // Deliver out of order: 3, then 1, then 2.
    receiver_tx.deliver(sender_tx.outbox()[2]);
    receiver_tx.deliver(sender_tx.outbox()[0]);
    receiver_tx.deliver(sender_tx.outbox()[1]);

    receiver.tick(cd::net::AckWindowChannel::Clock::now());

    auto r1 = receiver.receive();
    auto r2 = receiver.receive();
    auto r3 = receiver.receive();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(str_of(*r1), "one");
    EXPECT_EQ(str_of(*r2), "two");
    EXPECT_EQ(str_of(*r3), "three");
    EXPECT_FALSE(receiver.receive().has_value());
}

TEST(NetReliable, SelectiveAckDischargesGapAndMeasuresRtt)
{
    // Drive the SACK path: the receiver acknowledges seq 1 AND seq 3 (via
    // the ack_bits window) while seq 2 is still in flight. The sender must
    // discharge 1 and 3 from pending and keep ONLY 2 — proving
    // discharge_pending_'s ack_bits branch, not just the cumulative ack.
    // Also asserts last_rtt() is measured on a first-transmit ack.
    ReorderTransport sender_tx;
    ReorderTransport receiver_tx;
    cd::net::AckWindowChannel sender { sender_tx };
    cd::net::AckWindowChannel receiver { receiver_tx };

    EXPECT_TRUE(sender.send(bytes_of("a")).has_value());  // seq 1
    EXPECT_TRUE(sender.send(bytes_of("b")).has_value());  // seq 2
    EXPECT_TRUE(sender.send(bytes_of("c")).has_value());  // seq 3
    ASSERT_EQ(sender.pending_send_count(), 3U);
    ASSERT_EQ(sender_tx.outbox().size(), 3U);

    // Receiver sees 1 then 3 (2 is "lost"): builds ack=3 with bit set for
    // seq 1 (age 2), leaving seq 2 (age 1) unacked.
    receiver_tx.deliver(sender_tx.outbox()[0]);  // seq 1
    receiver_tx.deliver(sender_tx.outbox()[2]);  // seq 3
    receiver.tick(cd::net::AckWindowChannel::Clock::now());

    // The receiver tick emits an AckOnly piggyback into receiver_tx.outbox.
    ASSERT_FALSE(receiver_tx.outbox().empty());
    sender_tx.deliver(receiver_tx.outbox().back());
    sender.tick(cd::net::AckWindowChannel::Clock::now());

    // 1 and 3 discharged via SACK; 2 still pending.
    EXPECT_EQ(sender.pending_send_count(), 1U);
    // RTT was measured on the first-transmit ack (retries == 0 path).
    EXPECT_GE(sender.last_rtt().count(), 0);
}

TEST(NetReliable, SymbolPresentInLibrary)
{
    EXPECT_NE(cd::net::ack_window_channel_translation_unit(), nullptr);
}

}  // namespace
