// =============================================================================
// CHROMODYNAMIC - cd/game/trigger/Trigger.hpp
// Phase 476 (G3.2) - Trigger volumes + event sources.
//
// A `TriggerWorld` owns a collection of named `TriggerVolume` entries keyed by
// `cd::ecs::Entity`. Each tick the world is fed the current positions of
// "subject" entities (the things that *enter* triggers - players, NPCs,
// projectiles); for every (volume, subject) pair the world maintains an
// occupancy bit and dispatches `on_enter` / `on_stay` / `on_exit` callbacks on
// transitions matching the canonical Unity / Unreal contract:
//
//   * on_enter : fires once on the tick where (subject was outside -> inside).
//   * on_stay  : fires every tick where (subject inside) and on_enter has
//                already fired this occupancy.
//   * on_exit  : fires once on the tick where (subject was inside -> outside).
//
// "Same-frame teleport" (subject enters and exits within one tick because its
// position jumped past the volume): on_enter does NOT fire because we never
// sampled the subject *inside* the volume. This matches Unity's
// `OnTriggerEnter` / Unreal's `OnComponentBeginOverlap` semantics on discrete
// sampling - a subject that was outside on tick N and outside on tick N+1
// produces no events even if its world-space trajectory crossed the volume.
// Subclasses that need swept-volume teleport detection wrap this layer with
// their own CCD pre-pass.
//
// Layer filtering: each TriggerVolume carries a 64-bit `layer_mask`; each
// subject carries its own `layer` integer in [0, 63]. The volume only fires
// on subjects whose `(1ULL << layer) & layer_mask` is non-zero (mirrors
// Unity Physics layers and Unreal collision channels).
//
// Spatial backend: G3.1 `cd::game::query::QueryWorld` is the intended
// broad-phase index; this revision uses a brute-force O(N*M) AABB / sphere
// scan and accepts the index as an opaque forward-declared pointer (nullable)
// so the switch to the indexed path is a body-only change with no API churn.
// See README G3.2 note for the integration plan.
//
// Library boundary (CLAUDE.md S7): depends only on cd::core / cd::math /
// cd::ecs / cd::physics for AABB+Sphere primitives; no scene / render / world
// includes. Callers feed `(entity, position, layer)` triples each tick - the
// trigger world never reaches into ECS storage itself, keeping the dependency
// arrow flowing upward through the DAG.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>
#include <cd/physics/Sphere.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// -----------------------------------------------------------------------------
// Forward declaration of cd::game::query::QueryWorld - the spatial broad-phase
// backend planned for G3.1.  The dependency is opaque here (raw pointer in
// the tick signature) so this header does NOT include the query library; if
// the caller has it, they pass it in, otherwise they pass nullptr and the
// brute-force fallback kicks in.
// -----------------------------------------------------------------------------
namespace cd::game::query
{
class QueryWorld;
}  // namespace cd::game::query

namespace cd::game::trigger
{

// -----------------------------------------------------------------------------
// Layer mask alias - 64 channels.  `0` means "fire on no subject" (disabled
// filtering by mask); `kAllLayers` means "fire on every subject regardless of
// its layer integer" and is the default to keep simple callers terse.
// -----------------------------------------------------------------------------
using LayerMask = std::uint64_t;

inline constexpr LayerMask kAllLayers = static_cast<LayerMask>(-1);

// -----------------------------------------------------------------------------
// TriggerVolume - shape + callbacks + filtering flags.
//
// Shape is a sum type: AABB or Sphere.  Both primitives share the same
// callback signature, so callers swap one for the other without touching
// dispatch code.  Future expansion (capsule / OBB) is one additional variant
// arm.
//
// Callbacks are stored as `std::function` so lambdas, free functions, and
// member binds all work without subclassing.  Any unset callback is a no-op.
//
// `enabled` is a runtime gate - flipping it to false short-circuits dispatch
// for that volume on the next tick without losing its occupancy history.
// Re-enabling a volume re-evaluates occupancy fresh: a subject already
// inside fires on_enter on the first re-enabled tick.
// -----------------------------------------------------------------------------
struct TriggerVolume
{
    using Shape    = std::variant<cd::physics::Aabb, cd::physics::Sphere>;
    using EventFn  = std::function<void(cd::ecs::Entity /*subject*/)>;

    std::string name {};
    Shape       shape {};
    EventFn     on_enter {};
    EventFn     on_stay {};
    EventFn     on_exit {};
    LayerMask   layer_mask {kAllLayers};
    bool        enabled {true};
};

// -----------------------------------------------------------------------------
// Subject - position + layer index, fed to `tick` each frame.
//
// `layer` is an integer in [0, 63] selecting a single channel; convert to a
// mask with `(1ULL << layer)`.  A subject with `layer >= 64` is treated as
// "channel 0" (we don't UB).  Subjects with a duplicate entity id within the
// same tick are tolerated (later wins) but unintended.
// -----------------------------------------------------------------------------
struct Subject
{
    cd::ecs::Entity entity {};
    cd::math::Vec3f position {};
    std::uint8_t    layer {0};
};

// -----------------------------------------------------------------------------
// TriggerWorld - keyed registry + dispatch loop.
//
// Lifecycle:
//   * `add_trigger(entity, volume)` registers a volume under the owning entity
//     (the entity that authored it - usually the trigger gameobject itself,
//     distinct from the subjects flowing through it).  Re-adding under the
//     same entity replaces the previous volume.
//   * `remove_trigger(entity)` discards the volume + its occupancy history;
//     no exit event fires for currently-inside subjects (consistent with
//     "trigger destroyed" semantics in Unity / Unreal - the gameobject is
//     gone, no callback target left to fire on).
//   * `tick(query_world, dt, subjects)` walks every enabled volume, tests
//     each subject against its shape, and fires the appropriate event.
//
// Thread-safety: NOT thread-safe.  One TriggerWorld per gameplay thread;
// wrap externally if shared between systems.
// -----------------------------------------------------------------------------
class TriggerWorld
{
public:
    TriggerWorld() = default;
    ~TriggerWorld() = default;

    TriggerWorld(const TriggerWorld&)            = delete;
    TriggerWorld& operator=(const TriggerWorld&) = delete;
    TriggerWorld(TriggerWorld&&)                 = default;
    TriggerWorld& operator=(TriggerWorld&&)      = default;

    // -- structure ----------------------------------------------------------

    /// Register or replace the volume owned by `owner`.  Replacing wipes the
    /// previous occupancy set so the new volume starts dispatch fresh.
    void add_trigger(cd::ecs::Entity owner, TriggerVolume volume);

    /// Remove the volume owned by `owner` together with its occupancy state.
    /// Returns true if a volume was actually removed.
    bool remove_trigger(cd::ecs::Entity owner);

    /// Toggle dispatch for the volume owned by `owner` without removing it.
    /// Returns true if the volume existed.  Disabling does NOT fire on_exit
    /// for current occupants - re-enabling will re-fire on_enter on the next
    /// tick if they are still inside.
    bool set_enabled(cd::ecs::Entity owner, bool enabled);

    /// Count of registered trigger volumes (enabled or not).
    CD_NODISCARD std::size_t trigger_count() const noexcept { return volumes_.size(); }

    /// Read-only volume access (debug / inspection only).  Returns nullptr if
    /// no volume is registered under `owner`.
    CD_NODISCARD const TriggerVolume* find(cd::ecs::Entity owner) const noexcept;

    // -- runtime ------------------------------------------------------------

    /// Advance one trigger tick.  `query_world` is an optional broad-phase
    /// index (G3.1 cd::game::query::QueryWorld); pass nullptr to use the
    /// brute-force O(N_volumes * N_subjects) fallback.  `dt` is forwarded
    /// unchanged - it is *not* used by the dispatch itself (trigger occupancy
    /// is a discrete predicate, not an integrator), but is part of the
    /// contract so callers can hand off the same dt they pass to FSM / BT.
    ///
    /// Dispatch order per volume:
    ///   1. Skip disabled volumes.
    ///   2. For every subject (after layer-mask filter):
    ///      a. inside  = shape.contains(subject.position)
    ///      b. was_in  = occupancy_.contains(volume_owner, subject.entity)
    ///      c. if  inside && !was_in -> on_enter, insert in occupancy_
    ///         if  inside &&  was_in -> on_stay
    ///         if !inside &&  was_in -> on_exit,  erase from occupancy_
    ///         else                  -> no event
    ///   3. Subjects no longer present this tick that were inside on the
    ///      previous tick get on_exit fired (subject removal == implicit exit).
    void tick(cd::game::query::QueryWorld* query_world,
              float                        dt,
              const std::vector<Subject>&  subjects);

    /// Direct query: is `subject_entity` currently inside the volume owned by
    /// `owner`?  Returns false if `owner` has no volume.
    CD_NODISCARD bool is_inside(cd::ecs::Entity owner,
                                cd::ecs::Entity subject) const noexcept;

    /// Reset every volume's occupancy without removing the volumes themselves.
    /// Useful for level reloads.  Does NOT fire on_exit for current occupants.
    void clear_occupancy() noexcept;

private:
    // Hash helper for std::pair<Entity, Entity> keying occupancy by
    // (owner, subject).  Two 32-bit fields per entity packed into a 64-bit
    // word per side, mixed with a splitmix64 step for distribution.
    struct EntityPairHash
    {
        std::size_t operator()(const std::pair<cd::ecs::Entity, cd::ecs::Entity>& p)
            const noexcept;
    };

    struct EntityPairEq
    {
        bool operator()(const std::pair<cd::ecs::Entity, cd::ecs::Entity>& a,
                        const std::pair<cd::ecs::Entity, cd::ecs::Entity>& b)
            const noexcept
        {
            return a.first == b.first && a.second == b.second;
        }
    };

    struct EntityHash
    {
        std::size_t operator()(cd::ecs::Entity e) const noexcept;
    };

    struct EntityEq
    {
        bool operator()(cd::ecs::Entity a, cd::ecs::Entity b) const noexcept
        {
            return a == b;
        }
    };

    // Per-volume registry keyed by owner entity.
    std::unordered_map<cd::ecs::Entity, TriggerVolume, EntityHash, EntityEq> volumes_ {};

    // Occupancy bitmap as a set of (owner, subject) pairs that were inside on
    // the previous tick.  Using a hash set instead of a per-volume vector
    // keeps the membership check O(1) for the brute-force tick path and stays
    // compatible with the indexed query path: the indexed path will still need
    // a per-pair "was inside last tick" lookup.
    std::unordered_set<std::pair<cd::ecs::Entity, cd::ecs::Entity>,
                       EntityPairHash, EntityPairEq>
        occupancy_ {};

    // Shape-vs-point dispatch helper.  Inline-friendly variant visitor.
    CD_NODISCARD static bool point_in_shape(const TriggerVolume::Shape& shape,
                                            const cd::math::Vec3f&      p) noexcept;
};

}  // namespace cd::game::trigger
