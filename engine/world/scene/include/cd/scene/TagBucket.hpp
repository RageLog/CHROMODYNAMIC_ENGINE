// =============================================================================
// CHROMODYNAMIC — cd/scene/TagBucket.hpp
// Phase 99.A / Wave 267 — tag → entity bucket registry.
//
// Companion to `cd::asset::AssetTag` (Phase 35): instead of asset
// tagging, this is entity tagging in a scene. `TagBucket` maps a
// tag hash to the set of entity IDs marked with it, so gameplay
// queries can fetch "all entities tagged Enemy" in O(1).
//
//   * `tag(entity, "Enemy")` — add to bucket.
//   * `untag(entity, "Enemy")` — remove.
//   * `entities_with("Enemy")` — span of IDs.
//
// Storage: `unordered_map<hash, vector<Entity>>`. Tag string hashed
// via FNV-1a 32; collision is caller-coordinated.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::scene
{

[[nodiscard]] constexpr std::uint32_t tag_hash(std::string_view s) noexcept
{
    std::uint32_t h = 0x811C9DC5u;
    for (char c : s)
    {
        h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
        h *= 0x01000193u;
    }
    return h;
}

class TagBucket
{
public:
    void tag(cd::ecs::Entity e, std::string_view name)
    {
        auto& v = buckets_[tag_hash(name)];
        for (const auto& x : v) if (x == e) return;
        v.push_back(e);
    }

    void untag(cd::ecs::Entity e, std::string_view name)
    {
        auto it = buckets_.find(tag_hash(name));
        if (it == buckets_.end()) return;
        auto& v = it->second;
        v.erase(std::remove(v.begin(), v.end(), e), v.end());
        if (v.empty()) buckets_.erase(it);
    }

    [[nodiscard]] const std::vector<cd::ecs::Entity>&
    entities_with(std::string_view name) const
    {
        auto it = buckets_.find(tag_hash(name));
        return (it != buckets_.end()) ? it->second : empty_;
    }

    [[nodiscard]] std::size_t bucket_count() const noexcept { return buckets_.size(); }

    void clear() noexcept { buckets_.clear(); }

private:
    std::unordered_map<std::uint32_t, std::vector<cd::ecs::Entity>> buckets_;
    static inline const std::vector<cd::ecs::Entity>                empty_ {};
};

}  // namespace cd::scene
