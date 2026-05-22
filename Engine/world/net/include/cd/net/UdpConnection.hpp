// =============================================================================
// CHROMODYNAMIC — cd/net/UdpConnection.hpp
// Phase 6 / Wave 37 — real UDP IConnection backend.
//
// `make_udp_connection` returns a connected `IConnection` backed by a
// non-blocking UDP socket. Datagrams sent on it land on `remote_addr:
// remote_port`; datagrams arriving at `local_port` are delivered to the
// caller via `receive()`. The endpoint is "connected" in the loose UDP
// sense — there is no handshake; the OS just remembers the peer address
// for outbound calls.
//
// `make_udp_pair_localhost` is the test convenience: two endpoints bound
// to different localhost ports, each one pointing at the other. Round-
// trip tests use this to exercise the wire path without a server stub.
//
// Cross-platform:
//   * Windows  → Winsock2 + ws2_32.lib (linked from the audio/io tier
//                already; this file pulls the symbol in via #pragma
//                comment(lib, ...) when MSVC is the compiler).
//   * Linux    → POSIX socket / fcntl(O_NONBLOCK). No extra link libs.
//   * macOS    → same as Linux; SO_NOSIGPIPE skipped (UDP doesn't raise).
//
// All sockets are non-blocking. `receive()` returns kWouldBlock when no
// datagram is available; `send()` returns kBackendError if the OS layer
// refuses (EMSGSIZE, ENETDOWN, etc.) with a human-readable message.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/Result.hpp>
#include <cd/net/IConnection.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace cd::net
{

/// Build a UDP-backed IConnection. `remote_addr` must be a dotted-quad
/// IPv4 string ("127.0.0.1", "10.0.0.5"); hostname resolution is not in
/// the v1 scope. `local_port == 0` lets the OS pick an ephemeral port —
/// useful for "client" endpoints that don't need a fixed bind.
[[nodiscard]] cd::core::Result<std::unique_ptr<IConnection>>
make_udp_connection(std::uint16_t local_port,
                    std::string_view remote_addr,
                    std::uint16_t remote_port);

/// Build a pair of localhost-bound UDP endpoints pointing at each other.
/// Both ports must be free; the function returns kBackendError if either
/// `bind()` fails (e.g. port already in use). Use port 0 for either
/// slot to ask the OS for an ephemeral pair.
[[nodiscard]] cd::core::Result<
    std::pair<std::unique_ptr<IConnection>, std::unique_ptr<IConnection>>>
make_udp_pair_localhost(std::uint16_t port_a, std::uint16_t port_b);

}  // namespace cd::net
