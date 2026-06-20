// =============================================================================
// CHROMODYNAMIC — cd/gameplay/time/Time.hpp
// Phase 459 — cd::gameplay::time::TimeKeeper
// Phase 1253 — FixedStepAccumulator (sub-step, catch-up, spiral-of-death clamp)
//
// Authoritative game-time abstraction. Production engines (Unreal AWorldSettings,
// Unity Time, Bevy bevy_time) all converge on three orthogonal axes:
//   1. real (wall-clock) delta seconds — the actual elapsed time
//   2. scaled (gameplay) delta seconds — real * time_scale, optionally paused
//   3. monotonic frame_index — never affected by pause / scale
//
// This library exposes a `TimeKeeper` driven by `tick(real_dt_seconds)` from
// the engine main loop, plus orthogonal `TimerCategory` channels so that UI,
// gameplay, and fixed-simulation timelines can advance independently (e.g. a
// pause menu still animates UI while the gameplay simulation is frozen).
//
// `FixedStepAccumulator` (also in this header) solves the classic sub-step
// remainder problem for physics / deterministic simulation:
//   - accumulate(scaled_dt) returns how many fixed steps to execute.
//   - residual() gives the fractional remainder in [0, step_seconds).
//   - max_steps clamp prevents "spiral of death" when the frame time spikes
//     (e.g. debugger pause, swap-chain stall) from snowballing catch-up work.
//
// Design notes:
//   - Pause stops the gameplay `elapsed_seconds` clock advancing, but
//     `delta_seconds` of the returned `GameTime` still reports the *real* dt
//     so UI animations and post-process flicker continue to update.
//   - `frame_index` increments on every `tick()`, regardless of pause / scale.
//   - Negative real_dt is rejected defensively (no time travel).
//   - Single-channel API (`get()`) returns the `kGameplay` channel by default.
//   - Sub-system channels are independent `TimeKeeper`-equivalent state slots
//     held inside the same object; each tracks its own elapsed time but they
//     all advance on the same `tick()` call.
//
// Dependencies: cd::core only (no math, no concurrency).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace cd::gameplay::time
{

// -----------------------------------------------------------------------------
// GameTime — snapshot returned by TimeKeeper::get().
// -----------------------------------------------------------------------------
struct GameTime
{
    /// Accumulated *scaled & pause-aware* time since TimeKeeper construction.
    /// While paused this value is frozen; otherwise it advances by
    /// `real_dt * time_scale` per tick.
    double elapsed_seconds { 0.0 };

    /// Real delta seconds of the most recent tick (unaffected by pause).
    /// UI animations should drive off this so they keep ticking in menus.
    double delta_seconds { 0.0 };

    /// Monotonic counter incremented once per `tick()` call. Useful for
    /// "every Nth frame" gates and replay determinism.
    std::uint64_t frame_index { 0 };

    /// Current scale applied to gameplay time. 1.0 = real time, 0.5 = slow-mo,
    /// 2.0 = fast-forward. Independent of pause.
    double time_scale { 1.0 };
};

// -----------------------------------------------------------------------------
// TimerCategory — orthogonal time channels held inside one TimeKeeper.
// Each channel maintains its own `elapsed_seconds` so pausing gameplay does
// not freeze UI animations, and a fixed-step simulation can advance at a
// different rate from the variable-step gameplay channel.
// -----------------------------------------------------------------------------
enum class TimerCategory : std::uint8_t
{
    kGameplay   = 0,  ///< Default channel — affected by pause and time_scale.
    kUi         = 1,  ///< UI / menu channel — ignores gameplay pause/scale.
    kSimulation = 2,  ///< Fixed-step simulation channel — independent scale.

    kCount      = 3   ///< Sentinel — must remain last.
};

// -----------------------------------------------------------------------------
// FixedStepAccumulator — remainder-carrying fixed-step driver.
//
// Usage pattern (physics / determinism):
//   FixedStepAccumulator accum { /*step_seconds=*/ 1.0 / 60.0 };
//
//   while (running) {
//     const double scaled_dt = keeper.get().delta_seconds
//                              * keeper.time_scale();  // already scaled
//     const int steps = accum.accumulate(scaled_dt);
//     for (int i = 0; i < steps; ++i) {
//       physics.step(accum.step_seconds());   // fixed-step kernel
//     }
//     const double alpha = accum.residual() / accum.step_seconds(); // interpolation
//     render_interpolated(alpha);
//   }
//
// Spiral-of-death: when frame time > max_steps * step_seconds the accumulator
// drops the excess rather than queuing unlimited catch-up work. The simulation
// "runs slow" but stays interactive.
// -----------------------------------------------------------------------------
class FixedStepAccumulator
{
public:
    /// Construct with a fixed step size and an optional maximum-steps-per-frame
    /// cap (default 8 — reasonable for 60 Hz physics at up to ~7 Hz frame rate
    /// before spiral-of-death protection kicks in).
    /// `step_seconds` must be > 0; values ≤ 0 are clamped to a 60 Hz default.
    explicit FixedStepAccumulator(double step_seconds,
                                  int    max_steps = 8) noexcept
        : step_seconds_ { step_seconds > 0.0 ? step_seconds : (1.0 / 60.0) }
        , max_steps_ { max_steps > 0 ? max_steps : 1 }
    {}

    /// Feed `scaled_dt` seconds (already multiplied by TimeKeeper's time_scale)
    /// into the accumulator. Returns how many full fixed steps are ready to run
    /// this frame, clamped to `max_steps` to prevent spiral-of-death.
    /// Negative or zero dt produces 0 steps (no harm, residual unchanged).
    [[nodiscard]] int accumulate(double scaled_dt) noexcept
    {
        if (scaled_dt <= 0.0)
        {
            return 0;
        }
        // Clamp excess before dividing to avoid unbounded accumulation.
        const double clamped = std::min(scaled_dt,
                                        step_seconds_ * static_cast<double>(max_steps_));
        residual_ += clamped;

        const int steps = static_cast<int>(residual_ / step_seconds_);
        residual_ -= static_cast<double>(steps) * step_seconds_;
        // Guard against fp rounding pushing residual slightly negative.
        residual_ = std::max(residual_, 0.0);
        return steps;
    }

    /// Fractional remainder in [0, step_seconds). Use for render interpolation:
    ///   alpha = residual() / step_seconds()  →  [0, 1)
    [[nodiscard]] double residual() const noexcept { return residual_; }

    /// The configured fixed step size in seconds.
    [[nodiscard]] double step_seconds() const noexcept { return step_seconds_; }

    /// Maximum fixed steps executed per `accumulate()` call.
    [[nodiscard]] int max_steps() const noexcept { return max_steps_; }

    /// Reset residual to zero. Does not change step_seconds or max_steps.
    void reset() noexcept { residual_ = 0.0; }

private:
    double step_seconds_;
    double residual_ { 0.0 };
    int    max_steps_;
};

// -----------------------------------------------------------------------------
// TimeKeeper — engine-side authoritative clock.
//
// Usage:
//   cd::gameplay::time::TimeKeeper clock;
//   while (running) {
//     const double real_dt = compute_wall_clock_dt();
//     clock.tick(real_dt);
//     const auto t = clock.get();                 // gameplay channel
//     const auto ui = clock.get(TimerCategory::kUi);
//     ...
//   }
// -----------------------------------------------------------------------------
class TimeKeeper
{
public:
    TimeKeeper() noexcept = default;

    /// Advance every channel by `real_dt_seconds`.
    /// Negative values are rejected (early-return, no state mutation, no
    /// frame_index bump) — passing negative dt is a programmer error.
    /// Returns true on accepted tick, false on rejected (negative) dt.
    bool tick(double real_dt_seconds) noexcept;

    /// Freeze the gameplay channel's `elapsed_seconds`. UI and simulation
    /// channels are unaffected by gameplay pause; they have their own
    /// `set_paused(category, bool)` toggle.
    void pause() noexcept { set_paused(TimerCategory::kGameplay, true); }

    /// Resume the gameplay channel.
    void resume() noexcept { set_paused(TimerCategory::kGameplay, false); }

    /// Per-channel pause control.
    void set_paused(TimerCategory category, bool paused) noexcept;

    /// Query pause state of a specific channel.
    [[nodiscard]] bool is_paused(TimerCategory category = TimerCategory::kGameplay) const noexcept;

    /// Scale the gameplay channel's per-tick advance. 1.0 = real time.
    /// Negative scales are clamped to 0 (running time backwards desyncs
    /// every downstream system — particle ages, animation cursors, ECS).
    void set_time_scale(double scale) noexcept { set_time_scale(TimerCategory::kGameplay, scale); }

    /// Per-channel scale control.
    void set_time_scale(TimerCategory category, double scale) noexcept;

    [[nodiscard]] double time_scale(TimerCategory category = TimerCategory::kGameplay) const noexcept;

    /// Returns the pause-aware, scale-applied delta for the last tick on the
    /// given channel. While paused, returns 0.0. This is the value callers
    /// should feed to FixedStepAccumulator::accumulate().
    [[nodiscard]] double scaled_delta(TimerCategory category = TimerCategory::kGameplay) const noexcept;

    /// Snapshot the requested channel. Default = kGameplay.
    [[nodiscard]] GameTime get(TimerCategory category = TimerCategory::kGameplay) const noexcept;

    /// Monotonic frame counter — shared across all channels.
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }

    /// Reset the keeper back to construction state (all channels, all flags).
    void reset() noexcept;

private:
    struct Channel
    {
        double elapsed_seconds { 0.0 };
        double delta_seconds { 0.0 };
        double time_scale { 1.0 };
        bool   paused { false };
    };

    static constexpr std::size_t kChannelCount = static_cast<std::size_t>(TimerCategory::kCount);

    [[nodiscard]] static std::size_t index_of(TimerCategory category) noexcept
    {
        return static_cast<std::size_t>(category);
    }

    std::array<Channel, kChannelCount> channels_ {};
    std::uint64_t                      frame_index_ { 0 };
};

}  // namespace cd::gameplay::time
