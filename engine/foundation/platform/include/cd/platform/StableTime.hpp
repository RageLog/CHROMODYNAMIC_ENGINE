// =============================================================================
// CHROMODYNAMIC — cd/platform/StableTime.hpp
// Phase 35.B / Wave 203 — monotonic time + frame epoch.
//
// Two related queries:
//   * `monotonic_seconds()` — `steady_clock` wall time as f64 seconds,
//     unaffected by NTP corrections / DST / clock skew. Same baseline
//     between calls within a process. Returns 0.0 at program start by
//     subtracting a process-start anchor.
//   * `frame_epoch_seconds()` — like monotonic_seconds but reset every
//     `begin_frame()` so per-frame time stays inside [0, dt] bounds.
//     Used by editor command timestamps + replay diff.
//
// Why wrap chrono:
//   * Edit history / replay want seconds-as-f64, not duration<long, nano>.
//   * Multiple call sites (editor + audio + animation) shouldn't each
//     fetch the start anchor on first call (race-y across threads).
//     StableTime initializes the anchor at process load (constinit-ish).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <chrono>

namespace cd::platform
{

class StableTime
{
public:
    /// Seconds since the first call to any StableTime function (or
    /// since construction of the singleton — first-call wins).
    [[nodiscard]] static double monotonic_seconds() noexcept
    {
        const auto& s = singleton();
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double> { now - s.process_anchor_ }.count();
    }

    /// Reset the frame epoch to "now". Call once per main-loop tick.
    static void begin_frame() noexcept
    {
        auto& s = singleton();
        s.frame_anchor_ = std::chrono::steady_clock::now();
    }

    /// Seconds since the last `begin_frame()` call. Zero before the
    /// first begin_frame.
    [[nodiscard]] static double frame_epoch_seconds() noexcept
    {
        const auto& s = singleton();
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double> { now - s.frame_anchor_ }.count();
    }

private:
    StableTime() noexcept
        : process_anchor_ { std::chrono::steady_clock::now() },
          frame_anchor_ { process_anchor_ } {}

    static StableTime& singleton() noexcept
    {
        static StableTime s;
        return s;
    }

    std::chrono::steady_clock::time_point process_anchor_;
    std::chrono::steady_clock::time_point frame_anchor_;
};

}  // namespace cd::platform
