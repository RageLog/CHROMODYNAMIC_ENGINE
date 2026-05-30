// =============================================================================
// CHROMODYNAMIC - cd/game/particles_event/ParticlesEvent.cpp
// Phase 498 (G3.4) - ParticleEventDispatcher implementation.
//
// Implementation notes:
//
//   * The dispatcher stages recipe data only - no per-particle physics, no
//     RHI / shader contact, no link against cd::render::gpu_particles. The
//     render-side adapter (one tier above this library) pulls
//     `active_bursts()` each frame and translates each ActiveBurst into the
//     GPU sink's spawn / advance commands.
//
//   * Recipe storage is a flat vector indexed by a dense `std::uint32_t`
//     handle. `register_recipe` REPLACES an existing entry in-place so any
//     ActiveBurst already holding that index continues to simulate against
//     the new (or updated) recipe data. `unregister_recipe` tombstones the
//     name map but DOES NOT erase the storage slot, so live bursts that
//     still hold a stale index keep their original recipe view alive until
//     they age out.
//
//   * `tick(dt)` walks the active list, ages every burst, decays
//     `alive_count` linearly with remaining-life fraction, and compacts
//     finished bursts out. Compaction is stable so creation order survives.
//
//   * `on_emit` callbacks run inline at `fire()` time. They are NOT called
//     from `tick()`; "burst died of old age" notifications belong in a
//     consumer that walks `active_bursts()` itself.
// =============================================================================
#include <cd/game/particles_event/ParticlesEvent.hpp>

#include <cd/log/Service.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cd::game::particles_event
{

// -----------------------------------------------------------------------------
// Registry mutators.
// -----------------------------------------------------------------------------
void ParticleEventDispatcher::register_recipe(std::string_view name,
                                              ParticleRecipe   recipe)
{
    std::string key {name};
    // Keep recipe.name in sync with the lookup key so `find_recipe(n)->name
    // == n` is always true (callers may have left .name empty in the input).
    recipe.name = key;

    const auto it = recipe_index_.find(key);
    if (it == recipe_index_.end())
    {
        const auto id = static_cast<std::uint32_t>(recipes_storage_.size());
        recipes_storage_.emplace_back(std::move(recipe));
        recipe_index_.emplace(std::move(key), id);
    }
    else
    {
        // Replace in place so live bursts already pointing at this slot see
        // the new recipe.
        recipes_storage_[it->second] = std::move(recipe);
    }
}

bool ParticleEventDispatcher::unregister_recipe(std::string_view name)
{
    const auto it = recipe_index_.find(std::string {name});
    if (it == recipe_index_.end())
    {
        return false;
    }
    // We intentionally do NOT erase from `recipes_storage_`. The recipe
    // index inside any still-alive ActiveBurst MUST remain valid until the
    // burst expires; tombstoning the name map prevents future fires from
    // the same name while preserving stable indices.
    recipe_index_.erase(it);
    return true;
}

const ParticleRecipe*
ParticleEventDispatcher::find_recipe(std::string_view name) const noexcept
{
    const auto it = recipe_index_.find(std::string {name});
    if (it == recipe_index_.end())
    {
        return nullptr;
    }
    return &recipes_storage_[it->second];
}

// -----------------------------------------------------------------------------
// Callback registry.
// -----------------------------------------------------------------------------
std::uint32_t
ParticleEventDispatcher::add_on_emit(OnEmitCallback cb)
{
    const std::uint32_t id = next_cb_id_++;
    callbacks_.push_back(CallbackEntry {id, std::move(cb)});
    return id;
}

void ParticleEventDispatcher::remove_on_emit(std::uint32_t id) noexcept
{
    const auto it = std::find_if(
        callbacks_.begin(), callbacks_.end(),
        [id](const CallbackEntry& e) noexcept { return e.id == id; });
    if (it != callbacks_.end())
    {
        callbacks_.erase(it);
    }
}

// -----------------------------------------------------------------------------
// fire - spawn a burst, invoke on_emit callbacks, return its index.
// -----------------------------------------------------------------------------
std::uint32_t
ParticleEventDispatcher::fire(std::string_view       name,
                              const cd::math::Vec3f& world_position,
                              const cd::math::Quatf& world_rotation)
{
    const auto it = recipe_index_.find(std::string {name});
    if (it == recipe_index_.end())
    {
        CD_LOG_WARN("ParticleEventDispatcher::fire - unknown recipe name");
        return kInvalidBurst;
    }
    const std::uint32_t   recipe_id = it->second;
    const ParticleRecipe& recipe    = recipes_storage_[recipe_id];

    ActiveBurst burst {};
    burst.recipe_idx  = recipe_id;
    burst.origin      = world_position;
    burst.rotation    = world_rotation;
    burst.age_s       = 0.0F;
    burst.alive_count = recipe.count;

    active_.push_back(burst);

    // Fire on_emit callbacks. We pass a const-ref to the freshly-pushed
    // burst so callbacks see the canonical state. Copy the callback list
    // index instead of iterators in case a callback mutates the registry
    // (e.g. by detaching itself).
    const ActiveBurst& published = active_.back();
    for (std::size_t i = 0; i < callbacks_.size(); ++i)
    {
        if (callbacks_[i].cb)
        {
            callbacks_[i].cb(published);
        }
    }

    return static_cast<std::uint32_t>(active_.size() - 1U);
}

// -----------------------------------------------------------------------------
// tick - age bursts, decay alive_count, compact finished bursts.
// -----------------------------------------------------------------------------
void ParticleEventDispatcher::tick(float dt)
{
    if (active_.empty() || dt <= 0.0F)
    {
        // Negative / zero dt still requires aging-zero side effects? The
        // contract is "age by dt"; dt <= 0 is a clean no-op so callers can
        // pause without re-checking. Empty list -> nothing to do.
        if (active_.empty())
        {
            return;
        }
    }

    // Walk + compact in one pass: copy survivors forward to `cursor`.
    std::size_t cursor = 0U;
    for (std::size_t i = 0; i < active_.size(); ++i)
    {
        ActiveBurst& b = active_[i];
        if (dt > 0.0F)
        {
            b.age_s += dt;
        }

        const ParticleRecipe& recipe = recipes_storage_[b.recipe_idx];
        const float           life   = std::max(recipe.lifetime_s, 1e-6F);

        if (b.age_s >= recipe.lifetime_s)
        {
            // Expired. Skip (do not advance cursor).
            continue;
        }

        // Linear decay of alive_count over lifetime - the render side uses
        // this to fade emission strength without re-evaluating curves.
        const float remaining = std::max(0.0F, 1.0F - (b.age_s / life));
        const auto  decayed   = static_cast<std::uint32_t>(
            std::round(static_cast<float>(recipe.count) * remaining));
        b.alive_count = decayed;

        if (cursor != i)
        {
            active_[cursor] = b;
        }
        ++cursor;
    }
    active_.resize(cursor);
}

// -----------------------------------------------------------------------------
void ParticleEventDispatcher::clear_active() noexcept
{
    active_.clear();
}

void ParticleEventDispatcher::reset() noexcept
{
    active_.clear();
    recipe_index_.clear();
    recipes_storage_.clear();
    callbacks_.clear();
    next_cb_id_ = 1;
}

}  // namespace cd::game::particles_event
