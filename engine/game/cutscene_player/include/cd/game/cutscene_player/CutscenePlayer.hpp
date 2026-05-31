// =============================================================================
// CHROMODYNAMIC - cd/game/cutscene_player/CutscenePlayer.hpp
// Phase 610 - cd::game::cutscene_player (M7 W4B: data-driven cutscene timeline)
//
// A lightweight, data-driven cutscene timeline player. The model is inspired
// by narrative timeline tooling in Unity's Cinemachine/Timeline, Unreal's
// Sequencer, and Godot's AnimationPlayer, while remaining fully decoupled from
// any rendering or audio backend:
//
//   * A Cutscene is a sequence of CutscenePhase entries, each with a
//     duration_ms and an ordered list of CutsceneEvent triggers.
//   * A CutsceneEvent fires when the cumulative playhead crosses
//     (phase_start + event.offset_ms) from below -- i.e. exactly once per
//     play-through when offset_ms falls within [0, phase.duration_ms].
//   * CutscenePlayer is a state machine over {kIdle, kPlaying, kPaused}
//     driven by tick(dt_ms). Fired events are collected into a per-tick
//     buffer; callers drain it via events_fired_this_tick().
//   * can_skip: when false, stop() from external code is a no-op (skip
//     attempt fails silently). The field is advisory; the engine decides
//     whether to expose a skip UI at all.
//
// Threading: NOT thread-safe. Mutate from the owning thread.
//
// Dependencies (CLAUDE.md §7): cd::core only at the public header level.
// No allocator, no math, no I/O.
//
// Design references:
//   * Unity Technologies. "Cinemachine / Timeline" documentation, 2023.
//     https://docs.unity3d.com/Packages/com.unity.timeline@1.8/manual/
//   * Epic Games. "Sequencer Overview", Unreal Engine 5 docs, 2024.
//     https://docs.unrealengine.com/5.0/en-US/sequencer-cinematic-editor-unreal-engine/
//   * GDC / Valve. "The Cabal: Valve's Design Process for Creating Half-Life."
//     GDC 1999 -- phased scripted-sequence model.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cd::game::cutscene_player
{

// -----------------------------------------------------------------------------
// EventKind - discriminant for CutsceneEvent. Each enumerator maps 1:1 to a
// well-known engine signal; the receiving system interprets string_arg /
// vec3_arg as it sees fit (no strong typing inside this library -- by design,
// to avoid pulling in ECS / audio / render headers).
// -----------------------------------------------------------------------------
enum class EventKind : std::uint8_t
{
    kCameraMove    = 0,  ///< Move camera; vec3_arg = world target; string_arg = easing id
    kCharacterTalk = 1,  ///< Start dialogue; string_arg = line id or script key
    kPlaySound     = 2,  ///< Play one-shot; string_arg = asset path; vec3_arg = world pos
    kFadeIn        = 3,  ///< Screen fade in; vec3_arg[0] = duration_ms override (0 = default)
    kFadeOut       = 4,  ///< Screen fade out; vec3_arg[0] = duration_ms override
    kSpawnEntity   = 5,  ///< Spawn; string_arg = archetype; vec3_arg = position
    kDespawnEntity = 6,  ///< Despawn; string_arg = entity tag / unique id
    kSetFlag       = 7,  ///< Set a gameplay flag; string_arg = flag name; vec3_arg[0] = value
};

// -----------------------------------------------------------------------------
// CutsceneEvent - one timed signal within a CutscenePhase.
//
// Fields:
//   * offset_ms   : time from the start of the containing phase at which this
//                   event fires. Clamped to [0, phase.duration_ms) by the
//                   player at play() time. If two events share the same
//                   offset_ms they fire in the order they appear in the vector.
//   * kind        : discriminant (see EventKind).
//   * string_arg  : free-form string payload (asset path, dialogue id, flag
//                   name, archetype tag, ...).
//   * vec3_arg    : three-float payload (world position, colour, scale, ...).
//                   Unused components should be zero-initialised.
// -----------------------------------------------------------------------------
struct CutsceneEvent
{
    float                  offset_ms  {0.0F};
    EventKind              kind       {EventKind::kCameraMove};
    std::string            string_arg {};
    std::array<float, 3>   vec3_arg   {0.0F, 0.0F, 0.0F};
};

// -----------------------------------------------------------------------------
// CutscenePhase - one contiguous segment of a cutscene.
//
// Fields:
//   * phase_id    : stable authoring id, unique within the parent Cutscene
//                   ("intro", "boss_arrives", "outro"). Empty id is valid
//                   (anonymous phase).
//   * duration_ms : length of this phase in milliseconds. Must be > 0; the
//                   player clamps it to 1 ms at play() to avoid a zero-length
//                   phase stall.
//   * events      : timed events within this phase. Events whose offset_ms
//                   falls outside [0, duration_ms) are silently skipped.
// -----------------------------------------------------------------------------
struct CutscenePhase
{
    std::string               phase_id    {};
    float                     duration_ms {1000.0F};
    std::vector<CutsceneEvent> events      {};
};

// -----------------------------------------------------------------------------
// Cutscene - the full authored timeline asset.
//
// Fields:
//   * cutscene_id : stable authoring id ("opening_cinematic", "level2_outro").
//   * phases      : ordered sequence of phases. An empty phase list is valid
//                   and results in an immediately-complete cutscene.
//   * can_skip    : advisory flag. When false, CutscenePlayer::stop() is a
//                   no-op (see stop() contract below).
// -----------------------------------------------------------------------------
struct Cutscene
{
    std::string                  cutscene_id {};
    std::vector<CutscenePhase>   phases      {};
    bool                         can_skip    {true};
};

// -----------------------------------------------------------------------------
// PlayerState - observable lifecycle tag exposed via is_playing / is_complete.
// Internal to the player; exposed in the header for testing convenience.
// -----------------------------------------------------------------------------
enum class PlayerState : std::uint8_t
{
    kIdle    = 0,  ///< No cutscene loaded or fully stopped.
    kPlaying = 1,  ///< Actively advancing via tick().
    kPaused  = 2,  ///< Frozen; tick() is a no-op. resume() resumes.
};

// -----------------------------------------------------------------------------
// CutscenePlayer - the runtime state machine.
//
// Lifecycle:
//   1. play(cutscene) -- load and immediately begin playing. Any currently
//      active cutscene is stopped first (regardless of can_skip).
//   2. tick(dt_ms) -- advance the playhead by dt_ms. Fires all events whose
//      offset falls within the consumed time window. At phase boundary the
//      player transitions to the next phase automatically. When the last phase
//      ends the player transitions to kIdle with is_complete() == true.
//   3. pause() / resume() -- freeze / unfreeze. Idempotent.
//   4. stop() -- terminate immediately. Respects can_skip: if the active
//      cutscene has can_skip == false this call is a no-op.
//
// Per-tick event buffer:
//   events_fired_this_tick() returns a span that is valid until the NEXT call
//   to tick(), play(), or stop(). Callers should drain it immediately.
//
// State queries (all noexcept):
//   is_playing()       -- true iff state == kPlaying.
//   is_complete()      -- true iff the most recent cutscene played to the end
//                         without being stop()ped. Reset by play().
//   current_offset_ms() -- playhead offset from the START of the current phase.
// -----------------------------------------------------------------------------
class CutscenePlayer
{
public:
    CutscenePlayer()  = default;
    ~CutscenePlayer() = default;

    CutscenePlayer(const CutscenePlayer&)            = delete;
    CutscenePlayer& operator=(const CutscenePlayer&) = delete;
    CutscenePlayer(CutscenePlayer&&)                 = default;
    CutscenePlayer& operator=(CutscenePlayer&&)      = default;

    // ---- control ------------------------------------------------------------

    /// Load `cutscene` and begin playing from phase 0. Any currently running
    /// cutscene is stopped without can_skip enforcement. Clears is_complete().
    void play(const Cutscene& cutscene);

    /// Freeze the playhead. Idempotent; no-op if not playing.
    void pause() noexcept;

    /// Unfreeze the playhead. Idempotent; no-op if not paused.
    void resume() noexcept;

    /// Stop and reset to kIdle. No-op if can_skip == false on the active
    /// cutscene. Always a no-op if already kIdle.
    void stop() noexcept;

    // ---- per-frame tick ------------------------------------------------------

    /// Advance the playhead by `dt_ms` milliseconds. No-op if not kPlaying.
    /// Fires all CutsceneEvents whose offset_ms is crossed during this step,
    /// in ascending offset order within each phase. When the final phase ends
    /// transitions to kIdle with is_complete() == true.
    void tick(float dt_ms);

    // ---- queries -------------------------------------------------------------

    /// Returns events fired during the most recent tick() call. Valid until
    /// the next call to tick(), play(), or stop().
    CD_NODISCARD std::span<const CutsceneEvent> events_fired_this_tick() const noexcept;

    /// True while the player is in kPlaying state.
    CD_NODISCARD bool is_playing() const noexcept;

    /// True if the most recent cutscene reached its natural end (not stopped).
    CD_NODISCARD bool is_complete() const noexcept;

    /// Playhead offset in ms from the START of the current phase.
    CD_NODISCARD float current_offset_ms() const noexcept;

    /// Current phase index (0-based). Returns 0 when kIdle.
    CD_NODISCARD std::size_t current_phase_index() const noexcept;

private:
    // Advance within the current phase by `dt_ms`, firing events as they
    // cross. Returns the leftover dt after the phase ends (0 if the phase
    // didn't end this step).
    float advance_phase(float dt_ms);

    // Fire all events in the current phase whose offset_ms falls within
    // (prev_offset, new_offset].
    void fire_events_in_window(float prev_offset, float new_offset);

    // ---- state ---------------------------------------------------------------
    Cutscene                   active_cutscene_    {};
    PlayerState                state_              {PlayerState::kIdle};
    bool                       complete_           {false};
    std::size_t                phase_index_        {0};
    float                      phase_offset_ms_    {0.0F};  ///< offset within current phase

    // ---- per-tick fired buffer -----------------------------------------------
    // Cleared at the start of every tick() and filled by fire_events_in_window.
    std::vector<CutsceneEvent> fired_this_tick_    {};
};

}  // namespace cd::game::cutscene_player
