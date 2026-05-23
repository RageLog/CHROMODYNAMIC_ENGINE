// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Latch.hpp
// Phase 32.B / Wave 200 — single-use counted latch.
//
// Lightweight wrapper over std::latch (C++20). Threads call
// `count_down()` to decrement; `wait()` blocks until the counter
// reaches zero. The latch is **single-use** — once reached zero it
// cannot be reset (use a Barrier for that).
//
// Why wrap std::latch?
//   * Adds `try_wait()` (return immediately if ready), useful for
//     polling join sites without blocking.
//   * Adds explicit `is_ready()` query — std::latch only exposes
//     this as part of `try_wait()` semantics, but we want it as a
//     pure boolean without the wait-counter side effect.
//   * Standard count_down(n) overload exposed.
//
// Used by: parallel job join points (Phase 4 ThreadPool extension),
// asset-load fan-in (wait for N textures to decode), test scaffolding
// (don't `sleep_for` — anti-flakiness rule in CLAUDE.md §5).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <latch>
#include <cstddef>

namespace cd::concurrency
{

class Latch
{
public:
    explicit Latch(std::ptrdiff_t initial) noexcept : latch_ { initial } {}

    void count_down(std::ptrdiff_t n = 1) noexcept
    {
        latch_.count_down(n);
    }

    void wait() noexcept
    {
        latch_.wait();
    }

    /// Returns true if the counter has reached zero; does not block.
    [[nodiscard]] bool is_ready() noexcept
    {
        return latch_.try_wait();
    }

    /// Decrement and block until ready. Convenience for the typical
    /// last-worker pattern: `latch.arrive_and_wait();`.
    void arrive_and_wait() noexcept
    {
        latch_.arrive_and_wait();
    }

private:
    std::latch latch_;
};

}  // namespace cd::concurrency
