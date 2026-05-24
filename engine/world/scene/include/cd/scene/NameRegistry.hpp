// =============================================================================
// CHROMODYNAMIC — cd/scene/NameRegistry.hpp
// Phase 44.B / Wave 212 — string ↔ entity name lookup.
//
// Scenes load with hand-authored names ("MainCamera", "Player",
// "Sun"). The runtime needs:
//   * O(1) name → entity lookup for save/load + editor selection.
//   * Optional reverse (entity → name) for debug output.
//   * Stable name hash that survives string reordering.
//
// The hash is FNV-1a 32-bit so callers can serialize the integer hash
// instead of the string when binary size matters.
//
// Storage indexed by Entity::id (uint32_t); the registry assumes name
// bindings are scene-scope (not generation-stable across destroy/recreate
// — the caller `unbind`s on entity destruction). Names are
// `std::string` (small-string optimization keeps short names
// allocation-free).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cd::scene
{

[[nodiscard]] constexpr std::uint32_t name_hash(std::string_view s) noexcept
{
    std::uint32_t h = 0x811C9DC5u;
    for (char c : s)
    {
        h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
        h *= 0x01000193u;
    }
    return h;
}

class NameRegistry
{
public:
    void bind(cd::ecs::Entity e, std::string name)
    {
        const auto hash = name_hash(name);
        auto it = by_entity_.find(e.id);
        if (it != by_entity_.end())
        {
            by_hash_.erase(name_hash(it->second));
            by_entity_.erase(it);
        }
        by_hash_[hash] = e;
        by_entity_[e.id] = std::move(name);
    }

    [[nodiscard]] cd::ecs::Entity find(std::string_view name) const noexcept
    {
        const auto it = by_hash_.find(name_hash(name));
        return (it != by_hash_.end()) ? it->second : cd::ecs::Entity {};
    }

    [[nodiscard]] cd::ecs::Entity find_by_hash(std::uint32_t hash) const noexcept
    {
        const auto it = by_hash_.find(hash);
        return (it != by_hash_.end()) ? it->second : cd::ecs::Entity {};
    }

    [[nodiscard]] std::string_view name_of(cd::ecs::Entity e) const noexcept
    {
        const auto it = by_entity_.find(e.id);
        return (it != by_entity_.end()) ? std::string_view { it->second } : std::string_view {};
    }

    void unbind(cd::ecs::Entity e)
    {
        const auto it = by_entity_.find(e.id);
        if (it == by_entity_.end()) return;
        by_hash_.erase(name_hash(it->second));
        by_entity_.erase(it);
    }

    [[nodiscard]] std::size_t size() const noexcept { return by_entity_.size(); }

private:
    std::unordered_map<std::uint32_t, cd::ecs::Entity> by_hash_;
    std::unordered_map<std::uint32_t, std::string>     by_entity_;  // entity.id → name
};

}  // namespace cd::scene
