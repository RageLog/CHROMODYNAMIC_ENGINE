// =============================================================================
// CHROMODYNAMIC - cd/game/trigger/Trigger.cpp
// Phase 476 (G3.2) - TriggerWorld implementation.
//
// Brute-force fallback (cd::game::query not yet wired): for every enabled
// volume, walk every subject, run an O(1) point-in-shape test, and dispatch
// on the (was_in, is_in) transition.  Complexity is O(V * S) per tick - fine
// for the typical "tens of triggers, single-digit subjects" gameplay budget,
// and a known cliff above ~1k * 1k that G3.1 indexed broad-phase will fix.
//
// Subject-removal exit detection:
//   We capture the set of (owner, subject) pairs touched on this tick in
//   `seen_pairs_`.  After the main loop, any pair still in `occupancy_` that
//   is NOT in `seen_pairs_` represents a subject that was inside last tick
//   but did not appear in this tick's subject list at all - i.e. the gameplay
//   layer destroyed / despawned it without first walking it out.  We fire
//   on_exit for those pairs and drop them from occupancy_.  This matches the
//   Unity / Unreal contract that destroying a gameobject inside a trigger
//   fires `OnTriggerExit` exactly once before the entity is forgotten.
// =============================================================================
#include <cd/game/trigger/Trigger.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cd::game::trigger
{

// -----------------------------------------------------------------------------
// Hash helpers - splitmix64 mix on the 64-bit pack of (id, generation).
// -----------------------------------------------------------------------------
namespace
{

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t x) noexcept
{
    x ^= x >> 30U;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27U;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31U;
    return x;
}

[[nodiscard]] std::uint64_t pack_entity(cd::ecs::Entity e) noexcept
{
    return (static_cast<std::uint64_t>(e.generation) << 32U)
         | static_cast<std::uint64_t>(e.id);
}

}  // namespace

std::size_t TriggerWorld::EntityHash::operator()(cd::ecs::Entity e) const noexcept
{
    return static_cast<std::size_t>(splitmix64(pack_entity(e)));
}

std::size_t TriggerWorld::EntityPairHash::operator()(
    const std::pair<cd::ecs::Entity, cd::ecs::Entity>& p) const noexcept
{
    // Mix two 64-bit packs into one hash.  Boost-style hash_combine on the
    // splitmix output of each side - distribution is more than adequate for
    // the trigger-occupancy load (rare collisions, set membership only).
    const std::uint64_t h1 = splitmix64(pack_entity(p.first));
    const std::uint64_t h2 = splitmix64(pack_entity(p.second));
    return static_cast<std::size_t>(h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U)));
}

// -----------------------------------------------------------------------------
// Shape dispatch - std::visit on the variant.  Both arms are O(1) - AABB is
// 6 compares, sphere is 3 multiplies + a compare.
// -----------------------------------------------------------------------------
bool TriggerWorld::point_in_shape(const TriggerVolume::Shape& shape,
                                  const cd::math::Vec3f&      p) noexcept
{
    return std::visit([&p](const auto& s) noexcept -> bool {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, cd::physics::Aabb>)
        {
            return cd::physics::contains(s, p);
        }
        else if constexpr (std::is_same_v<T, cd::physics::Sphere>)
        {
            return cd::physics::contains(s, p);
        }
        else
        {
            return false;
        }
    }, shape);
}

// -----------------------------------------------------------------------------
// Structure mutators.
// -----------------------------------------------------------------------------
void TriggerWorld::add_trigger(cd::ecs::Entity owner, TriggerVolume volume)
{
    // Drop any prior occupancy for this owner before replacing the volume -
    // the new volume has its own shape so prior "was inside" pairs are no
    // longer meaningful and would mis-classify the first tick as "stay".
    for (auto it = occupancy_.begin(); it != occupancy_.end(); )
    {
        if (it->first == owner)
        {
            it = occupancy_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    volumes_[owner] = std::move(volume);
}

bool TriggerWorld::remove_trigger(cd::ecs::Entity owner)
{
    const auto it = volumes_.find(owner);
    if (it == volumes_.end())
    {
        return false;
    }
    volumes_.erase(it);
    // Drop occupancy pairs whose owner side matches - those subjects no longer
    // have a callback target, no exit event fires (the volume "disappeared").
    for (auto p = occupancy_.begin(); p != occupancy_.end(); )
    {
        if (p->first == owner)
        {
            p = occupancy_.erase(p);
        }
        else
        {
            ++p;
        }
    }
    return true;
}

bool TriggerWorld::set_enabled(cd::ecs::Entity owner, bool enabled)
{
    const auto it = volumes_.find(owner);
    if (it == volumes_.end())
    {
        return false;
    }
    it->second.enabled = enabled;
    if (!enabled)
    {
        // Clear occupancy for a disabled volume so re-enabling it later
        // produces a fresh on_enter rather than "stay" on whoever happens to
        // still be inside.
        for (auto p = occupancy_.begin(); p != occupancy_.end(); )
        {
            if (p->first == owner)
            {
                p = occupancy_.erase(p);
            }
            else
            {
                ++p;
            }
        }
    }
    return true;
}

const TriggerVolume* TriggerWorld::find(cd::ecs::Entity owner) const noexcept
{
    const auto it = volumes_.find(owner);
    return it == volumes_.end() ? nullptr : &it->second;
}

bool TriggerWorld::is_inside(cd::ecs::Entity owner,
                             cd::ecs::Entity subject) const noexcept
{
    return occupancy_.find({owner, subject}) != occupancy_.end();
}

void TriggerWorld::clear_occupancy() noexcept
{
    occupancy_.clear();
}

// -----------------------------------------------------------------------------
// Tick dispatch.
// -----------------------------------------------------------------------------
void TriggerWorld::tick(cd::game::query::QueryWorld* /*query_world*/,
                        float                        /*dt*/,
                        const std::vector<Subject>&  subjects)
{
    // Track (owner, subject) pairs visited this tick so we can detect
    // subject-removal exits at the end.
    std::unordered_set<std::pair<cd::ecs::Entity, cd::ecs::Entity>,
                       EntityPairHash, EntityPairEq>
        seen_pairs;
    seen_pairs.reserve(volumes_.size() * subjects.size());

    for (auto& kv : volumes_)
    {
        const cd::ecs::Entity owner  = kv.first;
        TriggerVolume&        volume = kv.second;
        if (!volume.enabled)
        {
            continue;
        }

        for (const Subject& s : subjects)
        {
            // Layer-mask filter - subject layer >=64 falls back to channel 0
            // rather than UB on `1ULL << 64`.
            const std::uint8_t  layer    = (s.layer < 64U) ? s.layer : 0U;
            const LayerMask     subj_bit = static_cast<LayerMask>(1ULL) << layer;
            if ((volume.layer_mask & subj_bit) == 0U)
            {
                continue;
            }

            const std::pair<cd::ecs::Entity, cd::ecs::Entity> key {owner, s.entity};
            seen_pairs.insert(key);

            const bool inside = point_in_shape(volume.shape, s.position);
            const bool was_in = occupancy_.find(key) != occupancy_.end();

            if (inside && !was_in)
            {
                occupancy_.insert(key);
                if (volume.on_enter)
                {
                    volume.on_enter(s.entity);
                }
            }
            else if (inside && was_in)
            {
                if (volume.on_stay)
                {
                    volume.on_stay(s.entity);
                }
            }
            else if (!inside && was_in)
            {
                occupancy_.erase(key);
                if (volume.on_exit)
                {
                    volume.on_exit(s.entity);
                }
            }
            // else (!inside && !was_in) -> no event.
        }
    }

    // Subject-removal exit detection: anything in occupancy_ that wasn't seen
    // this tick must have been despawned while inside.  Fire on_exit on
    // behalf of the gameplay layer so book-keeping stays consistent.
    for (auto it = occupancy_.begin(); it != occupancy_.end(); )
    {
        if (seen_pairs.find(*it) == seen_pairs.end())
        {
            const auto owner_it = volumes_.find(it->first);
            if (owner_it != volumes_.end() && owner_it->second.enabled
                && owner_it->second.on_exit)
            {
                owner_it->second.on_exit(it->second);
            }
            it = occupancy_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

}  // namespace cd::game::trigger
