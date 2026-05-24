// =============================================================================
// CHROMODYNAMIC — cd/ecs/SystemGraph.hpp
// Phase 64.B / Wave 232 — named system DAG with topological execution order.
//
// SystemGraph models the ECS scheduler dependency graph: each system
// has a unique name and depends on zero or more other systems by name.
// `build_order()` returns the resolved execution order, or reports a
// cycle.
//
//   SystemGraph g;
//   g.add("input");
//   g.add("physics",   { "input" });
//   g.add("animation", { "input" });
//   g.add("render",    { "physics", "animation" });
//   auto order = g.build_order();
//
// Kahn's algorithm topo-sort is inlined here (cd::framegraph has the
// same algorithm — duplicated rather than introduce a cross-layer
// dependency from ecs to render).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::ecs
{

class SystemGraph
{
public:
    void add(std::string name, std::vector<std::string> depends_on = {})
    {
        const auto id = static_cast<std::uint32_t>(systems_.size());
        ids_[name] = id;
        systems_.push_back(System { std::move(name), std::move(depends_on) });
    }

    struct BuildResult
    {
        std::vector<std::string> order;
        bool                     has_cycle { false };
    };

    [[nodiscard]] BuildResult build_order() const
    {
        const auto n = static_cast<std::uint32_t>(systems_.size());
        std::vector<std::uint32_t> in_degree(n, 0);
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> succ;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            for (const auto& dep : systems_[i].deps)
            {
                auto it = ids_.find(dep);
                if (it == ids_.end()) continue;   // missing dep ignored
                ++in_degree[i];
                succ[it->second].push_back(i);
            }
        }
        std::queue<std::uint32_t> ready;
        for (std::uint32_t i = 0; i < n; ++i)
            if (in_degree[i] == 0) ready.push(i);

        BuildResult out;
        out.order.reserve(n);
        while (!ready.empty())
        {
            const auto idx = ready.front();
            ready.pop();
            out.order.push_back(systems_[idx].name);
            auto it = succ.find(idx);
            if (it == succ.end()) continue;
            for (auto v : it->second)
                if (--in_degree[v] == 0) ready.push(v);
        }
        out.has_cycle = (out.order.size() != n);
        return out;
    }

    [[nodiscard]] std::size_t size() const noexcept { return systems_.size(); }

    void clear() noexcept { systems_.clear(); ids_.clear(); }

private:
    struct System
    {
        std::string              name;
        std::vector<std::string> deps;
    };
    std::vector<System>                            systems_;
    std::unordered_map<std::string, std::uint32_t> ids_;
};

}  // namespace cd::ecs
