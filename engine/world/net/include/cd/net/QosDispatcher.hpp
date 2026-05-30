// =============================================================================
// CHROMODYNAMIC — cd/net/QosDispatcher.hpp
// Phase 468 / M0 wave — priority-tier dispatcher with token-bucket budget.
//
// `QosDispatcher` is a send-side traffic shaper that multiplexes
// application-level send requests across three coarse delivery tiers:
//
//   * kReliable             — guaranteed delivery, in-order (sticky).
//   * kUnreliable           — fire-and-forget; lost packets are gone.
//   * kUnreliableSequenced  — fire-and-forget, but newer beats older;
//                             receiver discards stale arrivals.
//
// Per tier the dispatcher carries:
//   * a FIFO of pending payloads (priority assignment by tier rank).
//   * an integer token-bucket rate budget — refill_rate tokens / sec,
//     bucket capacity `burst`. Tokens consumed per dispatched message
//     equal `1 + ceil(payload_size / cost_per_byte)` (cost_per_byte=0
//     disables byte-cost accounting, 1 token per message flat).
//   * a stat counter (dispatched / dropped-budget / dropped-overflow).
//
// `tick(now)`:
//   1. Refill all token buckets based on elapsed time since last tick.
//   2. Walk tiers in priority order (kReliable first, then
//      kUnreliableSequenced, then kUnreliable).
//   3. For each non-empty tier, dispatch messages while tokens remain
//      via the user-supplied `Sink` functor:
//         `bool sink(QosTier, std::span<const std::byte>)` → false to
//         abort the burst (e.g. transport WouldBlock).
//   4. Stop when all tiers drained OR sink returns false.
//
// Header-only; depends on std + cd/core.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>
#include <cd/net/IConnection.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>

namespace cd::net
{

enum class QosTier : std::uint8_t
{
    kReliable            = 0,
    kUnreliableSequenced = 1,
    kUnreliable          = 2,
};

inline constexpr std::size_t kQosTierCount = 3;

[[nodiscard]] constexpr std::uint32_t qos_priority(QosTier t) noexcept
{
    // Higher = served first. kReliable wins ties.
    switch (t)
    {
        case QosTier::kReliable:            return 3;
        case QosTier::kUnreliableSequenced: return 2;
        case QosTier::kUnreliable:          return 1;
    }
    return 0;
}

struct QosBudget
{
    /// Tokens refilled per second. 0 → infinite budget (no shaping).
    double refill_rate { 0.0 };
    /// Maximum tokens carried over between ticks (burst tolerance).
    double burst { 0.0 };
    /// If > 0, each dispatched message also costs ceil(size / cost_per_byte)
    /// extra tokens. 0 → flat 1 token per message.
    std::uint32_t cost_per_byte { 0 };
};

struct QosTierStats
{
    std::uint64_t dispatched { 0 };
    std::uint64_t dropped_budget { 0 };       // sink declined while budget was 0
    std::uint64_t dropped_queue_overflow { 0 };
};

class QosDispatcher
{
public:
    using Clock = std::chrono::steady_clock;
    using Sink  = std::function<bool(QosTier, std::span<const std::byte>)>;

    /// `max_queue_per_tier`: hard cap on pending messages per tier.
    /// Beyond this, `enqueue()` drops the OLDEST in-flight message
    /// (sequenced/unreliable semantics) or returns kBackendError
    /// (reliable: caller must back off).
    explicit QosDispatcher(std::size_t max_queue_per_tier = 256) noexcept
        : max_queue_ { max_queue_per_tier }
    {
        // Default: no shaping — refill_rate=0 means infinite budget.
        // tier_ default-constructs all three TierState{} entries; nothing
        // else to do here.
        last_tick_ = Clock::time_point {};
    }

    /// Configure the token-bucket budget for `tier`. Resets the
    /// available-token counter to `burst` (full bucket).
    void set_budget(QosTier tier, QosBudget budget) noexcept
    {
        auto& t = tier_[static_cast<std::size_t>(tier)];
        t.budget = budget;
        t.tokens = budget.burst;
    }

    [[nodiscard]] const QosBudget& budget(QosTier tier) const noexcept
    {
        return tier_[static_cast<std::size_t>(tier)].budget;
    }

    [[nodiscard]] double tokens(QosTier tier) const noexcept
    {
        return tier_[static_cast<std::size_t>(tier)].tokens;
    }

    [[nodiscard]] const QosTierStats& stats(QosTier tier) const noexcept
    {
        return tier_[static_cast<std::size_t>(tier)].stats;
    }

    [[nodiscard]] std::size_t pending(QosTier tier) const noexcept
    {
        return tier_[static_cast<std::size_t>(tier)].queue.size();
    }

    /// Enqueue a payload for the given tier. Returns kBackendError for
    /// kReliable when the queue is full (drop unacceptable). For the
    /// other tiers, evicts the OLDEST pending message and accounts it
    /// under `dropped_queue_overflow`.
    [[nodiscard]] cd::core::Result<void>
    enqueue(QosTier tier, std::span<const std::byte> payload)
    {
        auto& t = tier_[static_cast<std::size_t>(tier)];
        if (t.queue.size() >= max_queue_)
        {
            if (tier == QosTier::kReliable)
                return std::unexpected(net_errors::make(net_errors::Code::kBackendError,
                                                        "reliable queue full"));
            t.queue.pop_front();
            ++t.stats.dropped_queue_overflow;
        }
        t.queue.emplace_back(payload.begin(), payload.end());
        return {};
    }

    /// Drive one dispatch pass at time `now`. Returns the number of
    /// messages handed to the sink. Stops on first sink rejection
    /// (treated as "transport not ready, retry next tick").
    std::size_t tick(Clock::time_point now, const Sink& sink)
    {
        // Refill buckets based on elapsed time.
        if (last_tick_ != Clock::time_point {})
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
                now - last_tick_).count();
            for (auto& t : tier_)
            {
                if (t.budget.refill_rate <= 0.0)
                    continue;
                t.tokens += t.budget.refill_rate * elapsed;
                if (t.tokens > t.budget.burst)
                    t.tokens = t.budget.burst;
            }
        }
        last_tick_ = now;

        // Visit tiers in priority order.
        const std::array<QosTier, kQosTierCount> order { {
            QosTier::kReliable,
            QosTier::kUnreliableSequenced,
            QosTier::kUnreliable,
        } };
        std::size_t dispatched = 0;
        for (auto tier : order)
        {
            auto& t = tier_[static_cast<std::size_t>(tier)];
            while (!t.queue.empty())
            {
                const auto cost = compute_cost_(t.budget, t.queue.front().size());
                if (t.budget.refill_rate > 0.0 && t.tokens < cost)
                {
                    // Out of budget on this tier — note the would-have-
                    // dispatched but keep payload queued for next tick.
                    ++t.stats.dropped_budget;
                    break;
                }
                auto& front = t.queue.front();
                if (!sink(tier, { front.data(), front.size() }))
                {
                    // Transport stalled — keep payload at head for retry.
                    return dispatched;
                }
                if (t.budget.refill_rate > 0.0)
                    t.tokens -= cost;
                ++t.stats.dispatched;
                ++dispatched;
                t.queue.pop_front();
            }
        }
        return dispatched;
    }

private:
    struct TierState
    {
        QosBudget budget {};
        double tokens { 0.0 };
        std::deque<std::vector<std::byte>> queue;
        QosTierStats stats {};
    };

    [[nodiscard]] static double compute_cost_(const QosBudget& b, std::size_t payload_size) noexcept
    {
        if (b.cost_per_byte == 0)
            return 1.0;
        const auto byte_cost = (payload_size + b.cost_per_byte - 1) / b.cost_per_byte;
        return 1.0 + static_cast<double>(byte_cost);
    }

    std::array<TierState, kQosTierCount> tier_ {};
    std::size_t max_queue_;
    Clock::time_point last_tick_ {};
};

}  // namespace cd::net
