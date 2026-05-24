// =============================================================================
// CHROMODYNAMIC — cd/net/Throttle.hpp
// Phase 92.A / Wave 260 — token-bucket rate limiter.
//
// Classic token bucket:
//   * Bucket has a max capacity (max tokens).
//   * Tokens regenerate at `rate_per_sec`.
//   * `try_consume(n)` succeeds and removes n tokens if available;
//     otherwise leaves the bucket and returns false.
//   * `update(dt)` accumulates fresh tokens.
//
// Use for outbound packet rate limiting, action-spam prevention,
// API call ceiling.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>

namespace cd::net
{

class Throttle
{
public:
    Throttle(float capacity, float rate_per_sec) noexcept
        : capacity_ { capacity }, rate_ { rate_per_sec }, tokens_ { capacity }
    {
    }

    void update(float dt) noexcept
    {
        if (dt <= 0.0F) return;
        tokens_ = std::min(capacity_, tokens_ + rate_ * dt);
    }

    [[nodiscard]] bool try_consume(float n) noexcept
    {
        if (tokens_ < n) return false;
        tokens_ -= n;
        return true;
    }

    [[nodiscard]] float tokens() const noexcept { return tokens_; }
    [[nodiscard]] float capacity() const noexcept { return capacity_; }
    [[nodiscard]] float rate_per_sec() const noexcept { return rate_; }

    void reset() noexcept { tokens_ = capacity_; }

private:
    float capacity_;
    float rate_;
    float tokens_;
};

}  // namespace cd::net
