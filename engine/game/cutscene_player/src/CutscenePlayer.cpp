// =============================================================================
// CHROMODYNAMIC - cd/game/cutscene_player/CutscenePlayer.cpp
// Phase 610 - cd::game::cutscene_player (M7 W4B: data-driven cutscene timeline)
//
// Implementation notes:
//
// * play() resets all transient state before beginning. The active_cutscene_
//   is deep-copied from the caller's Cutscene so the player owns its data and
//   the caller can mutate or destroy theirs after play() returns.
//
// * tick() drives a while loop that calls advance_phase() which returns
//   leftover dt once the current phase ends. This loop handles dt values
//   that span multiple phase boundaries in a single tick (e.g. the final
//   phase of a cutscene is only 10 ms but dt is 16 ms). The loop terminates
//   when either:
//     a) the cutscene ends (all phases exhausted), or
//     b) the leftover is 0 (phase did not end this step).
//
// * fire_events_in_window() iterates events in the current phase once.
//   Events are NOT assumed to be pre-sorted -- we check each event's
//   offset_ms against the (prev, new] window so authoring order is
//   irrelevant. This is intentional: timeline authors should not need to
//   maintain sorted order (matches Unity Timeline and Unreal Sequencer
//   behaviour where events can be reordered in the inspector).
//   NOTE: If two events share the exact same offset_ms they both fire in
//   iteration order, matching the documented contract in the header.
//
// * stop() is a no-op if can_skip == false, matching the advisory-flag
//   contract. play() always stops regardless (internal forced stop used
//   for the play() path to avoid can_skip blocking a new cutscene).
//
// * events_fired_this_tick() returns a span over fired_this_tick_, which
//   is cleared at the top of every tick(). The span is valid until the
//   next tick() / play() / stop().
// =============================================================================
#include <cd/game/cutscene_player/CutscenePlayer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

namespace cd::game::cutscene_player
{

// =============================================================================
// Control
// =============================================================================

void CutscenePlayer::play(const Cutscene& cutscene)
{
    // Force-stop any active cutscene without honouring can_skip (play()
    // always wins -- the engine is starting a new scripted sequence).
    state_           = PlayerState::kIdle;
    complete_        = false;
    phase_index_     = 0;
    phase_offset_ms_ = 0.0F;
    fired_this_tick_.clear();

    active_cutscene_ = cutscene;  // deep copy

    // An empty-phase cutscene completes immediately without entering kPlaying.
    if (active_cutscene_.phases.empty())
    {
        complete_ = true;
        return;
    }

    // Clamp every phase duration to at least 1 ms so we never stall on
    // a zero-length phase boundary loop.
    for (auto& phase : active_cutscene_.phases)
    {
        if (phase.duration_ms < 1.0F)
        {
            phase.duration_ms = 1.0F;
        }
    }

    state_ = PlayerState::kPlaying;
}

void CutscenePlayer::pause() noexcept
{
    if (state_ == PlayerState::kPlaying)
    {
        state_ = PlayerState::kPaused;
    }
}

void CutscenePlayer::resume() noexcept
{
    if (state_ == PlayerState::kPaused)
    {
        state_ = PlayerState::kPlaying;
    }
}

void CutscenePlayer::stop() noexcept
{
    if (state_ == PlayerState::kIdle)
    {
        return;
    }
    // Honour can_skip: if the active cutscene disallows skipping, this call
    // is a no-op (the cutscene must play to completion or be superseded by
    // a new play() call from the engine).
    if (!active_cutscene_.can_skip)
    {
        return;
    }
    state_           = PlayerState::kIdle;
    complete_        = false;
    phase_index_     = 0;
    phase_offset_ms_ = 0.0F;
    fired_this_tick_.clear();
}

// =============================================================================
// Tick
// =============================================================================

void CutscenePlayer::tick(float dt_ms)
{
    if (state_ != PlayerState::kPlaying)
    {
        // If not playing (idle or paused) we still want fired_this_tick_ to
        // be empty so callers don't see stale events from a prior tick.
        fired_this_tick_.clear();
        return;
    }

    fired_this_tick_.clear();

    // Clamp negative dt to zero (caller error protection).
    if (dt_ms < 0.0F)
    {
        dt_ms = 0.0F;
    }

    // Drive a multi-phase consumption loop. advance_phase() returns the
    // leftover dt once the current phase ends; 0.0 means the phase is still
    // active (or exactly consumed its last ms).
    float remaining = dt_ms;
    while (remaining > 0.0F && state_ == PlayerState::kPlaying)
    {
        remaining = advance_phase(remaining);
    }
}

// -----------------------------------------------------------------------------
// advance_phase - consume up to `dt_ms` within the current phase.
// Returns the leftover (dt_ms - consumed) if the phase ended; 0.0 otherwise.
// Also handles the end-of-cutscene transition.
// -----------------------------------------------------------------------------
float CutscenePlayer::advance_phase(float dt_ms)
{
    const std::size_t n_phases = active_cutscene_.phases.size();
    if (phase_index_ >= n_phases)
    {
        // Should not be reachable while playing, but guard defensively.
        state_    = PlayerState::kIdle;
        complete_ = true;
        return 0.0F;
    }

    const float phase_duration = active_cutscene_.phases[phase_index_].duration_ms;
    const float prev_offset    = phase_offset_ms_;
    const float new_offset_raw = phase_offset_ms_ + dt_ms;

    if (new_offset_raw < phase_duration)
    {
        // Still inside the current phase.
        phase_offset_ms_ = new_offset_raw;
        fire_events_in_window(prev_offset, phase_offset_ms_);
        return 0.0F;
    }

    // Phase ends this step: fire events up to phase_duration, then advance.
    fire_events_in_window(prev_offset, phase_duration);

    const float leftover = new_offset_raw - phase_duration;

    ++phase_index_;
    phase_offset_ms_ = 0.0F;

    if (phase_index_ >= n_phases)
    {
        // All phases exhausted -- cutscene complete.
        state_    = PlayerState::kIdle;
        complete_ = true;
        return 0.0F;  // No further advance possible.
    }

    // Return leftover so the caller loop can continue into the next phase.
    return leftover;
}

// -----------------------------------------------------------------------------
// fire_events_in_window - append events whose offset_ms falls in (prev, next].
// Events at offset_ms == 0 fire when prev == 0 (start of phase) because the
// window is (prev, next]; we handle the very first tick specially below so
// offset == 0 events at phase start always fire.
//
// Special case: at the very beginning of a phase (prev_offset == 0) we also
// want events at offset_ms == 0.0 to fire -- the window is conventionally
// half-open (prev, next] which would exclude offset 0 when prev is also 0.
// We therefore use >= 0 for the lower bound when prev_offset == 0.
// -----------------------------------------------------------------------------
void CutscenePlayer::fire_events_in_window(float prev_offset, float new_offset)
{
    if (phase_index_ >= active_cutscene_.phases.size())
    {
        return;
    }

    const auto& phase = active_cutscene_.phases[phase_index_];
    for (const auto& ev : phase.events)
    {
        const float off = ev.offset_ms;
        // Include [0, new_offset] when prev_offset is 0 (phase just started).
        // Otherwise use the half-open (prev_offset, new_offset].
        const bool in_window = (prev_offset == 0.0F)
                                   ? (off >= 0.0F && off <= new_offset)
                                   : (off > prev_offset && off <= new_offset);
        if (in_window)
        {
            fired_this_tick_.push_back(ev);
        }
    }
}

// =============================================================================
// Queries
// =============================================================================

std::span<const CutsceneEvent> CutscenePlayer::events_fired_this_tick() const noexcept
{
    return std::span<const CutsceneEvent>{fired_this_tick_};
}

bool CutscenePlayer::is_playing() const noexcept
{
    return state_ == PlayerState::kPlaying;
}

bool CutscenePlayer::is_complete() const noexcept
{
    return complete_;
}

float CutscenePlayer::current_offset_ms() const noexcept
{
    return phase_offset_ms_;
}

std::size_t CutscenePlayer::current_phase_index() const noexcept
{
    return phase_index_;
}

}  // namespace cd::game::cutscene_player
