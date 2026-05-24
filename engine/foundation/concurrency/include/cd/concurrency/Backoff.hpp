// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Backoff.hpp
// Phase 39.B / Wave 207 — exponential backoff for contended retries.
//
// `Backoff` is the standard "spin lightly, then heavily, then yield"
// helper for lock-free CAS retry loops, spin locks, and busy-wait
// scopes where blocking is undesirable but unbounded spinning would
// starve other cores.
//
// Stages (matches Intel's Restless Multi-Lock recommendation):
//   1. count < 5:   `_mm_pause()` / yield-instruction, 1×2^count iterations.
//   2. 5..10:       std::this_thread::yield().
//   3. >= 10:       std::this_thread::sleep_for(stride×us).
//
// Use as:
//   Backoff b;
//   while (!try_acquire()) b.pause();
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
    #if __has_include(<immintrin.h>)
        #include <immintrin.h>
        #define CD_HAS_X86_PAUSE 1
    #else
        #define CD_HAS_X86_PAUSE 0
    #endif
#else
    #define CD_HAS_X86_PAUSE 0
#endif

namespace cd::concurrency
{

class Backoff
{
public:
    void pause() noexcept
    {
        if (count_ < 5)
        {
            const std::uint32_t iters = 1u << count_;
            for (std::uint32_t i = 0; i < iters; ++i)
            {
#if CD_HAS_X86_PAUSE
                _mm_pause();
#else
                // On non-x86 the loop body acts as a soft delay.
                volatile std::uint32_t sink = 0;
                (void)sink;
#endif
            }
        }
        else if (count_ < 10)
        {
            std::this_thread::yield();
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::microseconds { count_ });
        }
        if (count_ < 20) ++count_;
    }

    void reset() noexcept { count_ = 0; }

    [[nodiscard]] std::uint32_t step() const noexcept { return count_; }

private:
    std::uint32_t count_ { 0 };
};

}  // namespace cd::concurrency
