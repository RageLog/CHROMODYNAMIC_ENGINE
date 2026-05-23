// =============================================================================
// CHROMODYNAMIC — cd/time/HiResClock.hpp
// ADR-005 §F — sub-microsecond timestamping for profilers and frame pacer.
//
// Uses platform-native monotonic counter directly:
//   - Windows : QueryPerformanceCounter (QPC) + QueryPerformanceFrequency
//   - POSIX   : clock_gettime(CLOCK_MONOTONIC_RAW) on Linux, CLOCK_UPTIME_RAW on macOS
//   - Fallback: std::chrono::steady_clock
//
// **Never use RDTSC for time**: TSC is not invariant across cores and is
// virtualisation-hostile. The dedicated profiler cycle counter in
// cd::profile::cycle_counter is the only legitimate RDTSC consumer.
// =============================================================================
#pragma once

#include <cd/time/IClock.hpp>

#include <cstdint>

namespace cd::time
{

/// Platform-direct monotonic timestamp in nanoseconds since an arbitrary
/// (but stable for the process lifetime) epoch.
[[nodiscard]] std::uint64_t hires_now_ns() noexcept;

class HiResClock final : public IClock
{
public:
    [[nodiscard]] TimePoint now() const noexcept override
    {
        return TimePoint {} + Nanoseconds { static_cast<std::int64_t>(hires_now_ns()) };
    }

    [[nodiscard]] TimeMode mode() const noexcept override
    {
        return TimeMode::HiResRaw;
    }

    [[nodiscard]] static HiResClock& instance() noexcept
    {
        static HiResClock s;
        return s;
    }
};

}  // namespace cd::time
