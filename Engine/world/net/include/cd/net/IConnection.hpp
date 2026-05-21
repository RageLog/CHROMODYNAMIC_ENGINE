// =============================================================================
// CHROMODYNAMIC — cd/net/IConnection.hpp
// Phase 4 / Sprint S4.7 — networking abstraction.
//
// `IConnection` is a duplex, message-oriented transport. Each `send()`
// hands the implementation an opaque byte payload; `receive()` returns
// the next pending payload. Concrete backends (UDP, TCP, WebRTC,
// SteamNetworkingSockets, ENet) translate to/from their wire format.
//
// The built-in `LoopbackConnection` connects two endpoints in-process —
// invaluable for tests and split-screen / single-machine multiplayer.
//
// SOTA references: GameNetworkingSockets [Valve 2020], yojimbo, ENet,
// quiche (QUIC). All converge on a "datagram in, datagram out" surface
// over an underlying reliability/ordering policy — this interface mirrors
// that and lets the policy live in the implementation.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cd::net
{

namespace net_errors
{
inline constexpr std::uint32_t kDomain = 0x0010;
enum class Code : std::uint32_t
{
    kOk = 0,
    kDisconnected = 1,
    kInvalidArgument = 2,
    kWouldBlock = 3,
    kBackendError = 4,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace net_errors

enum class ConnectionState : std::uint8_t
{
    kConnected,
    kDisconnected
};

class IConnection
{
public:
    IConnection() noexcept = default;
    virtual ~IConnection() = default;
    IConnection(const IConnection&) = delete;
    IConnection& operator=(const IConnection&) = delete;
    IConnection(IConnection&&) = delete;
    IConnection& operator=(IConnection&&) = delete;

    /// Push one message onto the outbound queue. The bytes are copied;
    /// the caller's buffer can be reused immediately. Returns
    /// kDisconnected when the link is closed.
    [[nodiscard]] virtual cd::core::Result<void> send(std::span<const std::byte> bytes) = 0;

    /// Pull the next inbound message. Returns an empty vector + kWouldBlock
    /// when nothing is queued; kDisconnected on a closed link.
    [[nodiscard]] virtual cd::core::Result<std::vector<std::byte>> receive() = 0;

    [[nodiscard]] virtual ConnectionState state() const noexcept = 0;
    [[nodiscard]] virtual std::size_t bytes_sent() const noexcept = 0;
    [[nodiscard]] virtual std::size_t bytes_received() const noexcept = 0;

    virtual void close() = 0;
};

/// Create a connected pair of in-process endpoints. Returns
/// {endpoint_a, endpoint_b} where bytes sent on `a` arrive on `b`'s
/// receive() and vice-versa. Ownership of both endpoints is handed back
/// to the caller; closing one transitions the other to kDisconnected on
/// the next operation.
[[nodiscard]] std::pair<std::unique_ptr<IConnection>, std::unique_ptr<IConnection>> make_loopback_pair();

}  // namespace cd::net
