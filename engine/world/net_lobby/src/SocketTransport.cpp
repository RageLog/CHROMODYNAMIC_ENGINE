// =============================================================================
// CHROMODYNAMIC — cd/network/lobby/SocketTransport.cpp
// Phase 754 / FINALE-4 W1-E1 — Sprint-3 implementation
// =============================================================================
#include <cd/network/lobby/SocketTransport.hpp>

#include <cd/net/UdpConnection.hpp>

#include <cstddef>
#include <cstring>
#include <span>
#include <utility>

namespace cd::network::lobby
{

// ---------------------------------------------------------------------------
// Send / recv
// ---------------------------------------------------------------------------

void SocketTransport::send(const Packet& pkt)
{
    if (!connection_)
        return;
    if (connection_->state() != cd::net::ConnectionState::kConnected)
        return;

    const auto encoded = encode_packet(pkt);
    // Reinterpret std::uint8_t bytes as std::byte for the IConnection API.
    // This is a layout-compatible cast; both types are 1-byte trivially-
    // copyable and the lifetime of `encoded` outlives the send() call.
    const auto* data = reinterpret_cast<const std::byte*>(encoded.data());
    const std::span<const std::byte> view { data, encoded.size() };
    (void)connection_->send(view);  // UDP is best-effort; silent drop on failure
}

std::optional<Packet> SocketTransport::recv()
{
    if (!connection_)
        return std::nullopt;
    if (connection_->state() != cd::net::ConnectionState::kConnected)
        return std::nullopt;

    auto recv_result = connection_->receive();
    if (!recv_result.has_value())
        return std::nullopt;  // kWouldBlock or kDisconnected — nothing to deliver

    const auto& bytes = *recv_result;
    if (bytes.empty())
        return std::nullopt;

    // std::byte and std::uint8_t share layout; decode_packet takes a
    // std::span<const std::uint8_t>.
    const auto* raw = reinterpret_cast<const std::uint8_t*>(bytes.data());
    return decode_packet(std::span<const std::uint8_t>{ raw, bytes.size() });
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool SocketTransport::is_connected() const noexcept
{
    return connection_ &&
           connection_->state() == cd::net::ConnectionState::kConnected;
}

void SocketTransport::close() noexcept
{
    if (connection_)
        connection_->close();
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

std::optional<std::pair<SocketTransport, SocketTransport>>
SocketTransport::configure_localhost_pair(std::uint16_t port_a,
                                          std::uint16_t port_b)
{
    // Concrete-port path: caller asked for a specific pair; defer to
    // cd::net::make_udp_pair_localhost which routes a<->b on those ports.
    if (port_a != 0U && port_b != 0U)
    {
        auto pair = cd::net::make_udp_pair_localhost(port_a, port_b);
        if (!pair.has_value())
            return std::nullopt;
        SocketTransport a { std::move(pair->first) };
        SocketTransport b { std::move(pair->second) };
        return std::make_pair(std::move(a), std::move(b));
    }

    // Ephemeral-port path: cd::net::make_udp_pair_localhost has a known
    // limitation when both ports are 0 — each endpoint's remote_port is
    // set to the *requested* (0) port rather than the OS-assigned one,
    // so datagrams have no valid destination. Work around it by walking
    // a fixed high-port window until we find a free pair. GTEST_SKIP fires
    // upstream if every candidate collides (CI sandbox with no socket
    // permissions, parallel runners, etc).
    constexpr std::uint16_t kBase     { 49160U };  // above IANA ephemeral start
    constexpr std::uint16_t kStride   { 2U };
    constexpr int           kAttempts { 64 };

    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        const auto candidate_a = static_cast<std::uint16_t>(
            kBase + static_cast<std::uint16_t>(attempt) * kStride);
        const auto candidate_b = static_cast<std::uint16_t>(candidate_a + 1U);

        auto pair = cd::net::make_udp_pair_localhost(candidate_a, candidate_b);
        if (pair.has_value())
        {
            SocketTransport a { std::move(pair->first) };
            SocketTransport b { std::move(pair->second) };
            return std::make_pair(std::move(a), std::move(b));
        }
    }
    return std::nullopt;
}

std::optional<SocketTransport>
SocketTransport::configure_udp(std::uint16_t local_port,
                               std::string_view remote_addr,
                               std::uint16_t remote_port)
{
    auto conn = cd::net::make_udp_connection(local_port, remote_addr, remote_port);
    if (!conn.has_value())
        return std::nullopt;
    return SocketTransport { std::move(*conn) };
}

}  // namespace cd::network::lobby
