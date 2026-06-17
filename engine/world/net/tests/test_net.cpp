// =============================================================================
// CHROMODYNAMIC — cd::net tests
// =============================================================================
#include <cd/net/ChannelMux.hpp>
#include <cd/net/IConnection.hpp>
#include <cd/net/Retransmit.hpp>
#include <cd/net/UdpConnection.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] std::vector<std::byte> bytes_of(std::string_view s)
{
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

TEST(Net, LoopbackPairIsConnected)
{
    auto [a, b] = cd::net::make_loopback_pair();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->state(), cd::net::ConnectionState::kConnected);
    EXPECT_EQ(b->state(), cd::net::ConnectionState::kConnected);
}

TEST(Net, ReceiveEmptyQueueReturnsWouldBlock)
{
    auto [a, b] = cd::net::make_loopback_pair();
    auto r = a->receive();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));
}

TEST(Net, SendOnAReceivesOnB)
{
    auto [a, b] = cd::net::make_loopback_pair();
    const auto payload = bytes_of("hello");
    ASSERT_TRUE(a->send(payload).has_value());
    auto recv = b->receive();
    ASSERT_TRUE(recv.has_value());
    ASSERT_EQ(recv->size(), 5U);
    EXPECT_EQ(static_cast<unsigned char>((*recv)[0]), 'h');
    EXPECT_EQ(a->bytes_sent(), 5U);
    EXPECT_EQ(b->bytes_received(), 5U);
}

TEST(Net, MessagesArePreservedInOrder)
{
    auto [a, b] = cd::net::make_loopback_pair();
    ASSERT_TRUE(a->send(bytes_of("alpha")).has_value());
    ASSERT_TRUE(a->send(bytes_of("beta")).has_value());
    auto r1 = b->receive();
    auto r2 = b->receive();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->size(), 5U);  // "alpha"
    EXPECT_EQ(r2->size(), 4U);  // "beta"
}

TEST(Net, BiDirectional)
{
    auto [a, b] = cd::net::make_loopback_pair();
    ASSERT_TRUE(a->send(bytes_of("ping")).has_value());
    ASSERT_TRUE(b->send(bytes_of("pong!")).has_value());
    auto on_b = b->receive();
    auto on_a = a->receive();
    ASSERT_TRUE(on_b.has_value());
    ASSERT_TRUE(on_a.has_value());
    EXPECT_EQ(on_b->size(), 4U);
    EXPECT_EQ(on_a->size(), 5U);
}

TEST(Net, CloseDisconnectsBothEndpoints)
{
    auto [a, b] = cd::net::make_loopback_pair();
    a->close();
    EXPECT_EQ(a->state(), cd::net::ConnectionState::kDisconnected);
    EXPECT_EQ(b->state(), cd::net::ConnectionState::kDisconnected);
    auto r = b->send(bytes_of("nope"));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::net::net_errors::Code::kDisconnected));
}

TEST(Net, PendingBytesDeliveredBeforeDisconnectError)
{
    auto [a, b] = cd::net::make_loopback_pair();
    ASSERT_TRUE(a->send(bytes_of("queued")).has_value());
    a->close();
    // The receiver should still drain queued messages; the disconnect
    // surfaces only when the queue empties.
    auto r = b->receive();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 6U);
    auto next = b->receive();
    ASSERT_FALSE(next.has_value());
    EXPECT_EQ(next.error().code, static_cast<std::uint32_t>(cd::net::net_errors::Code::kDisconnected));
}

// -----------------------------------------------------------------------------
// ChannelMux — typed multi-channel layer on top of IConnection
// -----------------------------------------------------------------------------

namespace
{

[[nodiscard]] std::span<const std::byte> bytes_span(const std::vector<std::byte>& v)
{
    return { v.data(), v.size() };
}

[[nodiscard]] std::string str_from_payload(const std::vector<std::byte>& p)
{
    std::string s(p.size(), '\0');
    if (!p.empty())
        std::memcpy(s.data(), p.data(), p.size());
    return s;
}

}  // namespace

TEST(ChannelMux, ReliableChannelPreservesOrder)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };

    const auto p1 = bytes_of("frame-0");
    const auto p2 = bytes_of("frame-1");
    const auto p3 = bytes_of("frame-2");
    ASSERT_TRUE(ma.send(3, cd::net::ChannelType::kReliableOrdered, bytes_span(p1)).has_value());
    ASSERT_TRUE(ma.send(3, cd::net::ChannelType::kReliableOrdered, bytes_span(p2)).has_value());
    ASSERT_TRUE(ma.send(3, cd::net::ChannelType::kReliableOrdered, bytes_span(p3)).has_value());

    auto r1 = mb.receive();
    auto r2 = mb.receive();
    auto r3 = mb.receive();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(r1->channel, 3U);
    EXPECT_EQ(r1->type, cd::net::ChannelType::kReliableOrdered);
    EXPECT_EQ(r1->sequence, 0U);
    EXPECT_EQ(str_from_payload(r1->payload), "frame-0");
    EXPECT_EQ(r2->sequence, 1U);
    EXPECT_EQ(str_from_payload(r2->payload), "frame-1");
    EXPECT_EQ(r3->sequence, 2U);
    EXPECT_EQ(str_from_payload(r3->payload), "frame-2");
}

TEST(ChannelMux, UnreliableChannelForwardsPayload)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };

    const auto p = bytes_of("voxel-tick");
    ASSERT_TRUE(ma.send(7, cd::net::ChannelType::kUnreliableUnordered, bytes_span(p)).has_value());
    auto r = mb.receive();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->channel, 7U);
    EXPECT_EQ(r->type, cd::net::ChannelType::kUnreliableUnordered);
    EXPECT_EQ(r->sequence, 0U);
    EXPECT_EQ(str_from_payload(r->payload), "voxel-tick");
}

TEST(ChannelMux, IndependentChannelsHaveIndependentSequences)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };

    const auto p = bytes_of("x");
    ASSERT_TRUE(ma.send(1, cd::net::ChannelType::kReliableOrdered, bytes_span(p)).has_value());
    ASSERT_TRUE(ma.send(2, cd::net::ChannelType::kReliableOrdered, bytes_span(p)).has_value());
    ASSERT_TRUE(ma.send(1, cd::net::ChannelType::kReliableOrdered, bytes_span(p)).has_value());

    auto r1 = mb.receive();
    auto r2 = mb.receive();
    auto r3 = mb.receive();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(r1->channel, 1U);
    EXPECT_EQ(r1->sequence, 0U);
    EXPECT_EQ(r2->channel, 2U);
    EXPECT_EQ(r2->sequence, 0U);  // channel 2 starts its own seq at 0
    EXPECT_EQ(r3->channel, 1U);
    EXPECT_EQ(r3->sequence, 1U);  // channel 1 second message → seq=1
}

TEST(ChannelMux, ReceiveWithoutDataReturnsWouldBlock)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux mb { *b };
    auto r = mb.receive();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));
}

TEST(ChannelMux, ReliableDuplicateIsDropped)
{
    // We send three good messages, then manually inject a frame that
    // duplicates sequence #0 — the mux must drop it silently.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };

    const auto p = bytes_of("a");
    const auto q = bytes_of("b");
    ASSERT_TRUE(ma.send(0, cd::net::ChannelType::kReliableOrdered, bytes_span(p)).has_value());

    // Hand-craft a frame with the SAME channel + seq=0 + type=reliable,
    // bypassing ma so we can simulate a duplicate at the wire layer.
    std::vector<std::byte> dup;
    dup.reserve(cd::net::kChannelHeaderSize + q.size());
    dup.push_back(std::byte { 0 });  // channel
    dup.push_back(std::byte { 0 });  // reliable
    for (int i = 0; i < 4; ++i)
        dup.push_back(std::byte { 0 });  // seq=0
    dup.insert(dup.end(), q.begin(), q.end());
    ASSERT_TRUE(a->send(bytes_span(dup)).has_value());

    ASSERT_TRUE(ma.send(0, cd::net::ChannelType::kReliableOrdered, bytes_span(p)).has_value());

    auto r1 = mb.receive();
    auto r2 = mb.receive();
    auto r3 = mb.receive();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->sequence, 0U);
    EXPECT_EQ(str_from_payload(r1->payload), "a");
    // The duplicate frame was silently dropped — the next real message
    // arrives with seq=1 (auto-incremented by ma).
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->sequence, 1U);
    EXPECT_EQ(str_from_payload(r2->payload), "a");
    ASSERT_FALSE(r3.has_value());
    EXPECT_EQ(r3.error().code, static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));
}

// -----------------------------------------------------------------------------
// UDP IConnection — Wave 37
// Uses ephemeral ports (port 0 lets the OS pick a free port). The
// receive path polls briefly because UDP is asynchronous; a short
// `wait_for_recv` helper avoids a thread::sleep_for race.
// -----------------------------------------------------------------------------

namespace
{

[[nodiscard]] cd::core::Result<std::vector<std::byte>>
wait_for_recv(cd::net::IConnection& c, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto r = c.receive();
        if (r.has_value())
            return r;
        if (r.error().code
            != static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock))
            return r;
        std::this_thread::yield();
    }
    return std::unexpected(cd::net::net_errors::make(cd::net::net_errors::Code::kWouldBlock,
                                                     "timeout waiting for datagram"));
}

}  // namespace

TEST(UdpConnection, BuildPairAndRoundTrip)
{
    // Use a fixed high port pair (unlikely to clash on CI runners).
    // Skip cleanly if the bind fails — keeps the test stable when a
    // parallel runner already grabbed the slot.
    constexpr std::uint16_t kPortA = 39541;
    constexpr std::uint16_t kPortB = 39542;
    auto pair = cd::net::make_udp_pair_localhost(kPortA, kPortB);
    if (!pair.has_value())
        GTEST_SKIP() << "Local UDP ports busy — skipping (CI flake-safe)";
    auto& [pa, pb] = *pair;
    ASSERT_NE(pa, nullptr);
    ASSERT_NE(pb, nullptr);
    ASSERT_TRUE(pa->send(bytes_of("hello-udp")).has_value());
    auto recv = wait_for_recv(*pb, std::chrono::milliseconds { 500 });
    ASSERT_TRUE(recv.has_value()) << "no datagram arrived within timeout";
    ASSERT_EQ(recv->size(), 9U);  // "hello-udp"
    EXPECT_EQ(pa->bytes_sent(), 9U);
    EXPECT_EQ(pb->bytes_received(), 9U);
}

TEST(UdpConnection, ReceiveWithoutDataReturnsWouldBlock)
{
    auto a = cd::net::make_udp_connection(0, "127.0.0.1", 39599);
    ASSERT_TRUE(a.has_value());
    auto r = (*a)->receive();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));
}

TEST(UdpConnection, InvalidAddressRejected)
{
    auto r = cd::net::make_udp_connection(0, "not-an-ip", 1234);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::net::net_errors::Code::kInvalidArgument));
}

TEST(UdpConnection, CloseTransitionsToDisconnected)
{
    auto a = cd::net::make_udp_connection(0, "127.0.0.1", 39598);
    ASSERT_TRUE(a.has_value());
    (*a)->close();
    EXPECT_EQ((*a)->state(), cd::net::ConnectionState::kDisconnected);
    auto send = (*a)->send(bytes_of("x"));
    ASSERT_FALSE(send.has_value());
    EXPECT_EQ(send.error().code,
              static_cast<std::uint32_t>(cd::net::net_errors::Code::kDisconnected));
}

// -----------------------------------------------------------------------------
// ReliableChannel — Wave 41
// Uses an injected `now` time point so tests can fast-forward time
// past the RTO without sleeping. Drop simulation is done by directly
// draining the IConnection queue (bypassing the mux) — that's the
// receiver's wire equivalent of "the datagram never arrived".
// -----------------------------------------------------------------------------

TEST(ReliableChannel, NoDropDeliversWithoutRetransmit)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender { ma, /*user_channel=*/0, /*ack_channel=*/1 };
    cd::net::ReliableChannel receiver { mb, /*user_channel=*/0, /*ack_channel=*/1 };

    const auto p = bytes_of("hello");
    auto seq = sender.send({ p.data(), p.size() });
    ASSERT_TRUE(seq.has_value());
    EXPECT_EQ(*seq, 0U);
    EXPECT_EQ(sender.pending_send_count(), 1U);

    // Receiver drains the user frame and emits an ACK.
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);
    auto got = receiver.receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), 5U);

    // Sender drains the ACK and discharges the pending entry.
    sender.tick(t0);
    EXPECT_EQ(sender.pending_send_count(), 0U);
    EXPECT_EQ(sender.retransmit_count(), 0U);
}

TEST(ReliableChannel, DroppedUserFrameTriggersRetransmit)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1, std::chrono::milliseconds { 50 }, /*max_retries=*/3
    };
    cd::net::ReliableChannel receiver {
        mb, 0, 1, std::chrono::milliseconds { 50 }
    };

    const auto p = bytes_of("retrans");
    auto seq = sender.send({ p.data(), p.size() });
    ASSERT_TRUE(seq.has_value());

    // Simulate frame drop: drain b's loopback queue directly so the
    // receiver's mux never sees the user frame.
    auto dropped = b->receive();
    ASSERT_TRUE(dropped.has_value());

    // Time before RTO → no retransmit.
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    sender.tick(t0);
    EXPECT_EQ(sender.retransmit_count(), 0U);

    // Past RTO → exactly one retransmit fires.
    sender.tick(t0 + std::chrono::milliseconds { 60 });
    EXPECT_EQ(sender.retransmit_count(), 1U);
    EXPECT_EQ(sender.pending_send_count(), 1U);  // still unacked

    // Receiver now actually processes the retransmitted frame.
    receiver.tick(t0 + std::chrono::milliseconds { 65 });
    auto got = receiver.receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->size(), 7U);

    // Sender drains the ACK and discharges.
    sender.tick(t0 + std::chrono::milliseconds { 70 });
    EXPECT_EQ(sender.pending_send_count(), 0U);
}

TEST(ReliableChannel, DuplicateRetransmitDoesNotDeliverTwice)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1, std::chrono::milliseconds { 50 }
    };
    cd::net::ReliableChannel receiver {
        mb, 0, 1, std::chrono::milliseconds { 50 }
    };

    // Drop the ACK so the sender thinks the frame was lost and
    // retransmits. The receiver must dedup on the second arrival.
    const auto p = bytes_of("once");
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());

    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);  // receives + builds ACK frame on wire

    // Drop the ACK before the sender can see it.
    auto ack_drop = a->receive();
    ASSERT_TRUE(ack_drop.has_value());

    auto first = receiver.receive();
    ASSERT_TRUE(first.has_value());

    // Past RTO, sender retransmits the same frame.
    sender.tick(t0 + std::chrono::milliseconds { 60 });
    EXPECT_EQ(sender.retransmit_count(), 1U);

    // Receiver processes the duplicate but does NOT deliver again.
    receiver.tick(t0 + std::chrono::milliseconds { 65 });
    EXPECT_EQ(receiver.duplicate_drop_count(), 1U);
    auto second = receiver.receive();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code,
              static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));
}

TEST(ReliableChannel, SrttRtvarPopulatedFromFirstAck)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1,
        std::chrono::milliseconds { 200 },
        /*max_retries=*/5,
        /*rto_min=*/std::chrono::milliseconds { 25 },
        /*rto_max=*/std::chrono::milliseconds { 5000 }
    };
    cd::net::ReliableChannel receiver { mb, 0, 1 };

    EXPECT_EQ(sender.srtt().count(), 0);
    EXPECT_EQ(sender.current_rto(), std::chrono::milliseconds { 200 });

    const auto p = bytes_of("rtt");
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);  // builds + emits ACK
    sender.tick(t0);    // consumes ACK → first RTT sample

    // After the first sample SRTT must be set (non-zero) and RTO must
    // be adjusted to SRTT + 4*RTTVAR, clamped to [rto_min, rto_max].
    EXPECT_GT(sender.srtt().count(), 0);
    EXPECT_GE(sender.current_rto(), std::chrono::milliseconds { 25 });
    EXPECT_LE(sender.current_rto(), std::chrono::milliseconds { 5000 });
}

TEST(ReliableChannel, KarnSkipsRttSampleOnRetransmit)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1, std::chrono::milliseconds { 30 }, 3
    };
    cd::net::ReliableChannel receiver { mb, 0, 1 };

    const auto p = bytes_of("karn");
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());

    // Drop the original on the wire so retransmit fires.
    auto dropped = b->receive();
    ASSERT_TRUE(dropped.has_value());

    const auto t0 = cd::net::ReliableChannel::Clock::now();
    sender.tick(t0 + std::chrono::milliseconds { 50 });  // retransmits
    EXPECT_EQ(sender.retransmit_count(), 1U);

    // Now the receiver processes the retransmit and ACKs.
    receiver.tick(t0 + std::chrono::milliseconds { 55 });
    sender.tick(t0 + std::chrono::milliseconds { 60 });

    // SRTT must remain 0 because the only ACK arrived for a
    // retransmitted frame (Karn's algorithm skips that sample).
    EXPECT_EQ(sender.srtt().count(), 0);
}

TEST(ReliableChannel, CumulativeAckDischargesAllPriorPendings)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender { ma, 0, 1 };
    cd::net::ReliableChannel receiver { mb, 0, 1 };

    // Send four frames back to back.
    const auto p = bytes_of("c");
    for (int i = 0; i < 4; ++i)
        ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
    EXPECT_EQ(sender.pending_send_count(), 4U);

    // Receiver processes them all in one tick → emits ONE cum-ack for
    // seq=3 (high water = 4 - 1). Sender's tick discharges all four.
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);
    sender.tick(t0);
    EXPECT_EQ(sender.pending_send_count(), 0U);
}

TEST(ReliableChannel, AimdAdditiveIncreaseOnAck)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender { ma, 0, 1 };
    cd::net::ReliableChannel receiver { mb, 0, 1 };
    sender.enable_aimd(/*cwnd_initial=*/4, /*cwnd_min=*/1, /*cwnd_max=*/16);
    EXPECT_EQ(sender.cwnd(), 4U);

    const auto p = bytes_of("a");
    // Send 3 frames within the cwnd=4 budget.
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());

    // Receiver drains + ACKs. Sender's tick processes ACKs → cwnd bumps
    // by 1 per successful non-retransmit discharge.
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);
    sender.tick(t0);
    EXPECT_EQ(sender.pending_send_count(), 0U);
    EXPECT_EQ(sender.cwnd(), 7U);  // 4 + 3 successful ACKs
}

TEST(ReliableChannel, AimdMultiplicativeDecreaseOnRtoLoss)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1, std::chrono::milliseconds { 25 }, 5
    };
    sender.enable_aimd(/*cwnd_initial=*/8, /*cwnd_min=*/1, /*cwnd_max=*/256);
    EXPECT_EQ(sender.cwnd(), 8U);

    const auto p = bytes_of("loss");
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());

    // Drop the frame so the sender will RTO-retransmit.
    auto dropped = b->receive();
    ASSERT_TRUE(dropped.has_value());

    const auto t0 = cd::net::ReliableChannel::Clock::now();
    sender.tick(t0 + std::chrono::milliseconds { 30 });  // past RTO → retransmit
    EXPECT_EQ(sender.retransmit_count(), 1U);
    EXPECT_EQ(sender.cwnd(), 4U);  // halved by AIMD MD
}

TEST(ReliableChannel, AimdCapHonoursMinAndMax)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1, std::chrono::milliseconds { 10 }, 10
    };
    sender.enable_aimd(/*cwnd_initial=*/2, /*cwnd_min=*/1, /*cwnd_max=*/2);
    EXPECT_EQ(sender.cwnd(), 2U);  // initial clamped to max=2

    // The max cap holds: after many successful ACKs cwnd stays at 2.
    cd::net::ReliableChannel receiver { mb, 0, 1 };
    const auto p = bytes_of("c");
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
        receiver.tick(t0);
        sender.tick(t0);
    }
    EXPECT_LE(sender.cwnd(), 2U);
    EXPECT_GE(sender.cwnd(), 1U);  // min cap
}

TEST(ReliableChannel, SendWindowBackPressuresWhenFull)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    // Window of 3 — fourth send must return kWouldBlock until the
    // receiver drains the first frame and the sender consumes the ACK.
    cd::net::ReliableChannel sender {
        ma, 0, 1,
        std::chrono::milliseconds { 500 }, 5,
        std::chrono::milliseconds { 25 },
        std::chrono::milliseconds { 5000 },
        /*send_window_size=*/3
    };
    cd::net::ReliableChannel receiver { mb, 0, 1 };

    const auto p = bytes_of("w");
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
    EXPECT_EQ(sender.pending_send_count(), 3U);

    // Fourth call must back-pressure.
    auto blocked = sender.send({ p.data(), p.size() });
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(blocked.error().code,
              static_cast<std::uint32_t>(cd::net::net_errors::Code::kWouldBlock));

    // Drain via receiver tick + sender tick → window empties → next send OK.
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);
    sender.tick(t0);
    EXPECT_EQ(sender.pending_send_count(), 0U);
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());
}

TEST(ReliableChannel, SackTriggersFastRetransmitForGap)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1, std::chrono::milliseconds { 500 }, 5
    };
    cd::net::ReliableChannel receiver {
        mb, 0, 1, std::chrono::milliseconds { 500 }
    };

    // Send three frames. Drop the FIRST on the wire (b->receive consumes
    // it before mb.tick) so the receiver gets seq=1 and seq=2 but not
    // seq=0. The receiver's SACK then points at the gap directly.
    const auto p = bytes_of("z");
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());  // seq=0
    // Drain seq=0 from the wire to simulate loss.
    auto dropped = b->receive();
    ASSERT_TRUE(dropped.has_value());
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());  // seq=1
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());  // seq=2

    // Receiver ticks: gets seq=1 + seq=2 → cum_high_water stuck at -1
    // (uint underflow ⇒ no ACK yet because next_recv_seq_ is 0), but
    // inbound_ has {1, 2} → SACK range [1,2].
    const auto t0 = cd::net::ReliableChannel::Clock::now();
    receiver.tick(t0);

    // Sender ticks: sees cum=last_known (still 0 from constructor? actually
    // no — no ACK yet, but the receiver should have emitted one for
    // SACK). Without SACK the sender would wait for RTO (500 ms). With
    // SACK it fast-retransmits seq=0 immediately.
    sender.tick(t0);
    EXPECT_GE(sender.fast_retransmit_count(), 1U);

    // Now the receiver gets the fast-retransmitted seq=0 and can flush
    // the buffered seq=1 and seq=2.
    receiver.tick(t0);
    EXPECT_EQ(receiver.ready_count(), 3U);
    // Sender's final cum-ACK discharges everything.
    sender.tick(t0);
    EXPECT_EQ(sender.pending_send_count(), 0U);
}

TEST(ReliableChannel, MaxRetriesCapsRetransmitCount)
{
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux ma { *a };
    cd::net::ChannelMux mb { *b };
    cd::net::ReliableChannel sender {
        ma, 0, 1, std::chrono::milliseconds { 10 }, /*max_retries=*/2
    };

    const auto p = bytes_of("lost");
    ASSERT_TRUE(sender.send({ p.data(), p.size() }).has_value());

    // Drop every wire frame the sender produces, then drive ticks past
    // multiple RTO windows. The retransmit count must NOT exceed
    // max_retries even when no ACK ever arrives.
    auto drain_a = [&] {
        while (true)
        {
            auto r = b->receive();
            if (!r.has_value())
                return;
        }
    };

    const auto t0 = cd::net::ReliableChannel::Clock::now();
    drain_a();
    for (int i = 0; i < 10; ++i)
    {
        sender.tick(t0 + std::chrono::milliseconds { 20 + i * 20 });
        drain_a();
    }
    EXPECT_EQ(sender.retransmit_count(), 2U);  // max_retries cap
    EXPECT_EQ(sender.pending_send_count(), 1U);
}

TEST(ChannelMux, ReliableOutOfOrderBuffersUntilGapFills)
{
    // Manually inject frames in the order [seq=1, seq=0] on the same
    // channel — the mux must buffer seq=1 and deliver seq=0 first, then
    // flush the buffered seq=1.
    auto [a, b] = cd::net::make_loopback_pair();
    cd::net::ChannelMux mb { *b };

    auto inject = [&](std::uint8_t ch, std::uint32_t seq, std::string_view payload) {
        std::vector<std::byte> frame;
        frame.reserve(cd::net::kChannelHeaderSize + payload.size());
        frame.push_back(std::byte { ch });
        frame.push_back(std::byte { 0 });  // reliable
        for (int i = 0; i < 4; ++i)
            frame.push_back(std::byte { static_cast<std::uint8_t>((seq >> (i * 8)) & 0xFFu) });
        frame.insert(frame.end(),
                     reinterpret_cast<const std::byte*>(payload.data()),
                     reinterpret_cast<const std::byte*>(payload.data()) + payload.size());
        ASSERT_TRUE(a->send(bytes_span(frame)).has_value());
    };

    inject(5, /*seq=*/1, "second");
    inject(5, /*seq=*/0, "first");

    auto r1 = mb.receive();
    auto r2 = mb.receive();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->sequence, 0U);
    EXPECT_EQ(str_from_payload(r1->payload), "first");
    EXPECT_EQ(r2->sequence, 1U);
    EXPECT_EQ(str_from_payload(r2->payload), "second");
}

}  // namespace

// ---------------------------------------------------------------------------
// Phase 18.E — PredictionBuffer tests (Wave 178)
// ---------------------------------------------------------------------------
#include <cd/net/PredictionBuffer.hpp>

TEST(PredictionBuffer, RecordAndLookup)
{
    cd::net::PredictionBuffer<int> pb { 16 };
    pb.record(0, 100);
    pb.record(1, 110);
    pb.record(2, 120);
    EXPECT_EQ(pb.size(), 3u);
    EXPECT_EQ(*pb.at(0), 100);
    EXPECT_EQ(*pb.at(2), 120);
    EXPECT_FALSE(pb.at(3).has_value());
}

TEST(PredictionBuffer, EvictsOldestWhenFull)
{
    cd::net::PredictionBuffer<int> pb { 4 };
    for (std::uint32_t i = 0; i < 6; ++i)
        pb.record(i, static_cast<int>(i * 10));
    EXPECT_EQ(pb.size(), 4u);
    EXPECT_FALSE(pb.at(0).has_value());
    EXPECT_FALSE(pb.at(1).has_value());
    EXPECT_EQ(*pb.at(2), 20);
    EXPECT_EQ(*pb.at(5), 50);
}

TEST(PredictionBuffer, CorrectAndReplayRebuildsState)
{
    cd::net::PredictionBuffer<int> pb { 16 };
    for (std::uint32_t i = 0; i < 5; ++i) pb.record(i, static_cast<int>(i));
    const auto replayed = pb.correct_and_replay(2, 200,
        [](int prev, std::uint32_t) { return prev + 1; });
    EXPECT_EQ(replayed, 2u);
    EXPECT_EQ(*pb.at(2), 200);
    EXPECT_EQ(*pb.at(3), 201);
    EXPECT_EQ(*pb.at(4), 202);
}

#include <cd/net/PacketHeader.hpp>

TEST(PacketHeader, DefaultIsValidWhenOpcodeSet)
{
    cd::net::PacketHeader h;
    EXPECT_FALSE(cd::net::is_valid(h));  // kInvalid opcode
    h.opcode = cd::net::Opcode::kHandshake;
    EXPECT_TRUE(cd::net::is_valid(h));
}

TEST(PacketHeader, MagicMismatchRejected)
{
    cd::net::PacketHeader h;
    h.opcode = cd::net::Opcode::kHeartbeat;
    h.magic = 0xDEADBEEFu;
    EXPECT_FALSE(cd::net::is_valid(h));
}

TEST(PacketHeader, VersionMismatchRejected)
{
    cd::net::PacketHeader h;
    h.opcode = cd::net::Opcode::kReliable;
    h.version = 99;
    EXPECT_FALSE(cd::net::is_valid(h));
}

TEST(PacketHeader, SizeIsEightBytes)
{
    EXPECT_EQ(sizeof(cd::net::PacketHeader), 8u);
}

#include <cd/net/SequenceWindow.hpp>

TEST(SequenceWindow, FirstAcceptIsTrue)
{
    cd::net::SequenceWindow<> w;
    EXPECT_TRUE(w.accept(100));
}

TEST(SequenceWindow, AscendingSequenceAlwaysAccepted)
{
    cd::net::SequenceWindow<> w;
    for (std::uint32_t i = 0; i < 100; ++i)
        EXPECT_TRUE(w.accept(i));
}

TEST(SequenceWindow, DuplicateRejected)
{
    cd::net::SequenceWindow<> w;
    EXPECT_TRUE(w.accept(10));
    EXPECT_FALSE(w.accept(10));
}

TEST(SequenceWindow, OutOfOrderInsideWindowAccepted)
{
    cd::net::SequenceWindow<> w;
    EXPECT_TRUE(w.accept(5));
    EXPECT_TRUE(w.accept(8));
    EXPECT_TRUE(w.accept(6));   // older but in window
    EXPECT_FALSE(w.accept(6));  // now duplicate
}

TEST(SequenceWindow, OutOfWindowRejected)
{
    cd::net::SequenceWindow<8> w;
    EXPECT_TRUE(w.accept(100));
    EXPECT_FALSE(w.accept(80));  // too old
}

#include <cd/net/DeltaWriter.hpp>
#include <cstring>

TEST(DeltaWriter, EmptyChangesProducesEmptyMarker)
{
    std::array<std::byte, 16> base {};
    std::array<std::byte, 16> cur {};
    auto d = cd::net::write_delta(base, cur);
    ASSERT_EQ(d.size(), 2u);   // just the count = 0 header
    EXPECT_EQ(static_cast<std::uint8_t>(d[0]), 0u);
    EXPECT_EQ(static_cast<std::uint8_t>(d[1]), 0u);
}

TEST(DeltaWriter, RoundTripThroughApply)
{
    std::array<std::byte, 32> base {};
    std::array<std::byte, 32> cur  {};
    cur[5]  = std::byte { 0xAB };
    cur[10] = std::byte { 0xCD };
    cur[20] = std::byte { 0xEF };
    auto d = cd::net::write_delta(base, cur);
    EXPECT_EQ(d.size(), 2u + 3u * 3u);

    std::array<std::byte, 32> restored {};
    ASSERT_TRUE(cd::net::apply_delta(restored, d));
    EXPECT_EQ(std::memcmp(restored.data(), cur.data(), 32), 0);
}

TEST(DeltaWriter, MismatchedSizesProduceEmptyDelta)
{
    std::array<std::byte, 8>  a {};
    std::array<std::byte, 16> b {};
    auto d = cd::net::write_delta(a, b);
    EXPECT_TRUE(d.empty());
}

TEST(DeltaWriter, TruncatedDeltaRejected)
{
    std::array<std::byte, 8> tgt {};
    std::array<std::byte, 4> bad { std::byte{1}, std::byte{0},
                                   std::byte{0}, std::byte{0} };
    EXPECT_FALSE(cd::net::apply_delta(tgt, bad));
}

#include <cd/net/SnapshotBuffer.hpp>

namespace {
struct PosState
{
    float x { 0.0F };

    PosState operator+(const PosState& b) const noexcept { return { x + b.x }; }
    PosState operator*(float s) const noexcept { return { x * s }; }
};
}  // namespace

TEST(SnapshotBuffer, SampleAtKeyTimeMatchesState)
{
    cd::net::SnapshotBuffer<PosState> b;
    b.push(0.0, PosState { 10.0F });
    b.push(1.0, PosState { 20.0F });
    auto r = b.sample(1.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_FLOAT_EQ(r->x, 20.0F);
}

TEST(SnapshotBuffer, SampleInterpolatesLinearly)
{
    cd::net::SnapshotBuffer<PosState> b;
    b.push(0.0, PosState { 0.0F });
    b.push(2.0, PosState { 10.0F });
    auto r = b.sample(1.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_NEAR(r->x, 5.0F, 1e-4F);
}

TEST(SnapshotBuffer, SampleBeforeFirstClamps)
{
    cd::net::SnapshotBuffer<PosState> b;
    b.push(5.0, PosState { 42.0F });
    auto r = b.sample(0.0);
    ASSERT_TRUE(r.has_value());
    EXPECT_FLOAT_EQ(r->x, 42.0F);
}

TEST(SnapshotBuffer, DropOlderThanReducesSize)
{
    cd::net::SnapshotBuffer<PosState> b;
    b.push(0.0, PosState {});
    b.push(1.0, PosState {});
    b.push(2.0, PosState {});
    b.drop_older_than(1.5);
    EXPECT_EQ(b.size(), 1u);   // only 2.0 remains (0.0 and 1.0 are < 1.5)
}

#include <cd/net/RleCodec.hpp>

TEST(RleCodec, EncodesRunOfZeros)
{
    std::vector<std::uint8_t> src(10, 0u);
    auto r = cd::net::rle_encode(src);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 10u);
    EXPECT_EQ(r[1], 0u);
}

TEST(RleCodec, RoundTripMixedData)
{
    std::vector<std::uint8_t> src;
    src.reserve(5);
for (int i = 0; i < 5; ++i) src.push_back(0xAA);
    for (int i = 0; i < 8; ++i) src.push_back(0x55);
    src.push_back(0x77);
    for (int i = 0; i < 3; ++i) src.push_back(0xFF);
    auto enc = cd::net::rle_encode(src);
    auto dec = cd::net::rle_decode(enc);
    EXPECT_EQ(dec, src);
}

TEST(RleCodec, EmptyInputProducesEmptyOutput)
{
    std::vector<std::uint8_t> src;
    EXPECT_TRUE(cd::net::rle_encode(src).empty());
    EXPECT_TRUE(cd::net::rle_decode(src).empty());
}

TEST(RleCodec, LongRunSplitsAt255)
{
    std::vector<std::uint8_t> src(300, 0xCCu);
    auto r = cd::net::rle_encode(src);
    // Two records: (255, 0xCC) + (45, 0xCC) = 4 bytes
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0], 255u);
    EXPECT_EQ(r[2], 45u);
}

#include <cd/net/SequenceId.hpp>

TEST(SequenceId, NewerLessThanOlderViaWrap)
{
    using cd::net::seq_greater_than;
    // u16 wrap: 65535 → 0 → 1 → ...
    EXPECT_TRUE(seq_greater_than<std::uint16_t>(0, 65535));   // 0 is "newer" than 65535
    EXPECT_FALSE(seq_greater_than<std::uint16_t>(65535, 0));
}

TEST(SequenceId, BasicMonotonic)
{
    using cd::net::seq_greater_than;
    EXPECT_TRUE(seq_greater_than<std::uint16_t>(100, 50));
    EXPECT_FALSE(seq_greater_than<std::uint16_t>(50, 100));
    EXPECT_FALSE(seq_greater_than<std::uint16_t>(100, 100));   // equal not greater
}

TEST(SequenceId, DistanceWrapsModulo)
{
    using cd::net::seq_distance;
    // Distance from 5 to 65535 (going backwards) should wrap as 6.
    EXPECT_EQ(seq_distance<std::uint16_t>(5, 65535), 6);
}

#include <cd/net/LatencyStats.hpp>

TEST(LatencyStats, FirstSampleBecomesRtt)
{
    cd::net::LatencyStats s;
    s.record(50000);   // 50 ms
    EXPECT_FLOAT_EQ(s.current_rtt_us(), 50000.0F);
    EXPECT_FLOAT_EQ(s.jitter_us(), 0.0F);
    EXPECT_EQ(s.sample_count(), 1u);
}

TEST(LatencyStats, EwmaSmoothsAcrossSamples)
{
    cd::net::LatencyStats s;
    for (int i = 0; i < 100; ++i) s.record(50000);
    EXPECT_NEAR(s.current_rtt_us(), 50000.0F, 1.0F);
    s.record(100000);   // spike — should not fully dominate
    EXPECT_LT(s.current_rtt_us(), 100000.0F);
    EXPECT_GT(s.current_rtt_us(), 50000.0F);
}

TEST(LatencyStats, JitterTracksVariation)
{
    cd::net::LatencyStats s;
    s.record(50000);
    s.record(70000);
    s.record(50000);
    EXPECT_GT(s.jitter_us(), 0.0F);
}

TEST(LatencyStats, MinMaxTracked)
{
    cd::net::LatencyStats s;
    s.record(50000);
    s.record(10000);
    s.record(200000);
    EXPECT_EQ(s.min_us(), 10000u);
    EXPECT_EQ(s.max_us(), 200000u);
}

#include <cd/net/QoSTier.hpp>

TEST(QoSTier, CriticalIsHighestPriority)
{
    EXPECT_GT(cd::net::tier_priority(cd::net::QoSTier::kCritical),
              cd::net::tier_priority(cd::net::QoSTier::kHigh));
    EXPECT_GT(cd::net::tier_priority(cd::net::QoSTier::kHigh),
              cd::net::tier_priority(cd::net::QoSTier::kNormal));
}

TEST(QoSTier, LowMayDrop)
{
    EXPECT_TRUE(cd::net::may_drop_on_congestion(cd::net::QoSTier::kLow));
    EXPECT_TRUE(cd::net::may_drop_on_congestion(cd::net::QoSTier::kNormal));
    EXPECT_FALSE(cd::net::may_drop_on_congestion(cd::net::QoSTier::kHigh));
    EXPECT_FALSE(cd::net::may_drop_on_congestion(cd::net::QoSTier::kCritical));
}

TEST(QoSTier, HighRequiresReliable)
{
    EXPECT_TRUE(cd::net::requires_reliable_delivery(cd::net::QoSTier::kCritical));
    EXPECT_TRUE(cd::net::requires_reliable_delivery(cd::net::QoSTier::kHigh));
    EXPECT_FALSE(cd::net::requires_reliable_delivery(cd::net::QoSTier::kNormal));
}

#include <cd/net/Throttle.hpp>

TEST(Throttle, StartsFull)
{
    cd::net::Throttle t { 10.0F, 5.0F };
    EXPECT_FLOAT_EQ(t.tokens(), 10.0F);
}

TEST(Throttle, ConsumeDecreasesTokens)
{
    cd::net::Throttle t { 10.0F, 5.0F };
    EXPECT_TRUE(t.try_consume(3.0F));
    EXPECT_FLOAT_EQ(t.tokens(), 7.0F);
}

TEST(Throttle, ConsumeOverBudgetFails)
{
    cd::net::Throttle t { 5.0F, 1.0F };
    EXPECT_FALSE(t.try_consume(10.0F));
    EXPECT_FLOAT_EQ(t.tokens(), 5.0F);
}

TEST(Throttle, UpdateRegeneratesTokens)
{
    cd::net::Throttle t { 10.0F, 5.0F };
    (void)t.try_consume(8.0F);
    t.update(1.0F);
    EXPECT_FLOAT_EQ(t.tokens(), 7.0F);   // 2 + 5*1 = 7
}

TEST(Throttle, UpdateClampsToCapacity)
{
    cd::net::Throttle t { 10.0F, 100.0F };
    (void)t.try_consume(2.0F);
    t.update(10.0F);
    EXPECT_FLOAT_EQ(t.tokens(), 10.0F);
}
