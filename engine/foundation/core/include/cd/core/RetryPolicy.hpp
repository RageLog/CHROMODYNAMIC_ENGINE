// =============================================================================
// CHROMODYNAMIC — cd/core/RetryPolicy.hpp
// Phase 66.A / Wave 234 — retry policy with exponential backoff.
//
// `RetryPolicy { max_attempts, base_delay_ms, max_delay_ms, factor }`
// captures the "try up to N times, sleeping a*factor^(i-1) ms between
// each retry up to max_delay_ms" pattern that asset loaders, network
// reconnect, and HTTP fetch all need.
//
// `attempt_delay_ms(attempt)` returns the suggested sleep duration
// for `attempt` ∈ [1, max_attempts]. Caller drives the loop.
//
// Pairs with cd::concurrency::Backoff (Phase 40) — Backoff is the
// CPU-spin variant; RetryPolicy is the IO-friendly variant with
// millisecond-scale sleeps.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::core
{

struct RetryPolicy
{
    std::uint32_t max_attempts  { 3 };
    std::uint32_t base_delay_ms { 100 };
    std::uint32_t max_delay_ms  { 10000 };
    float         factor        { 2.0F };

    [[nodiscard]] std::uint32_t attempt_delay_ms(std::uint32_t attempt) const noexcept
    {
        if (attempt <= 1) return base_delay_ms;
        auto d = static_cast<float>(base_delay_ms);
        for (std::uint32_t i = 1; i < attempt; ++i) d *= factor;
        d = std::min(d, static_cast<float>(max_delay_ms));
        return static_cast<std::uint32_t>(d);
    }

    [[nodiscard]] bool should_retry(std::uint32_t attempt) const noexcept
    {
        return attempt < max_attempts;
    }
};

}  // namespace cd::core
