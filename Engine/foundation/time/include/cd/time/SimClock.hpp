// =============================================================================
// CHROMODYNAMIC — cd/time/SimClock.hpp
// ADR-005 §F + ADR-017 P2 (DfH common/timing/simclock.hpp 396-line salvage,
// distilled & singleton-free)
//
// Simulation clock for engine time:
//   - pause / resume
//   - scale 0.1x .. 10x (slow-mo / fast-forward) plus arbitrary multiplier
//   - fixed-timestep tick() — advances simulation time by a known delta
//   - manual step() — advances by exactly one fixed step regardless of pause
//   - deterministic reproducibility when seeded and ticked the same way
//
// Use cases: gameplay tick, animation playback, bullet-time effects, frame-step
// debugging, replay/lockstep simulation.
//
// Thread-safe? In v1: single-thread (caller-synchronised). Atomic state
// upgrade lands when the job system stabilises (Sprint S2.1.x).
// =============================================================================
#pragma once

#include <cd/time/IClock.hpp>

#include <algorithm>

namespace cd::time
{

class SimClock final : public IClock
{
public:
    static constexpr double kMinScale = 0.0;  // 0 == effectively paused
    static constexpr double kMaxScale = 10.0;
    static constexpr Duration kDefaultStep =
        std::chrono::duration_cast<Duration>(std::chrono::microseconds { 16'667 });  // 60 Hz

    explicit SimClock(Duration fixed_step = kDefaultStep) noexcept
        : fixed_step_ { fixed_step }
    {
    }

    // --- IClock interface ----------------------------------------------------

    [[nodiscard]] TimePoint now() const noexcept override
    {
        return TimePoint {} + accumulated_;
    }

    [[nodiscard]] TimeMode mode() const noexcept override
    {
        return TimeMode::Simulation;
    }

    // --- Simulation control --------------------------------------------------

    void pause() noexcept
    {
        paused_ = true;
    }

    void resume() noexcept
    {
        paused_ = false;
    }

    [[nodiscard]] bool is_paused() const noexcept
    {
        return paused_;
    }

    void set_scale(double scale) noexcept
    {
        scale_ = std::clamp(scale, kMinScale, kMaxScale);
    }

    [[nodiscard]] double scale() const noexcept
    {
        return scale_;
    }

    void set_fixed_step(Duration step) noexcept
    {
        if (step.count() > 0)
        {
            fixed_step_ = step;
        }
    }

    [[nodiscard]] Duration fixed_step() const noexcept
    {
        return fixed_step_;
    }

    // --- Advancement ---------------------------------------------------------

    /// Advance simulation time by the real-time delta `dt`, applying pause/scale.
    /// Returns the simulation duration actually applied.
    Duration tick(Duration dt) noexcept
    {
        if (paused_)
        {
            return Duration::zero();
        }
        const auto scaled = std::chrono::duration_cast<Duration>(std::chrono::duration<double> { dt } * scale_);
        accumulated_ += scaled;
        ++tick_count_;
        return scaled;
    }

    /// Advance by exactly one fixed step (frame-step debug). Bypasses pause but
    /// respects scale (typical use: scale=1.0 when stepping).
    Duration step() noexcept
    {
        const auto scaled =
            std::chrono::duration_cast<Duration>(std::chrono::duration<double> { fixed_step_ } * scale_);
        accumulated_ += scaled;
        ++tick_count_;
        return scaled;
    }

    /// Reset to zero simulation time. Does not change scale/pause.
    void reset() noexcept
    {
        accumulated_ = Duration::zero();
        tick_count_ = 0;
    }

    [[nodiscard]] Duration elapsed() const noexcept
    {
        return accumulated_;
    }

    [[nodiscard]] std::uint64_t tick_count() const noexcept
    {
        return tick_count_;
    }

private:
    Duration fixed_step_;
    Duration accumulated_ { Duration::zero() };
    double scale_ { 1.0 };
    std::uint64_t tick_count_ { 0 };
    bool paused_ { false };
};

}  // namespace cd::time
