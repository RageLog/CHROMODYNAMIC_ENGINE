// =============================================================================
// CHROMODYNAMIC — cd::net tests
// =============================================================================
#include <cd/net/ChannelMux.hpp>
#include <cd/net/IConnection.hpp>
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
