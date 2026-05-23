// =============================================================================
// CHROMODYNAMIC — cd/time/Types.hpp
// ADR-005 §F (tri-clock model: real / game / hires) + ADR-017 P0 (DfH timing salvage)
//
// Engine-wide chrono aliases + conversion helpers. The engine NEVER uses
// std::chrono::system_clock for gameplay timing (it's wall-clock; subject to
// NTP, DST, and user changes). Use:
//   - real_clock : wall-clock; save timestamps + telemetry only
//   - game_clock : pausable/scaled gameplay tick (see SimClock)
//   - hires_clock: monotonic raw; profiler + frame pacer
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>
#include <cstdint>

namespace cd::time
{

enum class TimeMode : std::uint8_t
{
    WallClock,   // monotonic, system steady clock; advances with real time
    Simulation,  // scaled / pausable; advances per `tick()` and `set_scale()`
    HiResRaw,    // platform-direct monotonic counter, never paused
};

/// Default engine clock. Monotonic on every supported platform.
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

using Nanoseconds = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;
using Seconds = std::chrono::duration<double>;

template <class ToDuration = Duration, class Rep, class Period>
[[nodiscard]] constexpr ToDuration cast_duration(const std::chrono::duration<Rep, Period>& value) noexcept
{
    return std::chrono::duration_cast<ToDuration>(value);
}

template <class ToDuration = Duration>
[[nodiscard]] constexpr ToDuration since_epoch(const TimePoint& tp) noexcept
{
    return cast_duration<ToDuration>(tp.time_since_epoch());
}

template <class ToDuration = Duration>
[[nodiscard]] constexpr ToDuration elapsed(const TimePoint& start, const TimePoint& end) noexcept
{
    return cast_duration<ToDuration>(end - start);
}

template <class Rep, class Period>
[[nodiscard]] constexpr TimePoint to_time_point(const std::chrono::duration<Rep, Period>& value) noexcept
{
    return TimePoint {} + cast_duration<Duration>(value);
}

template <class ToDuration = Duration, class Rep, class Period>
[[nodiscard]] inline ToDuration scale_duration(const std::chrono::duration<Rep, Period>& value, double factor) noexcept
{
    return cast_duration<ToDuration>(std::chrono::duration<double>(value) * factor);
}

/// Convert engine Duration to floating-point seconds. Useful at API edges.
[[nodiscard]] constexpr double to_seconds(Duration d) noexcept
{
    return std::chrono::duration<double> { d }.count();
}

}  // namespace cd::time
