// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Flag.hpp
// Phase 94.B / Wave 262 — atomic boolean with intent-named API.
//
// `Flag` wraps `std::atomic<bool>` with names that document intent:
//   * `raise()` — set to true.
//   * `lower()` — set to false.
//   * `is_raised()` — read.
//   * `try_lower()` — exchange-and-test (return previous value).
//
// Plus `wait_until_raised()` polling with a `cd::concurrency::Backoff`
// (Phase 40) for "wait for some event without a condition variable"
// patterns. Use over raw `atomic<bool>` when read sites want named
// semantics ("is shutdown requested?" vs "load()").
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/concurrency/Backoff.hpp>

#include <atomic>

namespace cd::concurrency
{

class Flag
{
public:
    explicit Flag(bool initial = false) noexcept : value_ { initial } {}

    void raise() noexcept  { value_.store(true,  std::memory_order_release); }
    void lower() noexcept  { value_.store(false, std::memory_order_release); }

    [[nodiscard]] bool is_raised() const noexcept
    {
        return value_.load(std::memory_order_acquire);
    }

    /// Atomically lower the flag, returning the previous value.
    bool try_lower() noexcept
    {
        return value_.exchange(false, std::memory_order_acq_rel);
    }

    /// Atomically raise the flag, returning the previous value.
    bool try_raise() noexcept
    {
        return value_.exchange(true, std::memory_order_acq_rel);
    }

    /// Spin-poll until raised. Returns immediately if already raised.
    void wait_until_raised()
    {
        Backoff b;
        while (!is_raised()) b.pause();
    }

private:
    std::atomic<bool> value_;
};

}  // namespace cd::concurrency
