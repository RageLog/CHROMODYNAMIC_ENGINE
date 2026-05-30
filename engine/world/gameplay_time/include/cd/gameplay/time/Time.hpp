// =============================================================================
// CHROMODYNAMIC — cd/gameplay/time/Time.hpp
// Phase 459 — cd::gameplay::time::TimeKeeper
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
