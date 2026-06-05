// =============================================================================
// CHROMODYNAMIC — cd/network/lobby/SocketTransport.hpp
// Phase 754 / FINALE-4 W1-E1 — Sprint-3 real socket transport
//
// Drop-in counterpart to MockTransport that ships packets over a real UDP
// socket. Used by the lobby protocol to talk between hosts on a LAN.
//
// Public types:
//   SocketTransport — send()/recv() over cd::net::IConnection (UDP datagram).
//
// Design intent:
//   The Lobby never sees a real socket. It calls send(Packet) and recv() the
//   exact same way it does with MockTransport. SocketTransport encodes each
//   outbound Packet via encode_packet() and feeds the bytes to a UDP
//   IConnection. Inbound datagrams are decoded back to Packet via
//   decode_packet(); malformed datagrams are silently dropped (UDP is lossy
//   by design — there is no contract that every recv() yields a packet).
//
// Concurrency:
//   Not thread-safe. Single-thread usage matches the rest of cd::network::lobby
//   (Lobby and MockTransport have the same policy).
//
// Failure model:
//   configure_localhost_pair() returns std::nullopt if either OS bind fails
//   (port already in use, no permission, etc.) — same convention as the
//   test_net Vulkan/UDP tests that GTEST_SKIP on port collisions.
//
// SOTA note:
//   Valve GNS / yojimbo / ENet all expose a similar "datagram in, datagram
//   out" socket facade; the lobby layer above doesn't care which one wires
//   the bytes. Sprint-4+ can swap the IConnection backend for QUIC / WebRTC
//   without touching Lobby code.
// =============================================================================
#pragma once

#include <cd/net/IConnection.hpp>
#include <cd/network/lobby/Packet.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace cd::network::lobby
{

// ---------------------------------------------------------------------------
// SocketTransport
// ---------------------------------------------------------------------------

/// Real-socket lobby transport. send(Packet) encodes + ships over UDP;
/// recv() polls the underlying socket and decodes any pending datagram.
///
/// Construct via configure_localhost_pair() (test convenience: two
/// endpoints bound to localhost) or via configure_udp() in production —
/// the public API is identical regardless of backend.
///
/// Typical pattern (test):
/// @code
///   auto pair = SocketTransport::configure_localhost_pair(0U, 0U);
///   if (!pair.has_value()) { GTEST_SKIP() << "port bind failed"; }
///   auto& [host, client] = *pair;
///
///   host.send(Packet{ PacketKind::kSyncRoomState, ... });
///   // ... brief settle on the kernel UDP queue ...
///   auto pkt = client.recv();
/// @endcode
class SocketTransport
{
public:
    SocketTransport() noexcept = default;
    ~SocketTransport() noexcept = default;

    SocketTransport(const SocketTransport&) = delete;
    SocketTransport& operator=(const SocketTransport&) = delete;
    SocketTransport(SocketTransport&&) noexcept = default;
    SocketTransport& operator=(SocketTransport&&) noexcept = default;

    /// Construct around an already-built IConnection (production path).
    /// Takes ownership of `conn`.
    explicit SocketTransport(std::unique_ptr<cd::net::IConnection> conn) noexcept
        : connection_ { std::move(conn) }
    {
    }

    // ---- Production API (same shape as MockTransport) ----

    /// Encode `pkt` and ship it over the wire. Silently drops the packet if
    /// the connection is closed or the OS rejects the send — UDP is best-
    /// effort and lobby retransmit lives one layer up (cd::net::ReliableChannel).
    void send(const Packet& pkt);

    /// Poll the socket; return the next decoded packet, or std::nullopt if
    /// nothing is available (would-block) or the datagram failed to decode.
    [[nodiscard]] std::optional<Packet> recv();

    // ---- Lifecycle ----

    /// True iff this transport wraps a connection in kConnected state.
    [[nodiscard]] bool is_connected() const noexcept;

    /// Close the underlying connection. Idempotent.
    void close() noexcept;

    // ---- Factories ----

    /// Build a pair of localhost-bound UDP endpoints suitable for tests:
    /// each transport's send() lands on the other's recv(). Pass 0 for an
    /// OS-assigned ephemeral port. Returns std::nullopt if either bind
    /// fails (port collision in CI — caller should GTEST_SKIP).
    [[nodiscard]] static std::optional<std::pair<SocketTransport, SocketTransport>>
    configure_localhost_pair(std::uint16_t port_a, std::uint16_t port_b);

    /// Production factory: bind to `local_port`, point at `remote_addr:
    /// remote_port`. Returns std::nullopt on any OS failure.
    [[nodiscard]] static std::optional<SocketTransport>
    configure_udp(std::uint16_t local_port,
                  std::string_view remote_addr,
                  std::uint16_t remote_port);

private:
    std::unique_ptr<cd::net::IConnection> connection_;
};

}  // namespace cd::network::lobby
