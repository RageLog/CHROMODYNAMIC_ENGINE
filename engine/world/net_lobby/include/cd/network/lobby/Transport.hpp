// =============================================================================
// CHROMODYNAMIC — cd/network/lobby/Transport.hpp
// Phase 723 / Sprint W5B — cd::network::lobby Sprint-2 MockTransport
//
// In-process mock transport for deterministic lobby protocol testing.
//
// Public types:
//   MockTransport — queue-based send/recv with a test-facing inject() helper.
//
// Design intent:
//   Real lobby logic calls send() to emit packets and recv() to consume
//   inbound packets.  In tests, inject() pushes packets into recv_queue so
//   the lobby sees them as if they arrived over the wire — no real sockets
//   required.
//
// Concurrency:
//   Not thread-safe.  Callers that share a MockTransport across threads must
//   apply external synchronisation (same policy as Lobby itself).
//
// SOTA note:
//   Valve GNS test fixtures use a similar "fake transport" approach to verify
//   relay logic.  Unity Netcode for GameObjects provides a FakeNetworkDriver
//   for the same purpose.  The inject/drain pattern is also used in the Quake 3
//   network simulation test harness.
// =============================================================================
#pragma once

#include <cd/network/lobby/Packet.hpp>

#include <optional>
#include <vector>

namespace cd::network::lobby
{

// ---------------------------------------------------------------------------
// MockTransport
// ---------------------------------------------------------------------------

/// In-process mock transport: deterministic, no OS resources, no threads.
///
/// Typical test pattern:
/// @code
///   MockTransport transport;
///
///   // Simulate a packet arriving from the server.
///   transport.inject(Packet{ PacketKind::kJoinRoom, room_id, player_id, {} });
///
///   // Lobby logic drains it.
///   auto pkt = transport.recv();  // returns the injected packet
///
///   // Lobby logic sends a reply.
///   transport.send(Packet{ PacketKind::kSyncRoomState, ... });
///
///   // Test inspects the sent packet.
///   EXPECT_EQ(transport.send_queue.size(), 1U);
/// @endcode
class MockTransport
{
public:
    MockTransport() noexcept = default;
    ~MockTransport() noexcept = default;

    MockTransport(const MockTransport&) = delete;
    MockTransport& operator=(const MockTransport&) = delete;
    MockTransport(MockTransport&&) noexcept = default;
    MockTransport& operator=(MockTransport&&) noexcept = default;

    // ---- Production API ----

    /// Enqueue `pkt` on send_queue (simulates "sending over the wire").
    void send(const Packet& pkt);

    /// Pop the oldest packet from recv_queue.
    /// Returns std::nullopt when the queue is empty.
    [[nodiscard]] std::optional<Packet> recv();

    // ---- Test helper ----

    /// Push `pkt` onto the back of recv_queue so that the next recv() call
    /// returns it.  Use this in tests to simulate inbound server messages.
    void inject(const Packet& pkt);

    // ---- Observable state ----

    std::vector<Packet> send_queue;  ///< Packets queued for "transmission".
    std::vector<Packet> recv_queue;  ///< Packets waiting to be recv()'d.
};

}  // namespace cd::network::lobby
