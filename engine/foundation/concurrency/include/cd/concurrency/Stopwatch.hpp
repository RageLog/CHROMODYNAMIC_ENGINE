// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Stopwatch.hpp
// Phase 20.E / Wave 182 — header-only steady_clock-based stopwatch.
//
// Every sample / benchmark / profiler harness currently rolls its own
// `auto t0 = steady_clock::now(); ... duration ...`. This primitive
// names the pattern so cd::bench + cd::profile + sample code share a
// single API.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <cstdint>

namespace cd::concurrency
{

class Stopwatch
{
public:
    using Clock = std::chrono::steady_clock;

    Stopwatch() noexcept { restart(); }

    /// Restart the timer to "now". Returns the elapsed duration since
    /// the previous start, so callers can do
    /// `auto last = sw.restart(); // elapsed for the prior interval`.
    std::chrono::nanoseconds restart() noexcept
    {
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - start_);
        start_ = now;
        return elapsed;
    }

    [[nodiscard]] std::chrono::nanoseconds elapsed() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start_);
    }

    [[nodiscard]] double elapsed_seconds() const noexcept
    {
        return static_cast<double>(elapsed().count()) * 1e-9;
    }

    [[nodiscard]] double elapsed_ms() const noexcept
    {
        return static_cast<double>(elapsed().count()) * 1e-6;
    }

    [[nodiscard]] double elapsed_us() const noexcept
    {
        return static_cast<double>(elapsed().count()) * 1e-3;
    }

private:
    Clock::time_point start_ {};
};

}  // namespace cd::concurrency
