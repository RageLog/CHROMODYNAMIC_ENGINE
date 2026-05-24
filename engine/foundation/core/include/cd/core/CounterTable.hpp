// =============================================================================
// CHROMODYNAMIC — cd/core/CounterTable.hpp
// Phase 99.B / Wave 267 — named integer counter aggregation.
//
// Quick-and-dirty `name → count` accumulator for engine telemetry:
//   counters.increment("draws_culled");
//   counters.increment("entities_alive", scene.alive_count());
//   for (auto& [name, value] : counters.snapshot()) ...
//
// ProfileSpan (Phase 52) handles timing; CounterTable handles
// integer event counters. Editor inspector reads via `snapshot()`.
//
// Single-threaded; cross-thread aggregation lives elsewhere.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::core
{

class CounterTable
{
public:
    void increment(std::string name, std::int64_t delta = 1)
    {
        counters_[std::move(name)] += delta;
    }

    void set(std::string name, std::int64_t value)
    {
        counters_[std::move(name)] = value;
    }

    [[nodiscard]] std::int64_t get(const std::string& name) const noexcept
    {
        auto it = counters_.find(name);
        return (it != counters_.end()) ? it->second : 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return counters_.size(); }

    [[nodiscard]] std::vector<std::pair<std::string, std::int64_t>> snapshot() const
    {
        std::vector<std::pair<std::string, std::int64_t>> out;
        out.reserve(counters_.size());
        for (const auto& kv : counters_) out.emplace_back(kv.first, kv.second);
        return out;
    }

    void reset() noexcept { counters_.clear(); }

    void reset_all_to_zero() noexcept
    {
        for (auto& kv : counters_) kv.second = 0;
    }

private:
    std::unordered_map<std::string, std::int64_t> counters_;
};

}  // namespace cd::core
