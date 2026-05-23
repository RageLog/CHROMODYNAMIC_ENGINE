// =============================================================================
// CHROMODYNAMIC — cd/ecs/Entity.cpp
// =============================================================================
#include <cd/ecs/Entity.hpp>

namespace cd::ecs
{

Entity EntityManager::create()
{
    std::uint32_t id { 0 };
    if (!free_slots_.empty())
    {
        id = free_slots_.back();
        free_slots_.pop_back();
        // Bump generation: was even (free) → odd (alive).
        ++generations_[id];
    }
    else
    {
        id = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(1U);  // first alive generation
    }
    ++alive_;
    return Entity { id, generations_[id] };
}

void EntityManager::destroy(Entity e)
{
    if (!is_alive(e))
        return;
    // Bump generation: was odd (alive) → even (free). The same slot can be
    // handed out again later but with a higher (and therefore non-matching)
    // generation, so the stale handle harmlessly fails is_alive().
    ++generations_[e.id];
    free_slots_.push_back(e.id);
    --alive_;
}

bool EntityManager::is_alive(Entity e) const noexcept
{
    if (!e.is_valid())
        return false;
    if (e.id >= generations_.size())
        return false;
    return generations_[e.id] == e.generation;
}

}  // namespace cd::ecs
