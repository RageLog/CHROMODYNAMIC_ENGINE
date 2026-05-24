// =============================================================================
// CHROMODYNAMIC — cd/asset/HotReloadQueue.hpp
// Phase 54.B / Wave 222 — coalesced reload notifications by AssetId.
//
// FileWatcher fires raw "file changed" callbacks every kernel event.
// During text-editor save churn this can hit dozens of events per
// second per file. HotReloadQueue collapses them into a per-asset
// set with at-most-one pending notification.
//
//   * `notify(id)` — mark this asset dirty.
//   * `drain(fn)` — invoke `fn(id)` for each currently-dirty asset,
//                   then clear the set. Caller batches reloads at
//                   a frame boundary or end-of-stream.
//
// Unordered set storage; iteration order is unspecified — caller must
// not depend on it. AssetId stored as raw uint64 to avoid the std::hash
// specialization requirement.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>

#include <cstddef>
#include <unordered_set>

namespace cd::asset
{

class HotReloadQueue
{
public:
    void notify(AssetId id)
    {
        if (id.is_valid()) dirty_.insert(id.value());
    }

    [[nodiscard]] bool is_pending(AssetId id) const noexcept
    {
        return dirty_.find(id.value()) != dirty_.end();
    }

    template <class F>
    void drain(F&& fn)
    {
        for (auto v : dirty_) fn(AssetId { v });
        dirty_.clear();
    }

    [[nodiscard]] std::size_t pending_count() const noexcept { return dirty_.size(); }

    void clear() noexcept { dirty_.clear(); }

private:
    std::unordered_set<std::uint64_t> dirty_;
};

}  // namespace cd::asset
