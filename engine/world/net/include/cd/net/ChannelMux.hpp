// =============================================================================
// CHROMODYNAMIC — cd/net/ChannelMux.hpp
// Phase 5 / S4.x — multiplexed channels over an IConnection.
//
// Splits one IConnection (loopback for tests, real UDP/TCP in
// production) into N typed channels:
//   * kReliableOrdered — drops duplicates by sequence number, delivers
//                       payloads to the caller in send order.
//   * kUnreliableUnordered — passes everything through; out-of-order
//                       or dropped packets are the caller's problem.
//
// Wire format (little-endian):
//   ┌────────────────────────────────────┐
//   │ u8  channel_id                     │
//   │ u8  channel_type (0=rel, 1=unrel)  │
//   │ u32 sequence                       │
//   │ ... payload ...                    │
//   └────────────────────────────────────┘
//
// Header is 6 bytes. Retransmit + ack logic for unreliable transports
// (real UDP) is the follow-up sprint — this primitive establishes the
// API + serialization invariants so the retransmit layer slots in.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/net/IConnection.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cd::net
{

enum class ChannelType : std::uint8_t
{
    kReliableOrdered = 0,
    kUnreliableUnordered = 1,
};

inline constexpr std::size_t kChannelHeaderSize = 6;

struct ChannelMessage
{
    std::uint8_t channel { 0 };
    ChannelType type { ChannelType::kReliableOrdered };
    std::uint32_t sequence { 0 };
    std::vector<std::byte> payload;
};

class ChannelMux
{
public:
    /// Wraps an existing IConnection (loopback / TCP / UDP). The mux
    /// does not own the connection — caller keeps it alive.
    explicit ChannelMux(IConnection& transport) noexcept : transport_ { &transport } {}

    /// Send a payload on `channel` with the given delivery semantics.
    /// Auto-assigns the sequence number per (channel, type) pair.
    [[nodiscard]] cd::core::Result<void> send(std::uint8_t channel,
                                              ChannelType type,
                                              std::span<const std::byte> payload)
    {
        std::vector<std::byte> framed;
        framed.reserve(kChannelHeaderSize + payload.size());
        framed.push_back(std::byte { channel });
        framed.push_back(std::byte { static_cast<std::uint8_t>(type) });
        const auto seq = next_send_seq_[channel]++;
        for (std::size_t k = 0; k < 4; ++k)
            framed.push_back(std::byte { static_cast<std::uint8_t>((seq >> (k * 8)) & 0xFFu) });
        framed.insert(framed.end(), payload.begin(), payload.end());
        return transport_->send({ framed.data(), framed.size() });
    }

    /// Pull the next inbound message. Reliable-ordered duplicates are
    /// dropped silently; out-of-order reliable messages get buffered
    /// here and delivered when the gap fills. Returns kWouldBlock when
    /// no message is ready.
    [[nodiscard]] cd::core::Result<ChannelMessage> receive()
    {
        // Drain any buffered in-order reliable message first.
        if (!ready_reliable_.empty())
        {
            ChannelMessage front = std::move(ready_reliable_.front());
            ready_reliable_.erase(ready_reliable_.begin());
            return front;
        }

        auto frame = transport_->receive();
        if (!frame.has_value())
            return std::unexpected(frame.error());
        if (frame->size() < kChannelHeaderSize)
            return std::unexpected(net_errors::make(net_errors::Code::kBackendError, "short frame"));

        ChannelMessage msg;
        msg.channel = static_cast<std::uint8_t>((*frame)[0]);
        msg.type = static_cast<ChannelType>(static_cast<std::uint8_t>((*frame)[1]));
        msg.sequence = 0;
        for (std::size_t k = 0; k < 4; ++k)
            msg.sequence |= static_cast<std::uint32_t>(static_cast<std::uint8_t>((*frame)[2 + k])) << (k * 8);
        msg.payload.assign(frame->begin() + kChannelHeaderSize, frame->end());

        if (msg.type == ChannelType::kReliableOrdered)
        {
            auto& expected = next_recv_seq_[msg.channel];
            if (msg.sequence < expected || seen_seq_[msg.channel].count(msg.sequence))
            {
                // Duplicate / re-order: drop and recurse to fetch next.
                return receive();
            }
            seen_seq_[msg.channel].insert(msg.sequence);
            if (msg.sequence == expected)
            {
                ++expected;
                // Flush any contiguous buffered out-of-order messages.
                for (auto it = pending_.find(expected); it != pending_.end(); it = pending_.find(expected))
                {
                    ready_reliable_.push_back(std::move(it->second));
                    pending_.erase(it);
                    ++expected;
                }
                return msg;
            }
            // Future-seq: buffer, recurse.
            pending_.emplace(msg.sequence, std::move(msg));
            return receive();
        }

        // Unreliable: forward as-is.
        return msg;
    }

private:
    IConnection* transport_ { nullptr };
    std::array<std::uint32_t, 256> next_send_seq_ {};
    std::unordered_map<std::uint8_t, std::uint32_t> next_recv_seq_;
    std::unordered_map<std::uint8_t, std::unordered_set<std::uint32_t>> seen_seq_;
    std::unordered_map<std::uint32_t, ChannelMessage> pending_;
    std::vector<ChannelMessage> ready_reliable_;
};

}  // namespace cd::net
