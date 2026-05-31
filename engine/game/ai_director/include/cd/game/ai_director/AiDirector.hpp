// =============================================================================
// CHROMODYNAMIC — cd/game/ai_director/AiDirector.hpp
// Phase 620 — cd::game::ai_director (M8 W3B: high-level AI pacing controller)
//
// Left 4 Dead / Skyrim Radiant Quest-style AI Director: a high-level pacing
// controller that tracks player tension/intensity and schedules encounter
// spawns accordingly.
//
// Design references:
//   * Booth, M. "The AI Systems of Left 4 Dead." AIIDE 2009 / Valve Software.
//     The original "AI Director" paper — tension graph, population manager,
//     and relentless/peril/abrupt-intensity modes.
//   * Bethesda Game Studios. "Radiant Quest System." GDC 2012.
//     Procedural encounter scheduling driven by player progression.
//   * Yannakakis, G. N., & Togelius, J. "Artificial Intelligence and Games."
//     Springer, 2018. Chapter 5: dynamic difficulty adjustment.
//
// Tension model:
//   The Director maintains a floating-point `intensity` in [0, 1] that is
//   driven by player events (enemy kills, damage taken, objectives completed).
//   Intensity is subject to a natural decay each tick so quiet periods
//   automatically relax the state.  TensionState is a coarse classification
//   over intensity thresholds:
//
//     kIdle    [0.00, 0.25)  — calm; no active pressure.
//     kBuildUp [0.25, 0.60)  — mounting threat; Director primes encounters.
//     kPeak    [0.60, 0.85)  — high-pressure; Director fires encounters.
//     kRelief  [0.85, 1.00]  — post-peak cool-down; forced intensity decay.
//
//   kRelief is entered when intensity first reaches or exceeds the peak→relief
//   threshold (0.85).  In kRelief the Director applies an accelerated decay
//   regardless of incoming events until intensity falls back below kBuildUp,
//   then transitions to kIdle.  This mirrors L4D's "Director" post-peak rest.
//
// Encounter scheduling:
//   EncounterTemplate records a difficulty tier (0 = easiest) and an
//   intensity_contribution that is *added* to the director's intensity when
//   the encounter begins.  request_next_encounter() picks the highest-tier
//   template whose tier is ≤ floor(intensity * max_tier) — a simple linear
//   mapping that automatically scales challenge to current pacing.
//
// Threading:
//   NOT thread-safe.  Drive from the game-logic thread; results are read on
//   the same thread.
//
// Dependencies (CLAUDE.md §7): cd::core only.  No ECS, render, or audio
//   headers pulled in at the library boundary.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cd::game::ai_director
{

// -----------------------------------------------------------------------------
// TensionState — coarse classification over the continuous intensity curve.
//
// Transitions follow the one-directional ring:
//   kIdle → kBuildUp → kPeak → kRelief → kIdle
// (with kRelief → kIdle being an explicit forced-decay transition, not a
// threshold crossing.)
// -----------------------------------------------------------------------------
enum class TensionState : std::uint8_t
{
    kIdle    = 0,  ///< [0.00, 0.25) — calm.
    kBuildUp = 1,  ///< [0.25, 0.60) — mounting threat.
    kPeak    = 2,  ///< [0.60, 0.85) — high-pressure.
    kRelief  = 3,  ///< [0.85, 1.00] — forced cool-down after peak.
};

// -----------------------------------------------------------------------------
// EncounterTemplate — authored encounter recipe.
//
// Fields:
//   template_id          : stable authoring identifier ("goblin_raid",
//                          "boss_ambush", "patrol_group").
//   difficulty_tier      : non-negative tier index.  Higher = harder.
//                          Tier 0 is always available regardless of intensity.
//   intensity_contribution: added to the Director's intensity when this
//                           encounter starts.  Clamped to [0, 1] internally.
//   spawn_entity_ids     : ordered list of entity archetype ids to instantiate.
//                          The Director does NOT spawn entities directly —
//                          callers retrieve these ids and drive the spawn side
//                          (consistent with CLAUDE.md §7: no ECS dependency).
// -----------------------------------------------------------------------------
struct EncounterTemplate
{
    std::string              template_id           {};
    uint32_t                 difficulty_tier       {0};
    float                    intensity_contribution{0.0F};
    std::vector<std::string> spawn_entity_ids      {};
};

// -----------------------------------------------------------------------------
// DirectorState — observable snapshot of the Director's internal state.
//
// Immutable from the caller's perspective; returned by value from state().
// -----------------------------------------------------------------------------
struct DirectorState
{
    TensionState tension        {TensionState::kIdle};
    float        intensity      {0.0F};   ///< [0, 1]
    float        time_in_state_ms{0.0F};  ///< ms spent in current TensionState
    uint32_t     player_score   {0};      ///< cumulative score from notify_player_event
};

// -----------------------------------------------------------------------------
// AiDirector — high-level AI pacing controller.
//
// Lifecycle:
//   1. configure_templates(span) — load authored encounter recipes.
//   2. notify_player_event(id, delta) — inform Director of gameplay moments
//      ("enemy_killed", "damage_taken", "objective_complete").
//   3. tick(dt_ms) — advance time, decay intensity, update TensionState.
//   4. state() — poll the current DirectorState snapshot.
//   5. request_next_encounter() — ask for the next appropriate encounter.
//
// Intensity decay:
//   Each tick reduces intensity by kDecayRatePerMs * dt_ms, floored at 0.
//   In kRelief an additional kReliefDecayBonus is applied so the Director
//   exits the cool-down window deterministically.
//
// Thresholds (all constexpr, exposed for tests):
//   kThresholdBuildUp = 0.25F
//   kThresholdPeak    = 0.60F
//   kThresholdRelief  = 0.85F
// -----------------------------------------------------------------------------
class AiDirector
{
public:
    // --- Tuneable constants (public for test inspection) ----------------------
    static constexpr float kThresholdBuildUp  = 0.25F;
    static constexpr float kThresholdPeak     = 0.60F;
    static constexpr float kThresholdRelief   = 0.85F;

    /// Natural intensity decay per millisecond (applies every tick).
    static constexpr float kDecayRatePerMs    = 0.0001F;

    /// Additional decay per ms while in kRelief (accelerated cool-down).
    static constexpr float kReliefDecayBonus  = 0.0003F;

    /// Score awarded per unit of positive intensity_delta in player events.
    static constexpr uint32_t kScorePerUnit   = 10U;

    // -------------------------------------------------------------------------
    AiDirector() = default;

    AiDirector(const AiDirector&)            = delete;
    AiDirector& operator=(const AiDirector&) = delete;
    AiDirector(AiDirector&&)                 = default;
    AiDirector& operator=(AiDirector&&)      = default;

    ~AiDirector() = default;

    // ---- configuration -------------------------------------------------------

    /// Replace the full encounter template library.
    /// Templates are copied internally so the caller's span may be temporary.
    /// Calling configure_templates() resets the internal working set; it does
    /// NOT reset DirectorState — intensity / tension / score are preserved.
    void configure_templates(std::span<const EncounterTemplate> templates);

    // ---- per-event notification ----------------------------------------------

    /// Notify the Director of a gameplay event.
    ///
    /// `event_id`       : human-readable label ("enemy_killed", "damage_taken").
    ///                    The Director does not interpret the string — it is
    ///                    purely advisory and available for external logging.
    /// `intensity_delta`: signed change to apply to intensity *before* clamping
    ///                    to [0, 1].  Positive = more pressure; negative =
    ///                    relief event.  Positive deltas accumulate player_score
    ///                    (kScorePerUnit per unit delta, rounded down).
    void notify_player_event(std::string_view event_id, float intensity_delta);

    // ---- per-frame advance ---------------------------------------------------

    /// Advance the Director by `dt_ms` milliseconds.
    /// Applies intensity decay, updates time_in_state_ms, and re-evaluates
    /// TensionState transitions.
    void tick(float dt_ms);

    // ---- queries -------------------------------------------------------------

    /// Snapshot of the current Director state.
    [[nodiscard]] DirectorState state() const noexcept;

    /// Pick the most appropriate encounter for the current tension/intensity.
    ///
    /// Selection policy:
    ///   max_tier = floor(intensity * highest_configured_tier)
    ///   Returns the template with the highest difficulty_tier that is
    ///   ≤ max_tier, preferring the first match in insertion order on ties.
    ///   Returns std::nullopt if no templates are configured or if intensity
    ///   is 0 (no pressure — don't force an encounter).
    [[nodiscard]] std::optional<EncounterTemplate> request_next_encounter() const;

private:
    // Recompute TensionState from the current intensity_ value.
    // Updates state_.tension and resets time_in_state_ms_ when the state
    // changes.
    void update_tension_state();

    // ---- state ---------------------------------------------------------------
    TensionState tension_         {TensionState::kIdle};
    float        intensity_       {0.0F};
    float        time_in_state_ms_{0.0F};
    uint32_t     player_score_    {0};

    // ---- template library ----------------------------------------------------
    std::vector<EncounterTemplate> templates_ {};
};

}  // namespace cd::game::ai_director
