// =============================================================================
// CHROMODYNAMIC — cd/asset/AssetTag.hpp
// Phase 34.B / Wave 202 — typed tag set for asset filtering.
//
// Tags are lightweight string labels that group assets without forcing
// them into a single hierarchical category: an enemy character mesh
// can be tagged "character", "enemy", "tier:3" simultaneously. The
// editor uses tag queries ("all tier:3 enemies in level 4") to drive
// hot-reload scopes, baking selections, and search filters.
//
// Internally a tag is a 32-bit FNV-1a hash of its string. Hash
// collisions are caller-visible (caller must coordinate the namespace);
// this matches the "tag is a developer-visible label, not a UUID"
// design — a 32-bit space comfortably handles a few thousand tags.
//
// `AssetTagSet` is a small ordered vector of hash entries; lookup is
// O(log n) binary search; insertion is O(n). Tag count per asset is
// expected to stay small (≤ ~16), so cache locality beats a hash table.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cd::asset
{

struct AssetTag
{
    std::uint32_t hash { 0 };

    friend constexpr bool operator==(AssetTag, AssetTag) noexcept = default;
    friend constexpr auto operator<=>(AssetTag, AssetTag) noexcept = default;
};

[[nodiscard]] constexpr std::uint32_t fnv1a_32(std::string_view s) noexcept
{
    std::uint32_t h = 0x811C9DC5u;
    for (char c : s)
    {
        h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
        h *= 0x01000193u;
    }
    return h;
}

[[nodiscard]] constexpr AssetTag make_tag(std::string_view label) noexcept
{
    return AssetTag { fnv1a_32(label) };
}

class AssetTagSet
{
public:
    void add(AssetTag t)
    {
        auto it = std::lower_bound(tags_.begin(), tags_.end(), t);
        if (it == tags_.end() || *it != t) tags_.insert(it, t);
    }

    void remove(AssetTag t)
    {
        auto it = std::lower_bound(tags_.begin(), tags_.end(), t);
        if (it != tags_.end() && *it == t) tags_.erase(it);
    }

    [[nodiscard]] bool contains(AssetTag t) const noexcept
    {
        return std::binary_search(tags_.begin(), tags_.end(), t);
    }

    [[nodiscard]] std::size_t size() const noexcept { return tags_.size(); }

    [[nodiscard]] const std::vector<AssetTag>& tags() const noexcept { return tags_; }

    void clear() noexcept { tags_.clear(); }

private:
    std::vector<AssetTag> tags_;
};

}  // namespace cd::asset
