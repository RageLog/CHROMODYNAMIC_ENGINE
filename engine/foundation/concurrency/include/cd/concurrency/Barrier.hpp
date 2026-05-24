// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Barrier.hpp
// Phase 47.B / Wave 215 — reusable counted barrier.
//
// Wraps `std::barrier` (C++20). Unlike Latch (single-use), Barrier
// resets automatically after every cohort of N arrivers has passed —
// useful for game-loop sync points where N worker threads must finish
// stage X before moving to stage X+1 every frame.
//
// `arrive_and_wait()` blocks until N threads call it, then unblocks
// all and resets the counter. Optional `on_completion` callback fires
// in the last-arriver thread before unblock.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <barrier>
#include <cstddef>
#include <functional>

namespace cd::concurrency
{

class Barrier
{
public:
    explicit Barrier(std::ptrdiff_t count)
        : barrier_ { count }, count_ { count } {}

    void arrive_and_wait()
    {
        barrier_.arrive_and_wait();
    }

    [[nodiscard]] std::ptrdiff_t count() const noexcept { return count_; }

private:
    struct NoCompletion
    {
        void operator()() noexcept {}
    };

    std::barrier<NoCompletion> barrier_;
    std::ptrdiff_t             count_;
};

}  // namespace cd::concurrency
