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
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::ai::squad
{

// =============================================================================
// FormationShape — geometric pattern used when computing slot offsets.
//
//   kLine   — members placed side-by-side along the right-axis (X).
//             slot 0 is on-axis; slot i is at i*spacing right.
//   kWedge  — V-formation. Slot 0 is the point; subsequent pairs spread
//             back and outward symmetrically (left/right alternating).
//   kColumn — single-file behind the leader. Slot i is at -i*spacing along
//             the forward axis (Z negative = behind).
//   kCircle — members distributed evenly around the centroid at `radius`.
//
// All offsets are in a local right-handed coordinate frame where:
//   +X = right   +Y = up (unused for 2D ground squads)   +Z = forward
// The caller transforms them into world space using the squad's heading.
// =============================================================================
enum class FormationShape : std::uint8_t
{
    kLine   = 0,  ///< Side-by-side line.
    kWedge  = 1,  ///< V-formation with the leader at the tip.
    kColumn = 2,  ///< Single-file column.
    kCircle = 3,  ///< Even ring around the centroid.
};

// =============================================================================
// slot_offset_for — return the local-space 2D offset (X,Z) for slot `index`
// in the given `shape`, using `spacing` as the uniform inter-member distance.
//
// Returns std::array<float,2>{dx, dz} in the local frame described above.
// The Y (up) component is always 0 — callers that need 3D can splat it in.
//
// Slot 0 is always the leader/pointman position (origin or tip).
// =============================================================================
[[nodiscard]] inline std::array<float, 2> slot_offset_for(
    FormationShape shape,
    std::uint8_t   index,
    float          spacing) noexcept
{
    const auto i = static_cast<float>(index);

    switch (shape)
    {
    case FormationShape::kLine:
        // Centred line: slot 0 at x=0, then alternate ±spacing.
        //   0 → 0,  1 → +s,  2 → -s,  3 → +2s,  4 → -2s …
        {
            const float sign   = (index % 2U == 0U) ? 1.0F : -1.0F;
            const auto  pair   = (index + 1U) / 2U;  // integer pair step (floor)
            const float offset = static_cast<float>(pair) * spacing;
            return {sign * offset, 0.0F};
        }

    case FormationShape::kWedge:
        // Slot 0 is the tip (origin). Slots 1,2 fan back ±spacing on X and
        // back spacing on Z. General: side = index is odd → right (+X),
        // even (>0) → left (-X). Depth increases one step per pair.
        {
            if (index == 0U) { return {0.0F, 0.0F}; }
            const float side  = (index % 2U == 1U) ? 1.0F : -1.0F;
            const auto  pair  = (index + 1U) / 2U;  // integer pair step (floor)
            const auto  depth = static_cast<float>(pair);
            return {side * depth * spacing, -depth * spacing};
        }

    case FormationShape::kColumn:
        // Single file: slot i is at (0, -i*spacing).
        return {0.0F, -i * spacing};

    case FormationShape::kCircle:
        // Ring placement requires the total member count to compute angle
        // fractions. slot_offset_for() cannot carry that without an extra
        // parameter, so kCircle always returns {0,0} here. Callers that
        // need per-slot ring offsets must use slot_offset_circle() directly.
        return {0.0F, 0.0F};
    }
    return {0.0F, 0.0F};
}

// =============================================================================
// slot_offset_circle — dedicated ring overload that takes the total member
// count so the angle fraction can be computed exactly.
//
//   ring_radius — distance from centroid to each member.
//   index       — this member's slot index [0, member_count).
//   member_count — total members in the ring (≥ 1).
//
// Returns {dx, dz} in local squad space.
// =============================================================================
[[nodiscard]] inline std::array<float, 2> slot_offset_circle(
    float        ring_radius,
    std::uint8_t index,
    std::uint8_t member_count) noexcept
{
    if (member_count == 0U) { return {0.0F, 0.0F}; }
    const float angle = 2.0F * std::numbers::pi_v<float>
                        * static_cast<float>(index)
                        / static_cast<float>(member_count);
    return {ring_radius * std::cos(angle), ring_radius * std::sin(angle)};
}

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

    /// Reassign the tactical role of an existing member.
    /// No-op if `entity_id` is not a member.
    void assign_role(uint64_t entity_id, SquadRole role);

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
    // Formation shape control
    // -------------------------------------------------------------------------

    /// Set the geometric pattern used by slot_offset_for() queries.
    /// Default: kLine.
    void set_formation_shape(FormationShape shape) noexcept;

    /// Return the current formation shape.
    [[nodiscard]] FormationShape formation_shape() const noexcept;

    /// Return the inter-member spacing used for slot offset calculations.
    /// Default: 2.0 m.
    [[nodiscard]] float spacing() const noexcept;

    /// Set the inter-member spacing (must be > 0; values ≤ 0 are clamped to 0.1).
    void set_spacing(float s) noexcept;

    // -------------------------------------------------------------------------
    // Cohesion / regroup
    // -------------------------------------------------------------------------

    /// True when every member is within `radius` metres of the formation
    /// centroid. Useful for "are we regrouped?" queries after a scatter.
    /// Returns true for an empty squad (vacuously true).
    [[nodiscard]] bool within_cohesion(float radius) const noexcept;

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
    std::vector<SquadMember> members_         {};
    Formation                formation_        {};
    Blackboard               blackboard_       {};
    FormationShape           shape_            {FormationShape::kLine};
    float                    spacing_          {2.0F};  ///< Inter-member spacing (metres).
};

}  // namespace cd::ai::squad
