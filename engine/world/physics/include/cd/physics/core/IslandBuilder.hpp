// =============================================================================
// CHROMODYNAMIC — cd/physics/core/IslandBuilder.hpp
// Disjoint-set (Union-Find) simulation island builder with path compression,
// union-by-rank, and static-body isolation so static bodies never bridge
// independent dynamic islands.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <limits>
#include <vector>

namespace cd::physics::core
{

struct SimulationIsland
{
    std::vector<std::uint32_t> body_indices {};
};

class IslandBuilder
{
public:
    static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

    IslandBuilder() = default;

    explicit IslandBuilder(std::uint32_t body_count)
    {
        reset(body_count);
    }

    void reset(std::uint32_t body_count)
    {
        parent_.resize(body_count);
        rank_.assign(body_count, 0u);
        is_static_.assign(body_count, 0u);
        for (std::uint32_t i = 0u; i < body_count; ++i)
        {
            parent_[i] = i;
        }
    }

    void mark_static(std::uint32_t idx) noexcept
    {
        if (idx < is_static_.size())
        {
            is_static_[idx] = 1u;
        }
    }

    [[nodiscard]] bool is_static(std::uint32_t idx) const noexcept
    {
        return idx < is_static_.size() && is_static_[idx] != 0u;
    }

    [[nodiscard]] std::uint32_t find(std::uint32_t i) noexcept
    {
        if (i >= parent_.size())
        {
            return i;
        }
        std::uint32_t root = i;
        while (root != parent_[root])
        {
            root = parent_[root];
        }
        while (i != root)
        {
            const std::uint32_t next = parent_[i];
            parent_[i]               = root;
            i                        = next;
        }
        return root;
    }

    void unite(std::uint32_t a, std::uint32_t b) noexcept
    {
        if (a >= parent_.size() || b >= parent_.size())
        {
            return;
        }
        if (is_static_[a] != 0u || is_static_[b] != 0u)
        {
            return;
        }

        const std::uint32_t root_a = find(a);
        const std::uint32_t root_b = find(b);
        if (root_a == root_b)
        {
            return;
        }

        if (rank_[root_a] < rank_[root_b])
        {
            parent_[root_a] = root_b;
        }
        else if (rank_[root_a] > rank_[root_b])
        {
            parent_[root_b] = root_a;
        }
        else
        {
            parent_[root_b] = root_a;
            ++rank_[root_a];
        }
    }

    [[nodiscard]] std::vector<SimulationIsland> build_islands()
    {
        const auto                    n = static_cast<std::uint32_t>(parent_.size());
        std::vector<SimulationIsland> islands;
        if (n == 0u)
        {
            return islands;
        }

        std::vector<std::uint32_t> root_to_island(n, kInvalidIndex);
        for (std::uint32_t i = 0u; i < n; ++i)
        {
            if (is_static_[i] != 0u)
            {
                continue;
            }
            const std::uint32_t root = find(i);
            std::uint32_t       idx  = root_to_island[root];
            if (idx == kInvalidIndex)
            {
                idx                  = static_cast<std::uint32_t>(islands.size());
                root_to_island[root] = idx;
                islands.push_back(SimulationIsland {});
            }
            islands[idx].body_indices.push_back(i);
        }

        return islands;
    }

private:
    std::vector<std::uint32_t> parent_ {};
    std::vector<std::uint8_t>  rank_ {};
    std::vector<std::uint8_t>  is_static_ {};
};

}  // namespace cd::physics::core
