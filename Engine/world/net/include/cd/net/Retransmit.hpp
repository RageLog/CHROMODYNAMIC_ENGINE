// =============================================================================
// CHROMODYNAMIC — cd/net/Retransmit.hpp
// Phase 6 / Wave 41 — ACK + RTO loop on top of ChannelMux.
//
// `ReliableChannel` upgrades a ChannelMux user channel from "unreliable
// unordered datagrams" to "reliable ordered byte stream" by adding its
// own per-frame sequence number, deduplication, and RTO-driven retry.
//
// Both the user channel and the ACK channel are configured at the
// ChannelMux level as `kUnreliableUnordered` — we deliberately bypass
// ChannelMux's built-in dedup so we can implement the retransmit
// logic at this layer (otherwise the retransmit's mux-side sequence
// would look like a fresh frame and bubble up as a duplicate to the
// application).
//
// Wire format on the user channel (ChannelMux header NOT included —
// that wraps this transparently):
//   ┌──────────────────────────────┐
//   │ u32 reliable_sequence        │  4 bytes, little-endian
//   │ ... payload ...              │
//   └──────────────────────────────┘
//
// Wire format on the ACK channel: a single u32 little-endian sequence.
//
// RFC 6298 adaptive RTO (Wave 45):
//   * First RTT sample R0  : SRTT = R0, RTTVAR = R0/2,
//                            RTO = SRTT + max(G, K*RTTVAR), K=4.
//   * Subsequent samples R : RTTVAR = (1-β)*RTTVAR + β*|SRTT - R|
//                            SRTT   = (1-α)*SRTT   + α*R
//                            RTO    = SRTT + max(G, K*RTTVAR)
//                            α = 1/8, β = 1/4, K = 4.
//   * Karn's algorithm     : RTT samples taken from retransmitted frames
//                            are SKIPPED (we can't tell which transmission
//                            the ACK corresponds to).
//   * RTO doubling         : each retransmit doubles the per-frame RTO
//                            until current_rto() observes a fresh ACK.
//   * Clamped to [rto_min_, rto_max_] — defaults 25 ms / 5 s, configurable
//     via the constructor.
//
// What this layer does NOT do (Phase 6 follow-ups):
//   * Cumulative / SACK style ACK — every frame gets its own ACK packet.
//   * Congestion control / window — the API does not back-pressure send().
//
// Clock injection: `tick()` takes a `now` time point so unit tests can
// fast-forward time without sleeping. Real callers pass
// `std::chrono::steady_clock::now()`.
//
// Header-only. Depends on ChannelMux + standard library.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/net/ChannelMux.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cd::net
{

class ReliableChannel
{
public:
    using Clock = std::chrono::steady_clock;

    ReliableChannel(ChannelMux& mux,
                    std::uint8_t user_channel,
                    std::uint8_t ack_channel,
                    std::chrono::milliseconds initial_rto = std::chrono::milliseconds { 200 },
                    std::uint32_t max_retries = 5,
                    std::chrono::milliseconds rto_min = std::chrono::milliseconds { 25 },
                    std::chrono::milliseconds rto_max = std::chrono::milliseconds { 5000 }) noexcept
        : mux_ { &mux }
        , user_channel_ { user_channel }
        , ack_channel_ { ack_channel }
        , rto_ { initial_rto }
        , rto_min_ { rto_min }
        , rto_max_ { rto_max }
        , max_retries_ { max_retries }
    {
    }

    /// Send `payload` on the user channel. Returns the application-
    /// visible sequence number, which is also embedded as a 4-byte
    /// prefix on the wire so the receiver can dedup retransmits.
    [[nodiscard]] cd::core::Result<std::uint32_t>
    send(std::span<const std::byte> payload)
    {
        const auto seq = next_send_seq_++;
        Pending p;
        p.framed = build_user_frame_(seq, payload);
        p.seq = seq;
        p.send_time = Clock::now();
        p.retries = 0;

        auto r = mux_->send(user_channel_, ChannelType::kUnreliableUnordered,
                            { p.framed.data(), p.framed.size() });
        if (!r.has_value())
            return std::unexpected(r.error());
        pending_[seq] = std::move(p);
        return seq;
    }

    /// Pull the next inbound payload from the user channel in delivery
    /// order. Returns kWouldBlock when nothing is ready. Call `tick()`
    /// first to drain the wire.
    [[nodiscard]] cd::core::Result<std::vector<std::byte>> receive()
    {
        if (!ready_.empty())
        {
            auto front = std::move(ready_.front());
            ready_.erase(ready_.begin());
            return front;
        }
        return std::unexpected(net_errors::make(net_errors::Code::kWouldBlock));
    }

    /// Drive the loop: drain the underlying mux, dispatch ACKs,
    /// discharge pending entries, and retransmit RTO-expired frames.
    void tick(Clock::time_point now)
    {
        while (true)
        {
            auto msg = mux_->receive();
            if (!msg.has_value())
                break;
            if (msg->channel == user_channel_)
                handle_user_frame_(std::move(msg->payload));
            else if (msg->channel == ack_channel_)
                handle_ack_(msg->payload);
            // Other channels: ignored.
        }
        retransmit_(now);
        flush_ready_();
    }

    [[nodiscard]] std::size_t pending_send_count() const noexcept { return pending_.size(); }
    [[nodiscard]] std::uint32_t retransmit_count() const noexcept { return retransmit_count_; }
    [[nodiscard]] std::size_t ready_count() const noexcept { return ready_.size(); }
    [[nodiscard]] std::uint32_t duplicate_drop_count() const noexcept { return duplicate_drops_; }

    /// Current smoothed RTO estimate (RFC 6298). Stays at the initial
    /// value until the first non-retransmitted ACK delivers a sample.
    [[nodiscard]] std::chrono::milliseconds current_rto() const noexcept { return rto_; }
    /// Current smoothed RTT (zero until the first RTT sample arrives).
    [[nodiscard]] std::chrono::nanoseconds srtt() const noexcept { return srtt_; }
    /// Current RTT variance estimate (zero until the first sample).
    [[nodiscard]] std::chrono::nanoseconds rttvar() const noexcept { return rttvar_; }

private:
    struct Pending
    {
        std::vector<std::byte> framed;  ///< Full on-wire frame (prefix + payload).
        std::uint32_t seq { 0 };
        Clock::time_point send_time {};
        std::uint32_t retries { 0 };
        std::chrono::milliseconds effective_rto { 0 };  ///< 0 → use channel rto_.
    };

    [[nodiscard]] static std::vector<std::byte>
    build_user_frame_(std::uint32_t seq, std::span<const std::byte> payload)
    {
        std::vector<std::byte> framed;
        framed.reserve(4 + payload.size());
        for (int i = 0; i < 4; ++i)
            framed.push_back(std::byte { static_cast<std::uint8_t>((seq >> (i * 8)) & 0xFFu) });
        framed.insert(framed.end(), payload.begin(), payload.end());
        return framed;
    }

    [[nodiscard]] static std::uint32_t read_u32_(const std::byte* p) noexcept
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[i])) << (i * 8);
        return v;
    }

    void handle_user_frame_(std::vector<std::byte> bytes)
    {
        if (bytes.size() < 4)
            return;
        const auto seq = read_u32_(bytes.data());
        send_ack_(seq);

        if (seq < next_recv_seq_ || seen_.contains(seq))
        {
            ++duplicate_drops_;
            return;
        }
        seen_.insert(seq);
        // Strip prefix, store for ordered delivery.
        std::vector<std::byte> payload { bytes.begin() + 4, bytes.end() };
        inbound_[seq] = std::move(payload);
    }

    void handle_ack_(const std::vector<std::byte>& payload)
    {
        if (payload.size() < 4)
            return;
        const auto seq = read_u32_(payload.data());
        auto it = pending_.find(seq);
        if (it == pending_.end())
            return;
        // Karn's algorithm: only sample RTT for non-retransmitted frames.
        if (it->second.retries == 0)
            update_rtt_(Clock::now() - it->second.send_time);
        pending_.erase(it);
    }

    void retransmit_(Clock::time_point now)
    {
        for (auto& [seq, p] : pending_)
        {
            // Each pending entry has its own RTO: starts at the channel
            // RTO, doubles with each retransmit (RFC 6298 §5 step 5.5).
            const auto effective = p.effective_rto.count() == 0 ? rto_ : p.effective_rto;
            if (now - p.send_time < effective)
                continue;
            if (p.retries >= max_retries_)
                continue;
            (void)mux_->send(user_channel_, ChannelType::kUnreliableUnordered,
                             { p.framed.data(), p.framed.size() });
            p.send_time = now;
            ++p.retries;
            // Double the per-frame RTO; clamp to channel max.
            auto next = (effective.count() == 0 ? rto_ : effective) * 2;
            if (next > rto_max_)
                next = rto_max_;
            p.effective_rto = next;
            ++retransmit_count_;
        }
    }

    void update_rtt_(std::chrono::nanoseconds r)
    {
        if (srtt_.count() == 0)
        {
            // First sample (RFC 6298 §2.2): SRTT = R, RTTVAR = R/2.
            srtt_ = r;
            rttvar_ = r / 2;
        }
        else
        {
            // RTTVAR = (1 - β) * RTTVAR + β * |SRTT - R|   (β = 1/4)
            // SRTT   = (1 - α) * SRTT   + α * R            (α = 1/8)
            const auto diff = (srtt_ > r) ? (srtt_ - r) : (r - srtt_);
            rttvar_ = (rttvar_ * 3 + diff) / 4;
            srtt_ = (srtt_ * 7 + r) / 8;
        }
        // RTO = SRTT + max(G, K * RTTVAR), K = 4. Clamp to [min, max].
        // G (clock granularity) is conservatively treated as 0 — steady_clock
        // gives at worst ms-class granularity on Windows, and the rttvar
        // term dominates anyway.
        auto rto_ns = srtt_ + rttvar_ * 4;
        auto rto_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rto_ns);
        if (rto_ms < rto_min_)
            rto_ms = rto_min_;
        if (rto_ms > rto_max_)
            rto_ms = rto_max_;
        rto_ = rto_ms;
    }

    void flush_ready_()
    {
        // Move every contiguous-from-next_recv_seq_ entry into ready_.
        while (true)
        {
            auto it = inbound_.find(next_recv_seq_);
            if (it == inbound_.end())
                return;
            ready_.push_back(std::move(it->second));
            inbound_.erase(it);
            ++next_recv_seq_;
        }
    }

    void send_ack_(std::uint32_t seq)
    {
        std::array<std::byte, 4> buf {};
        for (int i = 0; i < 4; ++i)
            buf[static_cast<std::size_t>(i)] =
                std::byte { static_cast<std::uint8_t>((seq >> (i * 8)) & 0xFFu) };
        (void)mux_->send(ack_channel_, ChannelType::kUnreliableUnordered,
                         { buf.data(), buf.size() });
    }

    ChannelMux* mux_;
    std::uint8_t user_channel_;
    std::uint8_t ack_channel_;
    std::chrono::milliseconds rto_;
    std::chrono::milliseconds rto_min_;
    std::chrono::milliseconds rto_max_;
    std::chrono::nanoseconds srtt_ { 0 };
    std::chrono::nanoseconds rttvar_ { 0 };
    std::uint32_t max_retries_;
    std::uint32_t next_send_seq_ { 0 };
    std::uint32_t next_recv_seq_ { 0 };
    std::unordered_map<std::uint32_t, Pending> pending_;
    std::unordered_map<std::uint32_t, std::vector<std::byte>> inbound_;
    std::unordered_set<std::uint32_t> seen_;
    std::vector<std::vector<std::byte>> ready_;
    std::uint32_t retransmit_count_ { 0 };
    std::uint32_t duplicate_drops_ { 0 };
};

}  // namespace cd::net
