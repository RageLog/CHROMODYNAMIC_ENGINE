// =============================================================================
// CHROMODYNAMIC — cd::network::lobby::SocketTransport tests
// Phase 754 / FINALE-4 W1-E1 — Sprint-3 real socket transport
//
// Tests (5):
//   1. PairConstructibleOnLocalhost — two endpoints bind, both connected.
//   2. SendRecvRoundTrip            — encode + ship + decode survives wire.
//   3. BidirectionalRoundTrip       — A<->B both directions.
//   4. RecvEmptyReturnsNullopt      — no datagram pending → std::nullopt.
//   5. CloseStopsTransport          — after close(), is_connected() == false
//                                     and send()/recv() become silent no-ops.
//
// All tests GTEST_SKIP() when port binding fails — same flake-safe pattern
// used by cd::net Vulkan/UDP integration tests and the spec'd "Vulkan-style
// GTEST_SKIP port-binding" guard for CI without elevated socket permissions.
//
// MOMENT: 4 players on a LAN connect to a lobby host, see each other's
//         PlayerState in real-time. Real multiplayer over real sockets.
// =============================================================================
#include <cd/network/lobby/Packet.hpp>
#include <cd/network/lobby/SocketTransport.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using cd::network::lobby::Packet;
using cd::network::lobby::PacketKind;
using cd::network::lobby::SocketTransport;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] Packet make_packet(PacketKind kind,
                                  std::uint64_t room_id   = 42U,
                                  std::uint64_t player_id = 7U,
                                  std::vector<std::uint8_t> payload = {})
{
    Packet p;
    p.kind      = kind;
    p.room_id   = room_id;
    p.player_id = player_id;
    p.payload   = std::move(payload);
    return p;
}

/// Poll recv() up to `max_attempts` times, sleeping briefly between attempts,
/// to absorb the kernel's UDP delivery latency on localhost. Returns the
/// first decoded packet, or std::nullopt if none arrived inside the budget.
[[nodiscard]] std::optional<Packet>
poll_recv(SocketTransport& t, int max_attempts = 200)
{
    for (int i = 0; i < max_attempts; ++i)
    {
        auto pkt = t.recv();
        if (pkt.has_value())
            return pkt;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::nullopt;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. PairConstructibleOnLocalhost
// ---------------------------------------------------------------------------
TEST(SocketTransport, PairConstructibleOnLocalhost)
{
    auto pair = SocketTransport::configure_localhost_pair(0U, 0U);
    if (!pair.has_value())
    {
        GTEST_SKIP() << "configure_localhost_pair failed — port bind denied "
                        "by environment (CI sandbox / firewall).";
    }
    EXPECT_TRUE(pair->first.is_connected());
    EXPECT_TRUE(pair->second.is_connected());
}

// ---------------------------------------------------------------------------
// 2. SendRecvRoundTrip — packet survives the wire intact
// ---------------------------------------------------------------------------
TEST(SocketTransport, SendRecvRoundTrip)
{
    auto pair = SocketTransport::configure_localhost_pair(0U, 0U);
    if (!pair.has_value())
        GTEST_SKIP() << "localhost UDP bind failed.";

    auto& host   = pair->first;
    auto& client = pair->second;

    const Packet outgoing = make_packet(
        PacketKind::kJoinRoom,
        /*room_id=*/   100U,
        /*player_id=*/  42U,
        /*payload=*/   { 0xCAU, 0xFEU, 0xBAU, 0xBEU });

    host.send(outgoing);

    const auto received = poll_recv(client);
    ASSERT_TRUE(received.has_value())
        << "Packet did not arrive within poll budget.";
    EXPECT_EQ(received->kind,      outgoing.kind);
    EXPECT_EQ(received->room_id,   outgoing.room_id);
    EXPECT_EQ(received->player_id, outgoing.player_id);
    EXPECT_EQ(received->payload,   outgoing.payload);
}

// ---------------------------------------------------------------------------
// 3. BidirectionalRoundTrip — A<->B both directions
// ---------------------------------------------------------------------------
TEST(SocketTransport, BidirectionalRoundTrip)
{
    auto pair = SocketTransport::configure_localhost_pair(0U, 0U);
    if (!pair.has_value())
        GTEST_SKIP() << "localhost UDP bind failed.";

    auto& a = pair->first;
    auto& b = pair->second;

    const Packet a_to_b = make_packet(PacketKind::kCreateRoom, 1U, 10U);
    const Packet b_to_a = make_packet(PacketKind::kSyncRoomState, 1U, 0U,
                                       { 0x01U, 0x02U, 0x03U });

    a.send(a_to_b);
    b.send(b_to_a);

    const auto on_b = poll_recv(b);
    const auto on_a = poll_recv(a);

    ASSERT_TRUE(on_b.has_value());
    ASSERT_TRUE(on_a.has_value());
    EXPECT_EQ(on_b->kind,      PacketKind::kCreateRoom);
    EXPECT_EQ(on_b->player_id, 10U);
    EXPECT_EQ(on_a->kind,      PacketKind::kSyncRoomState);
    ASSERT_EQ(on_a->payload.size(), 3U);
    EXPECT_EQ(on_a->payload[0], 0x01U);
}

// ---------------------------------------------------------------------------
// 4. RecvEmptyReturnsNullopt — nothing sent → recv yields nullopt
// ---------------------------------------------------------------------------
TEST(SocketTransport, RecvEmptyReturnsNullopt)
{
    auto pair = SocketTransport::configure_localhost_pair(0U, 0U);
    if (!pair.has_value())
        GTEST_SKIP() << "localhost UDP bind failed.";

    auto& client = pair->second;
    // Quick single-attempt poll — we expect nullopt, no need to wait.
    EXPECT_FALSE(client.recv().has_value());
}

// ---------------------------------------------------------------------------
// 5. CloseStopsTransport — after close, send/recv are silent no-ops
// ---------------------------------------------------------------------------
TEST(SocketTransport, CloseStopsTransport)
{
    auto pair = SocketTransport::configure_localhost_pair(0U, 0U);
    if (!pair.has_value())
        GTEST_SKIP() << "localhost UDP bind failed.";

    auto& host = pair->first;
    EXPECT_TRUE(host.is_connected());

    host.close();
    EXPECT_FALSE(host.is_connected());

    // Post-close calls must be silent no-ops, never throw.
    host.send(make_packet(PacketKind::kLeaveRoom));
    EXPECT_FALSE(host.recv().has_value());
}
