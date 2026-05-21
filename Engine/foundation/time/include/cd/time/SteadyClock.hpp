// =============================================================================
// CHROMODYNAMIC — cd/time/SteadyClock.hpp
// ADR-005 §F + ADR-017 P0
//
// Production wall-clock backed by std::chrono::steady_clock (which on every
// supported platform delegates to QueryPerformanceCounter / clock_gettime
// CLOCK_MONOTONIC / mach_absolute_time).
// =============================================================================
#pragma once

#include <cd/time/IClock.hpp>

namespace cd::time
{

class SteadyClock final : public IClock
{
public:
    [[nodiscard]] TimePoint now() const noexcept override
    {
        return Clock::now();
    }

    [[nodiscard]] TimeMode mode() const noexcept override
    {
        return TimeMode::WallClock;
    }

    /// Convenience: return a process-wide singleton instance. Use when no
    /// injection point is available (e.g., free functions during bootstrap).
    [[nodiscard]] static SteadyClock& instance() noexcept
    {
        static SteadyClock s;
        return s;
    }
};

/// Free-function fast path; does not pay the virtual dispatch cost.
[[nodiscard]] inline TimePoint now() noexcept
{
    return Clock::now();
}

}  // namespace cd::time
