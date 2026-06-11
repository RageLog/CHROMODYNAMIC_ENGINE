// =============================================================================
// CHROMODYNAMIC — cd/net/LatencyStats.hpp
// Phase 84.A / Wave 252 — round-trip-time accumulator (EWMA + jitter).
//
// Records RTT samples (microseconds) and exposes:
//   * `current_rtt_us()` — exponentially-weighted moving average,
//     smoothing factor α = 0.125 (RFC 6298 default).
//   * `jitter_us()` — EWMA of |sample - prev_rtt|, α = 0.25.
//   * `sample_count()`.
//   * `min_us()`, `max_us()`.
//
// The TCP congestion-control RFC formulas, applied to UDP game-net
// channels for connection-quality telemetry.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <cstdint>
#include <limits>

namespace cd::net
{

class LatencyStats
{
public:
    void record(std::uint32_t rtt_us) noexcept
    {
        if (count_ == 0)
        {
            rtt_  = static_cast<float>(rtt_us);
            jitter_ = 0.0F;
        }
        else
        {
            const auto r = static_cast<float>(rtt_us);
            const float diff = (r > rtt_) ? (r - rtt_) : (rtt_ - r);
            jitter_ = jitter_ + 0.25F * (diff - jitter_);
            rtt_    = rtt_ + 0.125F * (r - rtt_);
        }
        ++count_;
        min_us_ = std::min(rtt_us, min_us_);
        max_us_ = std::max(rtt_us, max_us_);
    }

    [[nodiscard]] float         current_rtt_us() const noexcept { return rtt_; }
    [[nodiscard]] float         jitter_us()      const noexcept { return jitter_; }
    [[nodiscard]] std::uint32_t min_us()         const noexcept { return (count_ == 0) ? 0u : min_us_; }
    [[nodiscard]] std::uint32_t max_us()         const noexcept { return (count_ == 0) ? 0u : max_us_; }
    [[nodiscard]] std::uint64_t sample_count()   const noexcept { return count_; }

    void reset() noexcept
    {
        rtt_ = 0.0F;
        jitter_ = 0.0F;
        count_ = 0;
        min_us_ = std::numeric_limits<std::uint32_t>::max();
        max_us_ = 0;
    }

private:
    float         rtt_    { 0.0F };
    float         jitter_ { 0.0F };
    std::uint64_t count_  { 0 };
    std::uint32_t min_us_ { std::numeric_limits<std::uint32_t>::max() };
    std::uint32_t max_us_ { 0 };
};

}  // namespace cd::net
