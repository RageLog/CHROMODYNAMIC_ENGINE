// =============================================================================
// CHROMODYNAMIC — engine/world/net/src/UdpConnection.cpp
//
// See UdpConnection.hpp. Concrete UdpConnection class lives entirely in
// this TU so the public header stays free of Winsock / POSIX headers.
// =============================================================================
#include <cd/net/UdpConnection.hpp>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    if defined(_MSC_VER)
#        pragma comment(lib, "ws2_32.lib")
#    endif
using sock_t = SOCKET;
constexpr sock_t kInvalidSocket = INVALID_SOCKET;
namespace
{
int close_socket(sock_t s) noexcept { return ::closesocket(s); }
int last_socket_error() noexcept { return ::WSAGetLastError(); }
constexpr int kWouldBlockErr = WSAEWOULDBLOCK;
}  // namespace
#else
#    include <arpa/inet.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>
using sock_t = int;
constexpr sock_t kInvalidSocket = -1;
namespace
{
int close_socket(sock_t s) noexcept { return ::close(s); }
int last_socket_error() noexcept { return errno; }
constexpr int kWouldBlockErr = EWOULDBLOCK;
}  // namespace
#endif

namespace cd::net
{

namespace
{

#if defined(_WIN32)
// Winsock requires WSAStartup before any socket call. Use a process-wide
// guard so multiple connections share the init/cleanup cycle.
class WinsockInit
{
public:
    WinsockInit() noexcept
    {
        WSADATA data;
        const auto rc = ::WSAStartup(MAKEWORD(2, 2), &data);
        ok_ = (rc == 0);
    }
    ~WinsockInit()
    {
        if (ok_)
            ::WSACleanup();
    }
    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    bool ok_ { false };
};

WinsockInit& winsock_guard()
{
    static WinsockInit s;
    return s;
}
#endif

bool set_non_blocking(sock_t s) noexcept
{
#if defined(_WIN32)
    // FIONBIO is a u_long macro that triggers -Wsign-conversion on
    // Clang in -Werror mode (`unsigned long` → `long` in the IOC macro).
    // The macro is unfixable from our side — suppress locally.
#    if defined(__clang__)
#        pragma clang diagnostic push
#        pragma clang diagnostic ignored "-Wsign-conversion"
#    endif
    u_long flag = 1;
    const auto rc = ::ioctlsocket(s, FIONBIO, &flag);
#    if defined(__clang__)
#        pragma clang diagnostic pop
#    endif
    return rc == 0;
#else
    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0)
        return false;
    return ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

class UdpConnection final : public IConnection
{
public:
    UdpConnection(sock_t s, sockaddr_in remote) noexcept
        : socket_ { s }, remote_ { remote }, state_ { ConnectionState::kConnected }
    {
    }

    ~UdpConnection() override
    {
        if (socket_ != kInvalidSocket)
            close_socket(socket_);
    }

    [[nodiscard]] cd::core::Result<void> send(std::span<const std::byte> bytes) override
    {
        if (state_ != ConnectionState::kConnected)
            return std::unexpected(net_errors::make(net_errors::Code::kDisconnected));
        const auto* data = reinterpret_cast<const char*>(bytes.data());
        const auto n = ::sendto(socket_, data, static_cast<int>(bytes.size()), 0,
                                reinterpret_cast<const sockaddr*>(&remote_), sizeof(remote_));
        if (n < 0)
            return std::unexpected(net_errors::make(net_errors::Code::kBackendError,
                                                    "sendto failed"));
        bytes_sent_ += static_cast<std::size_t>(n);
        return {};
    }

    [[nodiscard]] cd::core::Result<std::vector<std::byte>> receive() override
    {
        if (state_ != ConnectionState::kConnected)
            return std::unexpected(net_errors::make(net_errors::Code::kDisconnected));
        constexpr std::size_t kMaxDatagram = 65535;
        std::vector<std::byte> buf(kMaxDatagram);
        sockaddr_in from {};
#if defined(_WIN32)
        int from_len = sizeof(from);
#else
        socklen_t from_len = sizeof(from);
#endif
        const auto n = ::recvfrom(socket_, reinterpret_cast<char*>(buf.data()),
                                  static_cast<int>(buf.size()), 0,
                                  reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n < 0)
        {
            const auto err = last_socket_error();
            if (err == kWouldBlockErr)
                return std::unexpected(net_errors::make(net_errors::Code::kWouldBlock));
            return std::unexpected(net_errors::make(net_errors::Code::kBackendError,
                                                    "recvfrom failed"));
        }
        buf.resize(static_cast<std::size_t>(n));
        bytes_received_ += static_cast<std::size_t>(n);
        return buf;
    }

    [[nodiscard]] ConnectionState state() const noexcept override { return state_; }
    [[nodiscard]] std::size_t bytes_sent() const noexcept override { return bytes_sent_; }
    [[nodiscard]] std::size_t bytes_received() const noexcept override { return bytes_received_; }

    void close() override
    {
        if (state_ == ConnectionState::kConnected)
        {
            close_socket(socket_);
            socket_ = kInvalidSocket;
            state_ = ConnectionState::kDisconnected;
        }
    }

private:
    sock_t socket_;
    sockaddr_in remote_;
    ConnectionState state_;
    std::size_t bytes_sent_ { 0 };
    std::size_t bytes_received_ { 0 };
};

[[nodiscard]] cd::core::Result<std::unique_ptr<IConnection>>
build_udp_endpoint(std::uint16_t local_port, std::string_view remote_addr,
                   std::uint16_t remote_port)
{
#if defined(_WIN32)
    if (!winsock_guard().ok())
        return std::unexpected(net_errors::make(net_errors::Code::kBackendError,
                                                "WSAStartup failed"));
#endif
    const sock_t s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalidSocket)
        return std::unexpected(net_errors::make(net_errors::Code::kBackendError,
                                                "socket() failed"));

    sockaddr_in local {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(local_port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0)
    {
        close_socket(s);
        return std::unexpected(net_errors::make(net_errors::Code::kBackendError,
                                                "bind() failed"));
    }

    if (!set_non_blocking(s))
    {
        close_socket(s);
        return std::unexpected(net_errors::make(net_errors::Code::kBackendError,
                                                "non-blocking switch failed"));
    }

    sockaddr_in remote {};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(remote_port);
    const std::string addr_str { remote_addr };
    if (::inet_pton(AF_INET, addr_str.c_str(), &remote.sin_addr) != 1)
    {
        close_socket(s);
        return std::unexpected(net_errors::make(net_errors::Code::kInvalidArgument,
                                                "remote_addr not an IPv4 dotted-quad"));
    }
    return std::unique_ptr<IConnection> { new UdpConnection(s, remote) };
}

}  // namespace

cd::core::Result<std::unique_ptr<IConnection>>
make_udp_connection(std::uint16_t local_port, std::string_view remote_addr,
                    std::uint16_t remote_port)
{
    return build_udp_endpoint(local_port, remote_addr, remote_port);
}

cd::core::Result<std::pair<std::unique_ptr<IConnection>, std::unique_ptr<IConnection>>>
make_udp_pair_localhost(std::uint16_t port_a, std::uint16_t port_b)
{
    auto a = build_udp_endpoint(port_a, "127.0.0.1", port_b);
    if (!a.has_value())
        return std::unexpected(a.error());
    auto b = build_udp_endpoint(port_b, "127.0.0.1", port_a);
    if (!b.has_value())
        return std::unexpected(b.error());
    return std::make_pair(std::move(*a), std::move(*b));
}

}  // namespace cd::net
