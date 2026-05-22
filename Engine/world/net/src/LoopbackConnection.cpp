// =============================================================================
// CHROMODYNAMIC — cd/net/LoopbackConnection.cpp
//
// In-process pair: each endpoint pushes onto the other's inbound queue.
// Thread-safe (mutex-guarded queues) so a producer thread can hand bytes
// to a consumer thread without external synchronization — split-screen
// multiplayer is the canonical use case.
// =============================================================================
#include <cd/net/IConnection.hpp>

#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

namespace cd::net
{

namespace
{

/// Shared state between the two endpoints of a loopback pair: two queues
/// (A→B and B→A) plus a shared closed flag, all protected by a single
/// mutex. The endpoints are non-owning views into this shared block.
struct LoopbackShared
{
    std::mutex m {};
    std::queue<std::vector<std::byte>> queue_ab {};  // a sends → b receives
    std::queue<std::vector<std::byte>> queue_ba {};  // b sends → a receives
    bool closed { false };
};

class LoopbackEndpoint final : public IConnection
{
public:
    LoopbackEndpoint(std::shared_ptr<LoopbackShared> shared, bool is_side_a) noexcept
        : shared_ { std::move(shared) }
        , is_side_a_ { is_side_a }
    {
    }

    [[nodiscard]] cd::core::Result<void> send(std::span<const std::byte> bytes) override
    {
        std::scoped_lock lock { shared_->m };
        if (shared_->closed)
        {
            return std::unexpected(net_errors::make(net_errors::Code::kDisconnected, "send: link closed"));
        }
        auto& dst = is_side_a_ ? shared_->queue_ab : shared_->queue_ba;
        dst.emplace(bytes.begin(), bytes.end());
        bytes_sent_ += bytes.size();
        return {};
    }

    [[nodiscard]] cd::core::Result<std::vector<std::byte>> receive() override
    {
        std::scoped_lock lock { shared_->m };
        auto& src = is_side_a_ ? shared_->queue_ba : shared_->queue_ab;
        if (src.empty())
        {
            if (shared_->closed)
            {
                return std::unexpected(net_errors::make(net_errors::Code::kDisconnected, "receive: link closed"));
            }
            return std::unexpected(net_errors::make(net_errors::Code::kWouldBlock, "receive: no pending messages"));
        }
        auto msg = std::move(src.front());
        src.pop();
        bytes_received_ += msg.size();
        return msg;
    }

    [[nodiscard]] ConnectionState state() const noexcept override
    {
        std::scoped_lock lock { shared_->m };
        return shared_->closed ? ConnectionState::kDisconnected : ConnectionState::kConnected;
    }

    [[nodiscard]] std::size_t bytes_sent() const noexcept override
    {
        return bytes_sent_;
    }

    [[nodiscard]] std::size_t bytes_received() const noexcept override
    {
        return bytes_received_;
    }

    void close() override
    {
        std::scoped_lock lock { shared_->m };
        shared_->closed = true;
    }

private:
    std::shared_ptr<LoopbackShared> shared_ {};
    bool is_side_a_ { true };
    std::size_t bytes_sent_ { 0 };
    std::size_t bytes_received_ { 0 };
};

}  // namespace

std::pair<std::unique_ptr<IConnection>, std::unique_ptr<IConnection>> make_loopback_pair()
{
    auto shared = std::make_shared<LoopbackShared>();
    return { std::make_unique<LoopbackEndpoint>(shared, /*is_side_a=*/true),
             std::make_unique<LoopbackEndpoint>(shared, /*is_side_a=*/false) };
}

}  // namespace cd::net
