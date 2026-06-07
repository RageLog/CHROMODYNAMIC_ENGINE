// =============================================================================
// CHROMODYNAMIC — cd/ai/squad/AiSquad.hpp
// Phase 682 — cd::ai::squad (M13 W5A: multi-agent squad coordination)
//
// Left 4 Dead Special Infected / Halo Flood-style squad coordination layer.
// Manages N AI agents as a logical squad: role assignment, formation geometry,
// and a shared blackboard for inter-agent communication.
//
// Design references:
//   * Booth, M. "The AI Systems of Left 4 Dead." AIIDE 2009 / Valve Software.
//     Squad-level "Special Infected" coordination — roles, flanking, suppression.
//   * Isla, D. "Handling Complexity in the Halo 2 AI." GDC 2005.
//     Hierarchical behavior with shared blackboard for coordinated group action.
//   * van der Sterren, W. "Squad Tactics for Video Games." AI Game Programming
//     Wisdom 3, Charles River Media, 2006, Ch. 10.
//     Formation steering + role-based tactical assignment for small groups.
//
// Sprint-1 scope:
//   Role assignment (SquadRole enum), formation geometry (centroid + radius),
//   shared blackboard (target position + threat level + generic fact map), and
//   a Squad class with add/remove/update/tick/query API.
//
//   Squad::tick() computes:
//     - Formation centroid from current member positions.
//     - Formation radius as max distance from centroid across all members.
//     - Blackboard threat_level decays toward 0 each tick (half-life damping).
//     - Pointman advances toward target; flankers increase lateral spread;
//       suppressor and coverer maintain position (behavior hooks for Sprint-2).
//
//   Sprint-2 plan: steering integration (inject MoveCommandFn per member),
//   communication graph (who can "see" whom), and cover-point reservation.
//
// Namespace: cd::ai::squad
//
// Dependencies (CLAUDE.md §7): cd::core only — stdlib, no ECS/render/audio.
// The Squad never spawns or moves entities directly; callers read formation()
// and member positions, then drive their own entity systems.
//
// Thread safety: NOT thread-safe. Drive from the game-logic thread.
//
// Moment: A combat designer creates a 4-agent enemy squad, sees them advance
// in formation with role-specific behaviors (pointman scouts, flankers go wide,
// suppressor lays cover) without writing each agent's logic from scratch.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::ai::squad
{

// =============================================================================
// SquadRole — tactical role assigned to a squad member.
//
// Roles influence tick() behavior:
//   kPointman   — lead agent; advances closest to the target.
//   kFlanker    — lateral displacement; attempts to envelop the target.
//   kSuppressor — fires to suppress; holds position near formation centroid.
//   kCoverer    — rear guard; trails the formation to protect retreat.
// =============================================================================
enum class SquadRole : std::uint8_t
{
    kPointman   = 0,  ///< Lead scout; advances closest to target.
    kFlanker    = 1,  ///< Enveloper; moves to the target's side.
    kSuppressor = 2,  ///< Cover fire; holds ground near centroid.
    kCoverer    = 3,  ///< Rear guard; trails the formation.
};

// =============================================================================
// SquadMember — per-agent record inside a Squad.
//
// `entity_id` is an opaque 64-bit handle (ECS entity, actor ID, etc.).
// The Squad does NOT dereference or otherwise interpret this value.
//
// `position` is the current world-space position reported by the caller via
// update_position(). The Squad does not move agents directly.
//
// `health` is advisory: [0, 1] normalised HP. The Squad uses it to demote
// a kPointman to kCoverer if health drops below a threshold (Sprint-2 hook).
// =============================================================================
struct SquadMember
{
    uint64_t              entity_id {};
    SquadRole             role      {SquadRole::kPointman};
    std::array<float, 3>  position  {};
    float                 health    {1.0F};  ///< [0, 1] normalised hit-points.
};

// =============================================================================
// Formation — observable formation snapshot.
//
// Updated every tick() from current member positions.
//
//   centroid     — arithmetic mean of all member positions.
//   radius       — max distance from centroid to any member.
//   member_count — cached from Squad::members().size() for quick access.
// =============================================================================
struct Formation
{
    std::array<float, 3> centroid     {};
    float                radius       {0.0F};
    uint8_t              member_count {0};
};

// =============================================================================
// Blackboard — shared knowledge store accessible by all squad members.
//
//   target_entity_id — entity the squad is currently engaging (0 = none).
//   target_position  — last known world-space position of the target.
//   threat_level     — [0, 1] perceived danger; decays toward 0 per tick.
//   shared_facts     — generic key→float store for designer-authored facts
//                      (e.g. "alarm_triggered", "suppression_rounds_remaining").
// =============================================================================
struct Blackboard
{
    uint64_t                               target_entity_id {0};
    std::array<float, 3>                   target_position  {};
    float                                  threat_level     {0.0F};  ///< [0, 1]
    std::unordered_map<std::string, float> shared_facts     {};
};

// =============================================================================
// Squad — multi-agent squad coordinator.
//
// Lifecycle:
//   1. add_member(entity_id, role)      — enlist an agent.
//   2. update_position(entity_id, pos)  — feed each agent's world position.
//   3. tick(dt)                         — advance formation + blackboard logic.
//   4. formation()                      — query current formation snapshot.
//   5. blackboard()                     — query shared knowledge.
//   6. members()                        — iterate over SquadMember records.
//
// Tick behaviour (Sprint-1):
//   a. Recompute formation centroid and radius from member positions.
//   b. Decay blackboard.threat_level: t' = t * exp(-kThreatDecayRate * dt).
//   c. Role-specific position hints (written to role_hint_ internally;
//      Sprint-2 will expose these as MoveCommand targets):
//        - kPointman   : moves toward target_position (lerp step).
//        - kFlanker    : offsets 90° from centroid→target axis.
//        - kSuppressor : stays at centroid.
//        - kCoverer    : trails formation away from target.
//   (Sprint-1 tick does NOT modify member.position — it only updates
//   Formation and Blackboard. Caller must read these and drive entity
//   movement in their ECS/physics layer.)
//
// Capacity: up to 255 members (Formation.member_count is uint8_t).
//
// Thread safety: NOT thread-safe. Drive from the game-logic thread.
// =============================================================================
class Squad
{
public:
    /// Natural threat-level decay coefficient (per second, natural-log units).
    /// At this rate: 1.0 -> ~0.37 after 1 s, ~0.05 after 3 s.
    static constexpr float kThreatDecayRate = 1.0F;

    Squad() = default;

    Squad(const Squad&)            = delete;
    Squad& operator=(const Squad&) = delete;
    Squad(Squad&&) noexcept            = default;
    Squad& operator=(Squad&&) noexcept = default;

    ~Squad() = default;

    // -------------------------------------------------------------------------
    // Membership management
    // -------------------------------------------------------------------------

    /// Enlist a new agent with the given role.
    /// No-op if `entity_id` is already a member.
    /// Capacity limit: 255 members (uint8_t formation count).
    void add_member(uint64_t entity_id, SquadRole role);

    /// Remove a member by entity_id.
    /// No-op if `entity_id` is not a member.
    void remove_member(uint64_t entity_id);

    // -------------------------------------------------------------------------
    // Per-frame state feed
    // -------------------------------------------------------------------------

    /// Update the world-space position of a member.
    /// No-op if `entity_id` is not a member.
    void update_position(uint64_t entity_id, std::array<float, 3> pos);

    /// Update the normalised health [0, 1] of a member.
    /// No-op if `entity_id` is not a member.
    void update_health(uint64_t entity_id, float health);

    // -------------------------------------------------------------------------
    // Blackboard writes (caller-driven)
    // -------------------------------------------------------------------------

    /// Set the current target for the squad.
    void set_target(uint64_t target_entity_id, std::array<float, 3> target_position);

    /// Increase threat_level by `delta`, clamped to [0, 1].
    void raise_threat(float delta);

    /// Write a named fact into the shared blackboard.
    void set_fact(const std::string& key, float value);

    // -------------------------------------------------------------------------
    // Advance
    // -------------------------------------------------------------------------

    /// Advance squad logic by `dt` seconds.
    ///   - Recomputes Formation (centroid + radius + member_count).
    ///   - Decays blackboard.threat_level by kThreatDecayRate.
    void tick(float dt);

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /// Current formation snapshot (updated each tick).
    [[nodiscard]] const Formation&  formation()  const noexcept;

    /// Shared knowledge store (updated each tick and by set_* methods).
    [[nodiscard]] const Blackboard& blackboard() const noexcept;

    /// Read-only view over all current squad members.
    [[nodiscard]] std::span<const SquadMember> members() const noexcept;

    /// Return a pointer to the member record for `entity_id`, or nullptr.
    [[nodiscard]] const SquadMember* find_member(uint64_t entity_id) const noexcept;

    /// Number of currently enlisted members.
    [[nodiscard]] std::size_t size() const noexcept;

    /// True when the squad has no members.
    [[nodiscard]] bool empty() const noexcept;

private:
    // Internal helper: recompute formation_ from current member positions.
    void recompute_formation() noexcept;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    std::vector<SquadMember> members_   {};
    Formation                formation_ {};
    Blackboard               blackboard_{};
};

}  // namespace cd::ai::squad
