// =============================================================================
// CHROMODYNAMIC — cd/time/RateLimiter.hpp
// Phase 6 / Wave 57 — frame-rate cap with hybrid sleep/spin pacing.
//
// Pairs with `cd::time::FramePacer` (fixed simulation + interpolated
// render) by capping the outer frame rate to a target Hz (e.g. 60 / 144
// / 240). The hybrid sleep+spin strategy lifts the OS scheduler's
// ±1 ms jitter into roughly ±50 µs steady state on Windows desktop —
// good enough for power-friendly frame caps even with the OS coarse
// timer.
//
// Strategy:
//   1. Compute the deadline = last_wake + (1 / target_hz).
//   2. If `deadline - now > spin_threshold` (default 1 ms), sleep_for
//      the difference minus the threshold.
//   3. Busy-spin yielding until `now >= deadline`.
//   4. Record the deadline as the new last_wake.
//
// IClock injection: takes any `cd::time::IClock*`, defaulting to a
// process-wide steady clock. Tests inject a `cd::time::SimClock` to
// drive the limiter deterministically without sleeping.
// =============================================================================
#pragma once

#include <cd/time/IClock.hpp>
#include <cd/time/SteadyClock.hpp>
#include <cd/time/Types.hpp>

#include <chrono>
#include <cstdint>
#include <thread>

namespace cd::time
{

/// Shared process-wide steady clock used as the default for the pacing
/// classes below when the caller doesn't inject one explicitly.
[[nodiscard]] inline IClock& default_steady_clock() noexcept
{
    static SteadyClock s;
    return s;
}

class RateLimiter
{
public:
    explicit RateLimiter(std::uint32_t target_hz,
                         Duration spin_threshold = std::chrono::milliseconds { 1 },
                         IClock* clock = nullptr) noexcept
        : clock_ { clock != nullptr ? clock : &default_steady_clock() }
        , spin_threshold_ { spin_threshold }
    {
        set_target_hz(target_hz);
    }

    void set_target_hz(std::uint32_t hz) noexcept
    {
        target_hz_ = hz;
        if (hz == 0)
        {
            period_ = Duration::zero();
        }
        else
        {
            period_ = std::chrono::duration_cast<Duration>(
                std::chrono::nanoseconds { 1'000'000'000 / static_cast<std::int64_t>(hz) });
        }
        reset();
    }

    /// Block (sleep + brief spin) until the next frame deadline. No-op
    /// when target_hz is 0 ("uncapped").
    void await_next_frame()
    {
        if (period_ == Duration::zero())
            return;
        const auto now = clock_->now();
        if (last_wake_ == TimePoint {})
            last_wake_ = now;
        const auto deadline = last_wake_ + period_;
        if (deadline > now)
        {
            const auto remaining = deadline - now;
            if (remaining > spin_threshold_)
                std::this_thread::sleep_for(remaining - spin_threshold_);
            while (clock_->now() < deadline)
                std::this_thread::yield();
            last_wake_ = deadline;
            last_frame_dur_ = period_;
        }
        else
        {
            last_frame_dur_ = now - last_wake_;
            last_wake_ = now;
        }
    }

    void reset() noexcept
    {
        last_wake_ = TimePoint {};
        last_frame_dur_ = Duration::zero();
    }

    [[nodiscard]] std::uint32_t target_hz() const noexcept { return target_hz_; }
    [[nodiscard]] Duration period() const noexcept { return period_; }
    [[nodiscard]] Duration last_frame_duration() const noexcept { return last_frame_dur_; }

private:
    IClock* clock_;
    Duration spin_threshold_;
    std::uint32_t target_hz_ { 0 };
    Duration period_ { Duration::zero() };
    TimePoint last_wake_ {};
    Duration last_frame_dur_ { Duration::zero() };
};

/// Periodic tick: fires true from `tick()` whenever `interval` has
/// elapsed since the previous fire. Useful for log flush / network
/// heartbeat / save-on-interval patterns.
class IntervalTicker
{
public:
    explicit IntervalTicker(Duration interval, IClock* clock = nullptr) noexcept
        : clock_ { clock != nullptr ? clock : &default_steady_clock() }
        , interval_ { interval }
    {
        reset();
    }

    [[nodiscard]] bool tick() noexcept
    {
        const auto now = clock_->now();
        if (next_fire_ == TimePoint {})
        {
            next_fire_ = now + interval_;
            return false;
        }
        if (now < next_fire_)
            return false;
        next_fire_ += interval_;
        ++fire_count_;
        return true;
    }

    void reset() noexcept
    {
        next_fire_ = TimePoint {};
        fire_count_ = 0;
    }

    [[nodiscard]] std::uint64_t fire_count() const noexcept { return fire_count_; }
    [[nodiscard]] Duration interval() const noexcept { return interval_; }

private:
    IClock* clock_;
    Duration interval_;
    TimePoint next_fire_ {};
    std::uint64_t fire_count_ { 0 };
};

}  // namespace cd::time
