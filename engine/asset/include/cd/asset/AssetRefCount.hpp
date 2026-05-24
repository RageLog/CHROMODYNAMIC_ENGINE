// =============================================================================
// CHROMODYNAMIC — cd/asset/AssetRefCount.hpp
// Phase 97.B / Wave 265 — per-asset reference counter.
//
// Asset hot reload safety: caller scopes hold-counts on an asset
// while it's bound to a draw / used by a system. When the count
// reaches zero, the asset can be safely unloaded; while non-zero,
// hot-reload defers (or atomically swaps after acquiring a fresh
// version).
//
// Storage: `unordered_map<AssetId::value(), int32>`. Negative counts
// are treated as zero (defensive); over-release does not underflow.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>

#include <cstdint>
#include <unordered_map>

namespace cd::asset
{

class AssetRefCount
{
public:
    std::int32_t acquire(AssetId id)
    {
        if (!id.is_valid()) return 0;
        auto& c = counts_[id.value()];
        ++c;
        return c;
    }

    std::int32_t release(AssetId id)
    {
        if (!id.is_valid()) return 0;
        auto it = counts_.find(id.value());
        if (it == counts_.end()) return 0;
        if (it->second <= 1)
        {
            counts_.erase(it);
            return 0;
        }
        return --it->second;
    }

    [[nodiscard]] std::int32_t count_of(AssetId id) const noexcept
    {
        if (!id.is_valid()) return 0;
        auto it = counts_.find(id.value());
        return (it == counts_.end()) ? 0 : it->second;
    }

    [[nodiscard]] bool is_held(AssetId id) const noexcept
    {
        return count_of(id) > 0;
    }

    [[nodiscard]] std::size_t tracked_count() const noexcept { return counts_.size(); }

    void clear() noexcept { counts_.clear(); }

private:
    std::unordered_map<std::uint64_t, std::int32_t> counts_;
};

}  // namespace cd::asset
