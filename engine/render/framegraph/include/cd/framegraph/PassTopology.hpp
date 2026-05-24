// =============================================================================
// CHROMODYNAMIC — cd/framegraph/PassTopology.hpp
// Phase 42.B / Wave 210 — DAG topo sort for framegraph passes.
//
// Many engine layers (framegraph, job scheduler, asset cook) build a
// dependency DAG of "pass N runs after passes M, K, …" and need a
// **linear execution order** that respects every edge. Kahn's algorithm
// is the textbook approach:
//
//   1. Compute in-degree of every node.
//   2. Push all in-degree-0 nodes into the ready queue.
//   3. Pop node, append to output, decrement in-degree of successors;
//      any newly-zero successor goes into the ready queue.
//   4. If output is shorter than node count → cycle.
//
// The implementation is intentionally generic: nodes are `std::uint32_t`
// IDs, edges are `std::vector<std::pair<from, to>>`. Caller is free
// to dictate tie-breaking by ordering successor lookups (we use stable
// "first-pushed-first-popped" order — std::queue, FIFO).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::framegraph
{

struct TopoResult
{
    std::vector<std::uint32_t> order;
    bool                       has_cycle { false };
};

[[nodiscard]] inline TopoResult topo_sort(
    std::uint32_t node_count,
    const std::vector<std::pair<std::uint32_t, std::uint32_t>>& edges)
{
    std::vector<std::uint32_t> in_degree(node_count, 0);
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> succ;
    for (auto [from, to] : edges)
    {
        if (from >= node_count || to >= node_count) continue;
        ++in_degree[to];
        succ[from].push_back(to);
    }

    std::queue<std::uint32_t> ready;
    for (std::uint32_t i = 0; i < node_count; ++i)
        if (in_degree[i] == 0) ready.push(i);

    TopoResult out;
    out.order.reserve(node_count);
    while (!ready.empty())
    {
        const auto n = ready.front();
        ready.pop();
        out.order.push_back(n);
        auto it = succ.find(n);
        if (it == succ.end()) continue;
        for (auto v : it->second)
        {
            if (--in_degree[v] == 0) ready.push(v);
        }
    }
    out.has_cycle = (out.order.size() != node_count);
    return out;
}

}  // namespace cd::framegraph
