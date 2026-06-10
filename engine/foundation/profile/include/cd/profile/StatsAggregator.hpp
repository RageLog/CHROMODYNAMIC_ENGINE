// =============================================================================
// CHROMODYNAMIC — cd/profile/StatsAggregator.hpp
//
// Roll up raw `Sample`s into per-name min / avg / max / count. The HUD,
// CSV exporter, and unit-test verifier all consume this aggregation —
// no consumer needs to walk the raw sample buffer.
//
// Aggregation is "wall-clock" — every sample counts the same regardless
// of which frame it came from. A future RolledWindowAggregator could
// drop entries older than N frames; this one is the simplest possible
// thing that buys readable timing output.
//
// Header-only because the implementation is trivial and inlining lets
// the HUD's frame loop call `apply()` without an extra TU dependency.
// =============================================================================
#pragma once

#include <cd/profile/Scope.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cd::profile
{

struct StatRow
{
    std::string name;
    std::uint64_t count { 0 };
    std::uint64_t total_ns { 0 };
    std::uint64_t min_ns { std::numeric_limits<std::uint64_t>::max() };
    std::uint64_t max_ns { 0 };

    [[nodiscard]] double avg_ns() const noexcept
    {
        return count == 0 ? 0.0 : static_cast<double>(total_ns) / static_cast<double>(count);
    }
};

class StatsAggregator
{
public:
    /// Fold a batch of samples into the rollup. Call repeatedly across
    /// frames; the rollup never forgets unless `reset()` is invoked.
    void apply(const std::vector<Sample>& samples)
    {
        for (const auto& s : samples)
        {
            auto& row = rows_[std::string { s.name }];
            row.name = std::string { s.name };
            ++row.count;
            row.total_ns += s.duration_ns;
            row.min_ns = std::min(row.min_ns, s.duration_ns);
            row.max_ns = std::max(row.max_ns, s.duration_ns);
        }
    }

    /// Snapshot the rolled-up rows. Sorted by total time descending so
    /// the hot scopes appear first in any report.
    [[nodiscard]] std::vector<StatRow> snapshot() const
    {
        std::vector<StatRow> out;
        out.reserve(rows_.size());
        for (const auto& [_, row] : rows_)
            out.push_back(row);
        std::ranges::sort(
            out,
            [](const StatRow& a, const StatRow& b)
            {
                return a.total_ns > b.total_ns;
            }
        );
        return out;
    }

    void reset()
    {
        rows_.clear();
    }

    [[nodiscard]] std::size_t row_count() const noexcept
    {
        return rows_.size();
    }

private:
    std::unordered_map<std::string, StatRow> rows_;
};

}  // namespace cd::profile
