// =============================================================================
// CHROMODYNAMIC — cd/concurrency/AtomicCounter.hpp
// Phase 23.D / Wave 188 — thread-safe monotonically-increasing counter.
//
// Use cases: frame counters, ID generators, statistics buckets. Wraps
// std::atomic<std::uint64_t> with the common API a caller wants
// (next() = post-increment, value() = current, reset()).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>
#include <cstdint>

namespace cd::concurrency
{

class AtomicCounter
{
public:
    AtomicCounter() noexcept = default;
    explicit AtomicCounter(std::uint64_t initial) noexcept : value_ { initial } {}

    /// Atomically increment and return the *previous* value. Equivalent
    /// to fetch_add(1) — the typical "give me a unique id" pattern.
    std::uint64_t next() noexcept
    {
        return value_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Current snapshot. Not synchronized with concurrent next() — the
    /// returned value can be stale by the time the caller reads it.
    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return value_.load(std::memory_order_relaxed);
    }

    /// Reset to `v`. Caller must ensure no concurrent next() races.
    void reset(std::uint64_t v = 0) noexcept
    {
        value_.store(v, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> value_ { 0 };
};

}  // namespace cd::concurrency
