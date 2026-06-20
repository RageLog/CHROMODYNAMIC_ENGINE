// =============================================================================
// CHROMODYNAMIC — cd/profile/StatsAggregator.hpp
//
// Roll up raw `Sample`s into per-name min / avg / max / count / percentile.
// The HUD, CSV exporter, and unit-test verifier all consume this aggregation
// — no consumer needs to walk the raw sample buffer.
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
#include <cmath>
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
    /// Raw duration_ns values in insertion order. Kept so callers can
    /// compute arbitrary percentiles via `percentile_ns()` without
    /// re-walking the raw sample buffer.
    std::vector<std::uint64_t> durations;

    [[nodiscard]] double avg_ns() const noexcept
    {
        return count == 0 ? 0.0 : static_cast<double>(total_ns) / static_cast<double>(count);
    }

    /// Nearest-rank percentile of `durations`. `p` must be in [0.0, 1.0].
    /// Returns 0 for an empty row. The durations vector is sorted on first
    /// call and the sorted copy is kept in `sorted_cache_` — no extra
    /// allocation on repeated queries with the same data set.
    ///
    /// NOTE: mutable because percentile caches a sorted copy lazily.
    [[nodiscard]] std::uint64_t percentile_ns(double p) const
    {
        if (durations.empty())
            return 0ULL;
        // Clamp p to [0,1].
        const double clamped = std::clamp(p, 0.0, 1.0);
        // Rebuild the sorted cache when the duration list has grown.
        if (sorted_cache_.size() != durations.size())
        {
            sorted_cache_ = durations;
            std::ranges::sort(sorted_cache_);
        }
        // Nearest-rank: index = ceil(p * n) - 1, clamped to [0, n-1].
        const std::size_t n = sorted_cache_.size();
        const auto rank = static_cast<std::size_t>(
            std::max(1.0, std::ceil(clamped * static_cast<double>(n)))
        );
        return sorted_cache_[std::min(rank, n) - 1U];
    }

private:
    mutable std::vector<std::uint64_t> sorted_cache_;
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
            row.durations.emplace_back(s.duration_ns);
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
