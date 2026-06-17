// =============================================================================
// CHROMODYNAMIC — cd/asset/DependencyGraph.hpp
// Phase 80.B / Wave 248 — asset dependency edges + reverse lookup.
//
// "Material X uses texture Y and shader Z" — when Y or Z reloads,
// X must invalidate too. `DependencyGraph` stores forward edges
// (parent → child) and lets the asset hot-reload pipeline walk the
// reverse:
//
//   * `depend(parent, child)` — register edge.
//   * `dependents_of(child)` — assets that depend on `child` (1-hop).
//   * `transitive_dependents(child)` — closure across the graph (BFS).
//
// Storage by AssetId::value() (uint64) to avoid std::hash<AssetId>
// requirement.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cd::asset
{

class DependencyGraph
{
public:
    void depend(AssetId parent, AssetId child)
    {
        if (!parent.is_valid() || !child.is_valid()) return;
        reverse_[child.value()].insert(parent.value());
    }

    [[nodiscard]] std::vector<AssetId> dependents_of(AssetId child) const
    {
        std::vector<AssetId> out;
        auto it = reverse_.find(child.value());
        if (it == reverse_.end()) return out;
        out.reserve(it->second.size());
        for (auto v : it->second) out.emplace_back(v);
        return out;
    }

    [[nodiscard]] std::vector<AssetId> transitive_dependents(AssetId child) const
    {
        std::vector<AssetId> out;
        std::unordered_set<std::uint64_t> seen;
        std::queue<std::uint64_t> q;
        q.push(child.value());
        seen.insert(child.value());
        while (!q.empty())
        {
            const auto v = q.front(); q.pop();
            auto it = reverse_.find(v);
            if (it == reverse_.end()) continue;
            for (auto parent : it->second)
            {
                if (seen.insert(parent).second)
                {
                    out.emplace_back(parent);
                    q.push(parent);
                }
            }
        }
        return out;
    }

    void clear() noexcept { reverse_.clear(); }

    [[nodiscard]] std::size_t edge_node_count() const noexcept { return reverse_.size(); }

private:
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> reverse_;
};

}  // namespace cd::asset
