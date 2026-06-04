// =============================================================================
// CHROMODYNAMIC — cd::network::lobby packet codec + MockTransport tests
// Phase 723 / Sprint W5B
//
// Tests: 7
//   1. EncodeDecodeRoundTrip     — encode then decode yields identical Packet.
//   2. AllKindsRoundTrip         — every PacketKind value survives a round-trip.
//   3. MalformedTooShort         — spans shorter than kMinPacketSize rejected.
//   4. MalformedOverrun          — payload_len claims more bytes than available.
//   5. MalformedUnknownKind      — kind byte >= kCount_ is rejected.
//   6. MockTransportSendRecv     — send() accumulates; inject()+recv() drains FIFO.
//   7. SyncRoomStatePayload      — kSyncRoomState carries a player-array payload
//                                  round-trip intact.
//
// MOMENT: A multiplayer dev tests lobby flow via MockTransport — no real
//         network required, deterministic. Real socket transport plugs in later.
// =============================================================================
#include <cd/network/lobby/Packet.hpp>
#include <cd/network/lobby/Transport.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <vector>

namespace
{

using cd::network::lobby::Packet;
using cd::network::lobby::PacketKind;
using cd::network::lobby::MockTransport;
using cd::network::lobby::kMinPacketSize;
using cd::network::lobby::encode_packet;
using cd::network::lobby::decode_packet;

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

void expect_packets_equal(const Packet& a, const Packet& b)
{
    EXPECT_EQ(a.kind,      b.kind);
    EXPECT_EQ(a.room_id,   b.room_id);
    EXPECT_EQ(a.player_id, b.player_id);
    EXPECT_EQ(a.payload,   b.payload);
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. EncodeDecodeRoundTrip — full round-trip, non-empty payload
// ---------------------------------------------------------------------------
TEST(PacketCodec, EncodeDecodeRoundTrip)
{
    const Packet original = make_packet(
        PacketKind::kJoinRoom,
        /*room_id=*/  100U,
        /*player_id=*/200U,
        /*payload=*/  { 0xDE, 0xAD, 0xBE, 0xEF }
    );

    const auto encoded = encode_packet(original);
    ASSERT_GE(encoded.size(), kMinPacketSize);
    ASSERT_EQ(encoded.size(), kMinPacketSize + 4U);  // 4-byte payload

    const auto decoded = decode_packet(encoded);
    ASSERT_TRUE(decoded.has_value());
    expect_packets_equal(original, *decoded);
}

// ---------------------------------------------------------------------------
// 2. AllKindsRoundTrip — every valid PacketKind survives round-trip
// ---------------------------------------------------------------------------
TEST(PacketCodec, AllKindsRoundTrip)
{
    constexpr std::underlying_type_t<PacketKind> kCount =
        static_cast<std::underlying_type_t<PacketKind>>(PacketKind::kCount_);

    for (std::underlying_type_t<PacketKind> i = 0; i < kCount; ++i)
    {
        const auto kind = static_cast<PacketKind>(i);
        const Packet original = make_packet(kind, static_cast<std::uint64_t>(i + 1U),
                                             static_cast<std::uint64_t>(i + 100U));

        const auto encoded = encode_packet(original);
        const auto decoded = decode_packet(encoded);
        ASSERT_TRUE(decoded.has_value()) << "Kind index " << static_cast<int>(i);
        EXPECT_EQ(decoded->kind, kind);
    }
}

// ---------------------------------------------------------------------------
// 3. MalformedTooShort — spans shorter than kMinPacketSize are rejected
// ---------------------------------------------------------------------------
TEST(PacketCodec, MalformedTooShort)
{
    // Empty span.
    EXPECT_FALSE(decode_packet({}).has_value());

    // One byte short of minimum.
    std::vector<std::uint8_t> short_buf(kMinPacketSize - 1U, 0x00U);
    EXPECT_FALSE(decode_packet(short_buf).has_value());

    // Exactly kMinPacketSize with zero-length payload → should succeed.
    std::vector<std::uint8_t> exact_min(kMinPacketSize, 0x00U);
    // kind byte = 0 (kCreateRoom), payload_len field at [17..20] = 0 → valid.
    const auto result = decode_packet(exact_min);
    EXPECT_TRUE(result.has_value());
}

// ---------------------------------------------------------------------------
// 4. MalformedOverrun — payload_len claims more bytes than available
// ---------------------------------------------------------------------------
TEST(PacketCodec, MalformedOverrun)
{
    // Encode a packet with a 4-byte payload then truncate the last 2 bytes.
    const Packet pkt = make_packet(PacketKind::kCreateRoom, 1U, 1U,
                                    { 0x01U, 0x02U, 0x03U, 0x04U });
    auto encoded = encode_packet(pkt);
    ASSERT_GT(encoded.size(), 2U);
    encoded.resize(encoded.size() - 2U);  // remove tail bytes

    EXPECT_FALSE(decode_packet(encoded).has_value());
}

// ---------------------------------------------------------------------------
// 5. MalformedUnknownKind — kind byte out of valid range is rejected
// ---------------------------------------------------------------------------
TEST(PacketCodec, MalformedUnknownKind)
{
    // Build a well-formed buffer then corrupt the kind byte.
    const Packet pkt = make_packet(PacketKind::kLeaveRoom, 5U, 5U);
    auto encoded = encode_packet(pkt);

    // Force kind to a value >= kCount_.
    const std::uint8_t bad_kind =
        static_cast<std::uint8_t>(PacketKind::kCount_) + 1U;
    encoded[0] = bad_kind;

    EXPECT_FALSE(decode_packet(encoded).has_value());
}

// ---------------------------------------------------------------------------
// 6. MockTransportSendRecv — send() accumulates; inject()+recv() drains FIFO
// ---------------------------------------------------------------------------
TEST(MockTransportTest, SendRecvRoundTrip)
{
    MockTransport transport;

    // No packets yet.
    EXPECT_FALSE(transport.recv().has_value());
    EXPECT_TRUE(transport.send_queue.empty());

    // send() accumulates.
    const Packet p1 = make_packet(PacketKind::kCreateRoom, 10U, 1U);
    const Packet p2 = make_packet(PacketKind::kJoinRoom,   10U, 2U);
    transport.send(p1);
    transport.send(p2);
    EXPECT_EQ(transport.send_queue.size(), 2U);
    EXPECT_EQ(transport.send_queue[0].kind, PacketKind::kCreateRoom);
    EXPECT_EQ(transport.send_queue[1].kind, PacketKind::kJoinRoom);

    // inject() queues inbound packets for recv().
    const Packet inbound = make_packet(PacketKind::kSyncRoomState, 10U, 0U);
    transport.inject(inbound);
    transport.inject(p1);

    // recv() drains FIFO.
    const auto r1 = transport.recv();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->kind, PacketKind::kSyncRoomState);

    const auto r2 = transport.recv();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->kind, PacketKind::kCreateRoom);

    // Queue now empty.
    EXPECT_FALSE(transport.recv().has_value());
    EXPECT_TRUE(transport.recv_queue.empty());
}

// ---------------------------------------------------------------------------
// 7. SyncRoomStatePayload — kSyncRoomState carries a player-array payload
//    round-trip intact.
//
//    We simulate a minimal binary player-array encoding:
//      For each player: [player_id : uint64_t LE] [is_ready : uint8_t]
//      (9 bytes per player)
//    Two players → 18-byte payload.
// ---------------------------------------------------------------------------
TEST(PacketCodec, SyncRoomStatePayload)
{
    // Build payload: player 101 (ready=1) + player 202 (ready=0).
    std::vector<std::uint8_t> payload;
    payload.reserve(18U);

    // Player 101, is_ready = 1.
    for (int shift = 0; shift < 64; shift += 8)
        payload.push_back(static_cast<std::uint8_t>((101ULL >> static_cast<unsigned>(shift)) & 0xFFU));
    payload.push_back(1U);

    // Player 202, is_ready = 0.
    for (int shift = 0; shift < 64; shift += 8)
        payload.push_back(static_cast<std::uint8_t>((202ULL >> static_cast<unsigned>(shift)) & 0xFFU));
    payload.push_back(0U);

    ASSERT_EQ(payload.size(), 18U);

    const Packet original = make_packet(PacketKind::kSyncRoomState,
                                         /*room_id=*/   55U,
                                         /*player_id=*/  0U,   // server → broadcast
                                         payload);

    const auto encoded = encode_packet(original);
    ASSERT_EQ(encoded.size(), kMinPacketSize + 18U);

    const auto decoded = decode_packet(encoded);
    ASSERT_TRUE(decoded.has_value());

    EXPECT_EQ(decoded->kind,    PacketKind::kSyncRoomState);
    EXPECT_EQ(decoded->room_id, 55U);
    ASSERT_EQ(decoded->payload.size(), 18U);
    EXPECT_EQ(decoded->payload, payload);

    // Verify player 101's ready flag survived the round-trip.
    EXPECT_EQ(decoded->payload[8],  1U);  // is_ready byte for player 101
    EXPECT_EQ(decoded->payload[17], 0U);  // is_ready byte for player 202
}
