// =============================================================================
// CHROMODYNAMIC — cd/asset/MemoryCache.hpp
// Phase 43.B / Wave 211 — LRU asset cache budget tracker.
//
// AssetRegistry today caches every loaded asset forever — fine for
// editor preview, dangerous for shipped runtime where a level might
// stream in 100s of textures totalling > VRAM.
//
// `MemoryCache` is the tracking primitive a future `AssetRegistry::evict()`
// pass will consult: it records `(id, bytes)` for each in-flight asset
// and an access timestamp; when the caller asks for `victims(budget_bytes)`
// it returns asset IDs to evict in LRU order until the cumulative
// freed bytes >= the requested amount.
//
// The cache does NOT own asset memory — it observes and dictates
// eviction policy only. The registry is the owner.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::asset
{

class MemoryCache
{
public:
    void touch(AssetId id, std::uint64_t bytes) noexcept
    {
        auto& e = entries_[id.value()];
        if (e.bytes == 0) total_bytes_ += bytes;
        e.bytes = bytes;
        e.last_use = ++clock_;
    }

    void forget(AssetId id) noexcept
    {
        auto it = entries_.find(id.value());
        if (it == entries_.end()) return;
        total_bytes_ -= it->second.bytes;
        entries_.erase(it);
    }

    [[nodiscard]] std::uint64_t total_bytes() const noexcept { return total_bytes_; }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    /// Returns asset IDs in least-recently-used order whose cumulative
    /// bytes ≥ `release_bytes`. Returns the chosen set; caller actually
    /// evicts (calls `forget()` after freeing the GPU side).
    [[nodiscard]] std::vector<AssetId> victims(std::uint64_t release_bytes) const
    {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> sorted;  // (last_use, id)
        sorted.reserve(entries_.size());
        for (const auto& [id, e] : entries_) sorted.emplace_back(e.last_use, id);
        // Ascending last_use → oldest first.
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<AssetId> out;
        std::uint64_t freed = 0;
        for (auto [_, id] : sorted)
        {
            if (freed >= release_bytes) break;
            const auto& e = entries_.at(id);
            freed += e.bytes;
            out.emplace_back(id);
        }
        return out;
    }

private:
    struct Entry
    {
        std::uint64_t bytes { 0 };
        std::uint64_t last_use { 0 };
    };
    std::unordered_map<std::uint64_t, Entry> entries_;
    std::uint64_t                            total_bytes_ { 0 };
    std::uint64_t                            clock_ { 0 };
};

}  // namespace cd::asset
