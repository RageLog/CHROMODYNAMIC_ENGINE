// =============================================================================
// CHROMODYNAMIC — cd/net/ReliableChannel.hpp
// Phase 468 / M0 wave — ack-windowed reliable channel.
//
// `AckWindowChannel` is a STANDALONE reliable-delivery channel sitting
// directly on top of an `IConnection` (no ChannelMux required). Where
// `Retransmit.hpp::ReliableChannel` provides the full RFC 6298 / SACK /
// AIMD stack on top of ChannelMux, this primitive is a focused
// production-grade building block:
//
//   * Fixed-size 64-bit ack window (latest seq + 63 previous bitfield)
//   * Per-frame timestamp + retransmit-on-timeout (single RTO knob)
//   * Header-framed payload: u8 kind | u32 seq | u32 ack | u64 ack_bits
//
// Wire format (little-endian, 17-byte header):
//
//   ┌─────────────────────────────────────────────────────────────┐
//   │ u8  kind       (0 = kData, 1 = kAckOnly)                    │
//   │ u32 sequence   (sender's per-frame seq, 1-based)            │
//   │ u32 ack        (highest seq the sender has received from us)│
//   │ u64 ack_bits   (bitfield: bit k => ack-k seen)              │
//   │ ... payload (kData only) ...                                │
//   └─────────────────────────────────────────────────────────────┘
//
// Each `send()` call ships one Data frame and records it in `pending_`
// keyed by sequence. Each `tick(now)`:
//   1. Drains the underlying IConnection.
//   2. For every Data frame received, payload is appended to `inbox_`
//      and the receiver's ack/ack_bits are advanced.
//   3. For every ack info (Data or AckOnly), pending entries whose
//      sequence is acknowledged (== ack OR bit set in ack_bits) are
//      released and contribute an RTT sample.
//   4. Any pending entry older than `rto_` is retransmitted (max
//      `max_retries_`); on exhaustion it is dropped and counted under
//      `permanent_loss_count()`.
//   5. If the receiver has seen any new sequence since the last egress,
//      a piggyback AckOnly frame goes out to keep the sender's
//      ack-window fresh even when traffic is one-directional.
//
// Header-only. Depends on cd::net::IConnection.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/net/IConnection.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace cd::net
{

class AckWindowChannel
{
public:
    using Clock = std::chrono::steady_clock;

    /// 17-byte header per frame on the wire.
    static constexpr std::size_t kHeaderSize = 1 + 4 + 4 + 8;

    /// Frame kind discriminator.
    enum class Kind : std::uint8_t
    {
        kData    = 0,
        kAckOnly = 1,
    };

    AckWindowChannel(IConnection& transport,
                     std::chrono::milliseconds rto = std::chrono::milliseconds { 100 },
                     std::uint32_t max_retries = 8) noexcept
        : transport_ { &transport }
        , rto_ { rto }
        , max_retries_ { max_retries }
    {
    }

    /// Push `payload` onto the wire as a new reliable Data frame.
    /// Returns the assigned (1-based) sequence number.
    [[nodiscard]] cd::core::Result<std::uint32_t>
    send(std::span<const std::byte> payload)
    {
        const auto seq = ++next_send_seq_;  // 1-based, 0 = sentinel "none"
        auto framed = build_frame_(Kind::kData, seq, payload);
        auto r = transport_->send({ framed.data(), framed.size() });
        if (!r.has_value())
        {
            --next_send_seq_;  // roll back so caller can retry seq.
            return std::unexpected(r.error());
        }
        Pending p;
        p.framed = std::move(framed);
        p.seq = seq;
        p.send_time = Clock::now();
        p.retries = 0;
        pending_[seq] = std::move(p);
        return seq;
    }

    /// Pull the next ordered payload. Returns kWouldBlock when nothing
    /// is contiguous-ready. Call `tick()` first to drain inbound frames.
    [[nodiscard]] cd::core::Result<std::vector<std::byte>> receive()
    {
        if (delivery_queue_.empty())
            return std::unexpected(net_errors::make(net_errors::Code::kWouldBlock));
        auto front = std::move(delivery_queue_.front());
        delivery_queue_.pop_front();
        return front;
    }

    /// Drive the channel: drain wire, dispatch acks, retransmit stale
    /// pending, optionally piggyback an AckOnly frame.
    void tick(Clock::time_point now)
    {
        // 1. Drain underlying transport.
        while (true)
        {
            auto bytes = transport_->receive();
            if (!bytes.has_value())
                break;
            handle_frame_(*bytes, now);
        }
        // 2. Retransmit any pending older than rto_.
        retransmit_(now);
        // 3. Piggyback ack info if receiver saw new traffic and we have
        //    nothing else to send.
        maybe_emit_ack_only_();
    }

    [[nodiscard]] std::size_t pending_send_count() const noexcept
    {
        return pending_.size();
    }
    [[nodiscard]] std::uint32_t retransmit_count() const noexcept
    {
        return retransmit_count_;
    }
    [[nodiscard]] std::uint32_t permanent_loss_count() const noexcept
    {
        return permanent_loss_count_;
    }
    [[nodiscard]] std::uint32_t duplicate_drop_count() const noexcept
    {
        return duplicate_drops_;
    }
    [[nodiscard]] std::size_t ready_count() const noexcept
    {
        return delivery_queue_.size();
    }
    [[nodiscard]] std::chrono::nanoseconds last_rtt() const noexcept
    {
        return last_rtt_;
    }
    [[nodiscard]] std::uint32_t latest_ack_seen() const noexcept
    {
        return latest_ack_seen_;
    }
    [[nodiscard]] std::uint64_t ack_bits_seen() const noexcept
    {
        return ack_bits_seen_;
    }

private:
    struct Pending
    {
        std::vector<std::byte> framed;
        std::uint32_t seq { 0 };
        Clock::time_point send_time {};
        std::uint32_t retries { 0 };
    };

    [[nodiscard]] std::vector<std::byte>
    build_frame_(Kind kind, std::uint32_t seq, std::span<const std::byte> payload) const
    {
        std::vector<std::byte> out;
        out.reserve(kHeaderSize + payload.size());
        out.push_back(std::byte { static_cast<std::uint8_t>(kind) });
        write_u32_(out, seq);
        write_u32_(out, latest_ack_seen_);
        write_u64_(out, ack_bits_seen_);
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    static void write_u32_(std::vector<std::byte>& out, std::uint32_t v) noexcept
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(std::byte { static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu) });
    }
    static void write_u64_(std::vector<std::byte>& out, std::uint64_t v) noexcept
    {
        for (int i = 0; i < 8; ++i)
            out.push_back(std::byte { static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu) });
    }
    [[nodiscard]] static std::uint32_t read_u32_(const std::byte* p) noexcept
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[i])) << (i * 8);
        return v;
    }
    [[nodiscard]] static std::uint64_t read_u64_(const std::byte* p) noexcept
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(p[i])) << (i * 8);
        return v;
    }

    void handle_frame_(const std::vector<std::byte>& bytes, Clock::time_point now)
    {
        if (bytes.size() < kHeaderSize)
            return;
        const auto kind = static_cast<Kind>(static_cast<std::uint8_t>(bytes[0]));
        const auto seq = read_u32_(bytes.data() + 1);
        const auto ack = read_u32_(bytes.data() + 5);
        const auto ack_bits = read_u64_(bytes.data() + 9);

        // Process the peer's ack info regardless of kind.
        discharge_pending_(ack, ack_bits, now);

        if (kind == Kind::kAckOnly)
            return;
        if (seq == 0)
            return;  // sentinel — invalid Data frame

        // Update receiver-side ack window.
        if (seq > latest_ack_seen_)
        {
            const std::uint32_t shift = seq - latest_ack_seen_;
            if (shift >= 64)
                ack_bits_seen_ = 0;
            else
            {
                // Shift previous window up by `shift`. The OLD latest
                // becomes "shift-1" steps behind the new latest.
                ack_bits_seen_ = (ack_bits_seen_ << shift)
                               | (1ULL << (shift - 1));
            }
            latest_ack_seen_ = seq;
            ack_pending_ = true;
        }
        else
        {
            const std::uint32_t age = latest_ack_seen_ - seq;
            if (age == 0)
            {
                // Duplicate of latest_ack_seen_.
                ++duplicate_drops_;
                ack_pending_ = true;
                return;
            }
            if (age >= 64)
            {
                // Too old — outside window. Ignore but ack so peer can
                // unstick its retransmits.
                ack_pending_ = true;
                ++duplicate_drops_;
                return;
            }
            const std::uint64_t bit = 1ULL << (age - 1);
            if (ack_bits_seen_ & bit)
            {
                ++duplicate_drops_;
                ack_pending_ = true;
                return;
            }
            ack_bits_seen_ |= bit;
        }
        ack_pending_ = true;

        // Stash payload for ordered delivery.
        std::vector<std::byte> payload { bytes.begin() + kHeaderSize, bytes.end() };
        inbox_[seq] = std::move(payload);
        flush_delivery_();
    }

    void discharge_pending_(std::uint32_t ack, std::uint64_t ack_bits, Clock::time_point now)
    {
        if (ack == 0)
            return;
        for (auto it = pending_.begin(); it != pending_.end();)
        {
            const auto s = it->second.seq;
            bool acked = (s == ack);
            if (!acked && s < ack)
            {
                const std::uint32_t age = ack - s;
                if (age <= 64)
                {
                    const std::uint64_t bit = 1ULL << (age - 1);
                    acked = (ack_bits & bit) != 0;
                }
            }
            if (acked)
            {
                if (it->second.retries == 0)
                    last_rtt_ = now - it->second.send_time;
                it = pending_.erase(it);
            }
            else
                ++it;
        }
    }

    void retransmit_(Clock::time_point now)
    {
        std::vector<std::uint32_t> permanent_drops;
        for (auto& [seq, p] : pending_)
        {
            if (now - p.send_time < rto_)
                continue;
            if (p.retries >= max_retries_)
            {
                permanent_drops.push_back(seq);
                continue;
            }
            // Refresh the header with current ack info before reshipping.
            // (cheaper than re-allocating: patch in-place if shape is
            // identical, otherwise rebuild.)
            std::span<const std::byte> payload_span;
            if (p.framed.size() >= kHeaderSize)
                payload_span = { p.framed.data() + kHeaderSize,
                                  p.framed.size() - kHeaderSize };
            auto fresh = build_frame_(Kind::kData, p.seq, payload_span);
            p.framed = std::move(fresh);
            (void)transport_->send({ p.framed.data(), p.framed.size() });
            p.send_time = now;
            ++p.retries;
            ++retransmit_count_;
        }
        for (auto seq : permanent_drops)
        {
            pending_.erase(seq);
            ++permanent_loss_count_;
        }
    }

    void flush_delivery_()
    {
        while (true)
        {
            auto it = inbox_.find(next_deliver_seq_ + 1);
            if (it == inbox_.end())
                return;
            delivery_queue_.push_back(std::move(it->second));
            inbox_.erase(it);
            ++next_deliver_seq_;
        }
    }

    void maybe_emit_ack_only_()
    {
        if (!ack_pending_)
            return;
        // Synthesize a 0-payload Data with kind=kAckOnly, seq=0.
        auto framed = build_frame_(Kind::kAckOnly, 0, {});
        (void)transport_->send({ framed.data(), framed.size() });
        ack_pending_ = false;
    }

    IConnection* transport_;
    std::chrono::milliseconds rto_;
    std::uint32_t max_retries_;

    std::uint32_t next_send_seq_ { 0 };           // 1-based
    std::uint32_t next_deliver_seq_ { 0 };        // delivered through this seq
    std::uint32_t latest_ack_seen_ { 0 };
    std::uint64_t ack_bits_seen_ { 0 };
    bool ack_pending_ { false };

    std::unordered_map<std::uint32_t, Pending> pending_;
    std::unordered_map<std::uint32_t, std::vector<std::byte>> inbox_;
    std::deque<std::vector<std::byte>> delivery_queue_;

    std::uint32_t retransmit_count_ { 0 };
    std::uint32_t permanent_loss_count_ { 0 };
    std::uint32_t duplicate_drops_ { 0 };
    std::chrono::nanoseconds last_rtt_ { 0 };
};

}  // namespace cd::net
