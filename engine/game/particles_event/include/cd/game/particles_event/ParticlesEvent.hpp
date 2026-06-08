// =============================================================================
// CHROMODYNAMIC - cd/game/particles_event/ParticlesEvent.hpp
// Phase 498 (G3.4) - Recipe-driven gameplay particle dispatcher.
//
// `ParticleEventDispatcher` is the gameplay-side facade for triggering named
// particle "bursts" (e.g. "muzzle_flash", "explosion", "blood_hit").  It maps
// authored recipes to active bursts, ages them per tick, and reports the
// alive-count so a separate render-side integration (the GPU sink, typically
// `cd::render::gpu_particles`) can pull the staged data off and push it to
// the actual GPU compute pipeline.
//
// Scope (intentionally narrow):
//   * The dispatcher does NOT simulate per-particle physics here.
//   * It does NOT touch any RHI / shader / framegraph resource.
//   * It does NOT link against cd::render::gpu_particles - that integration
//     lives one tier above (see render-side adapter).
//   * What it DOES: store recipes by name, manage an active-burst list with
//     deterministic IDs, age bursts each `tick(dt)`, drop bursts whose
//     `age_s >= lifetime_s`, and fire user-supplied `on_emit` callbacks when
//     a burst is born (and exactly once - on the `fire()` call itself).
//
// Recipe data model (`ParticleRecipe`):
//   * `name`           - human authoring tag echoed back in the burst.
//   * `count`          - number of particles a single fire() requests.
//   * `lifetime_s`     - burst lifetime in seconds; `tick(dt)` retires bursts
//                        whose age >= lifetime_s.
//   * `gravity`        - constant acceleration vector (recipe metadata for
//                        the render-side integrator; not applied here).
//   * `velocity_min` / `velocity_max` - inclusive component-wise velocity
//                        envelope; the render-side spawn step samples within.
//   * `color_start` / `color_end`      - RGBA endpoints for a linear ramp
//                        evaluated against burst age fraction by the render
//                        side; staged here as recipe metadata.
//   * `emitter_shape`  - one of {kSphere, kCone, kBox}.
//   * `emitter_radius_or_extent` - shape parameter:
//                        * kSphere : radius scalar packed into .x; .y/.z = 0.
//                        * kCone   : half-angle (radians) in .x, height in .y;
//                                    .z reserved for future use.
//                        * kBox    : full Vec3 half-extents (x, y, z).
//                        Stored as Vec3 so all three shapes fit the same field.
//
// Active burst record (`ActiveBurst`):
//   * `recipe_idx`     - dense index into the dispatcher's recipe storage.
//   * `origin`         - world-space spawn position passed to `fire()`.
//   * `rotation`       - world-space spawn orientation passed to `fire()`.
//   * `age_s`          - accumulated seconds since the burst was created.
//   * `alive_count`    - particles still considered live; starts at
//                        `recipe.count` and the dispatcher decays it linearly
//                        with age so render-side consumers can early-out on
//                        finished bursts without re-checking the recipe.
//
// Threading: NOT thread-safe.  One dispatcher per gameplay thread; wrap with
// an external mutex if multiple systems fire concurrently.
//
// Library boundary (CLAUDE.md S7): depends ONLY on cd::core (CD_NODISCARD)
// and cd::math (Vec3f, Vec4f, Quatf).  No render / RHI / scene includes.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::game::particles_event
{

// -----------------------------------------------------------------------------
// Emitter shape - tagged enum; the radius / extent semantics live alongside
// `ParticleRecipe::emitter_radius_or_extent` (see header comment).
// -----------------------------------------------------------------------------
enum class EmitterShape : std::uint8_t
{
    kSphere = 0,
    kCone   = 1,
    kBox    = 2,
};

// -----------------------------------------------------------------------------
// ParticleRecipe - authored, immutable data for a named burst.
// Brief-mandated fields, plus `name` echoed back in bursts for tooling.
// -----------------------------------------------------------------------------
struct ParticleRecipe
{
    std::string      name {};                       ///< Authoring tag.
    std::uint32_t    count        {32};             ///< Particles per fire().
    float            lifetime_s   {1.0F};           ///< Burst life in seconds.
    cd::math::Vec3f  gravity      {0.0F, -9.81F, 0.0F};

    cd::math::Vec3f  velocity_min {-1.0F, 0.0F, -1.0F};
    cd::math::Vec3f  velocity_max { 1.0F, 4.0F,  1.0F};

    cd::math::Vec4f  color_start  {1.0F, 1.0F, 1.0F, 1.0F};
    cd::math::Vec4f  color_end    {1.0F, 1.0F, 1.0F, 0.0F};

    EmitterShape     emitter_shape           {EmitterShape::kSphere};
    /// Shape parameter packed per `emitter_shape` (see header comment).
    cd::math::Vec3f  emitter_radius_or_extent {0.5F, 0.0F, 0.0F};
};

// -----------------------------------------------------------------------------
// ActiveBurst - per-fire record carried in the dispatcher's live list.
// Pure data; the dispatcher mutates `age_s` / `alive_count` each tick.
// -----------------------------------------------------------------------------
struct ActiveBurst
{
    std::uint32_t    recipe_idx  {0};               ///< Index into recipe storage.
    cd::math::Vec3f  origin      {0.0F, 0.0F, 0.0F};
    cd::math::Quatf  rotation    {};
    float            age_s       {0.0F};            ///< Accumulated time alive.
    std::uint32_t    alive_count {0};               ///< Particles still considered live.
};

// -----------------------------------------------------------------------------
// ParticleEventDispatcher
//
// Lifecycle:
//   * `register_recipe(name, recipe)` stores or replaces a recipe under
//     `name`.  The recipe is copied by value.  Existing live bursts that
//     reference the same recipe index keep simulating against the new data.
//   * `fire(name, world_position, world_rotation)` looks the recipe up by
//     name.  Unknown name -> CD_LOG_WARN + no-op (the dispatcher does not
//     swallow the warning silently).  Known name -> push a new ActiveBurst
//     into the active list and invoke every registered `on_emit` callback
//     with a const-ref to the new burst.
//   * `tick(dt)` walks the active list, adds `dt` to each burst's `age_s`,
//     decays `alive_count` linearly with remaining-life fraction, then drops
//     bursts whose `age_s >= recipe.lifetime_s`.  Compaction is stable so
//     burst-creation order survives across ticks.
//
// `on_emit` callbacks: any number of callbacks may be registered via
// `add_on_emit`.  They run inline at `fire()` time, after the burst has been
// pushed into the active list (so the callback sees the final ActiveBurst
// state).  Callbacks are not invoked from `tick()`; bursts dying of old age
// fire NO callback - subscribers needing death notifications wrap the
// dispatcher with their own predicate over `active_bursts()`.
// -----------------------------------------------------------------------------
class ParticleEventDispatcher
{
public:
    using OnEmitCallback = std::function<void(const ActiveBurst&)>;

    ParticleEventDispatcher() = default;
    ~ParticleEventDispatcher() = default;

    ParticleEventDispatcher(const ParticleEventDispatcher&)            = delete;
    ParticleEventDispatcher& operator=(const ParticleEventDispatcher&) = delete;
    ParticleEventDispatcher(ParticleEventDispatcher&&) noexcept            = default;
    ParticleEventDispatcher& operator=(ParticleEventDispatcher&&) noexcept = default;

    // -- registry -----------------------------------------------------------

    /// Register or replace the recipe under `name`.  The `name` field on the
    /// recipe itself is overwritten with the lookup `name` for consistency
    /// (so `find_recipe(n)->name == n` always holds).
    void register_recipe(std::string_view name, ParticleRecipe recipe);

    /// Remove a previously registered recipe.  Live bursts spawned before the
    /// removal continue to age out via `tick(dt)`; the recipe storage slot is
    /// not reclaimed (it tombstones so existing recipe indices stay stable).
    /// Returns `true` if a recipe was actually removed.
    bool unregister_recipe(std::string_view name);

    /// Count of currently-registered recipes.
    CD_NODISCARD std::size_t recipe_count() const noexcept
    {
        return recipe_index_.size();
    }

    /// Look up a recipe by name; nullptr if unknown.
    CD_NODISCARD const ParticleRecipe* find_recipe(std::string_view name) const noexcept;

    // -- callbacks ----------------------------------------------------------

    /// Register a callback fired once per `fire()` (after the burst is in
    /// `active_bursts()`).  Returns a small id that can be passed back to
    /// `remove_on_emit` if the caller needs to detach.
    std::uint32_t add_on_emit(OnEmitCallback cb);

    /// Unregister a previously added callback.  No-op if `id` is unknown.
    void remove_on_emit(std::uint32_t id) noexcept;

    // -- runtime ------------------------------------------------------------

    /// Spawn a burst from the named recipe at the supplied world transform.
    /// Returns the new burst's index inside `active_bursts()` on success,
    /// or `kInvalidBurst` (== `UINT32_MAX`) on unknown recipe.
    static constexpr std::uint32_t kInvalidBurst = static_cast<std::uint32_t>(-1);

    std::uint32_t fire(std::string_view       name,
                       const cd::math::Vec3f& world_position,
                       const cd::math::Quatf& world_rotation);

    /// Advance every active burst by `dt` seconds.  Bursts whose
    /// `age_s >= recipe.lifetime_s` are dropped; the live list is compacted
    /// in stable order.  `alive_count` decays linearly with age over the
    /// recipe's lifetime so render-side consumers see a smooth taper.
    void tick(float dt);

    // -- inspection ---------------------------------------------------------

    /// Live burst span.  Stable through one tick; pointers / references
    /// invalidate on the next `fire()` / `tick()`.
    CD_NODISCARD std::span<const ActiveBurst> active_bursts() const noexcept
    {
        return { active_ };
    }

    /// Current live-burst count.
    CD_NODISCARD std::size_t active_count() const noexcept { return active_.size(); }

    /// Drop every active burst.  Registered recipes and callbacks are kept
    /// so callers can reuse the dispatcher across level reloads.
    void clear_active() noexcept;

    /// Drop everything: bursts + recipes + callbacks.
    void reset() noexcept;

private:
    // Recipe storage.  We keep a parallel name -> dense-index map so callers
    // pay one hash on `fire()` and bursts carry a small integer thereafter.
    // `recipes_storage_` slots are tombstoned (not erased) on
    // `unregister_recipe` so live bursts that already hold an index keep
    // simulating against the original data.
    std::unordered_map<std::string, std::uint32_t> recipe_index_   {};
    std::vector<ParticleRecipe>                    recipes_storage_ {};

    // Active bursts.  Compacted in stable order each tick.
    std::vector<ActiveBurst> active_ {};

    // Emit callbacks - parallel arrays so we can erase by id without
    // re-indexing every other entry.  std::function carries its own
    // small-buffer optimisation; the cost amortises across the registry.
    struct CallbackEntry
    {
        std::uint32_t  id {0};
        OnEmitCallback cb {};
    };
    std::vector<CallbackEntry> callbacks_ {};
    std::uint32_t              next_cb_id_ {1};
};

}  // namespace cd::game::particles_event
