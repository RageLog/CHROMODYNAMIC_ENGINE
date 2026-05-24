// =============================================================================
// CHROMODYNAMIC — cd/asset/LoadProfile.hpp
// Phase 49.B / Wave 217 — per-asset load timing accumulator.
//
// Editor + dev tools want a histogram of "how long is each asset
// taking to load." LoadProfile records (id, duration_us) samples and
// exposes:
//
//   * `record(id, micros)` — append.
//   * `total_us(id)` / `count(id)` / `mean_us(id)` — per-asset stats.
//   * `slowest(n)` — top-N slowest individual samples.
//
// Aggregated stats are O(1) on insert (running sum + count in the
// stats map). The slowest-N query is O(n) scan; n is the *sample
// count*, not asset count, so it's a frame-budget concern only when
// telemetry is collecting at high rate.
// =============================================================================
#pragma once

#include <cd/asset/AssetId.hpp>
#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cd::asset
{

struct LoadSample
{
    AssetId       id;
    std::uint64_t duration_us { 0 };
};

class LoadProfile
{
public:
    void record(AssetId id, std::uint64_t duration_us)
    {
        samples_.push_back(LoadSample { id, duration_us });
        auto& s = stats_[id.value()];
        s.total_us += duration_us;
        ++s.count;
    }

    [[nodiscard]] std::uint64_t total_us(AssetId id) const noexcept
    {
        auto it = stats_.find(id.value());
        return (it != stats_.end()) ? it->second.total_us : 0u;
    }

    [[nodiscard]] std::uint32_t count(AssetId id) const noexcept
    {
        auto it = stats_.find(id.value());
        return (it != stats_.end()) ? it->second.count : 0u;
    }

    [[nodiscard]] std::uint64_t mean_us(AssetId id) const noexcept
    {
        auto it = stats_.find(id.value());
        if (it == stats_.end() || it->second.count == 0) return 0u;
        return it->second.total_us / it->second.count;
    }

    [[nodiscard]] std::vector<LoadSample> slowest(std::size_t n) const
    {
        std::vector<LoadSample> sorted = samples_;
        std::sort(sorted.begin(), sorted.end(),
            [](const LoadSample& a, const LoadSample& b)
            { return a.duration_us > b.duration_us; });
        if (sorted.size() > n) sorted.resize(n);
        return sorted;
    }

    [[nodiscard]] std::size_t sample_count() const noexcept { return samples_.size(); }

    void clear() noexcept
    {
        samples_.clear();
        stats_.clear();
    }

private:
    struct Stat
    {
        std::uint64_t total_us { 0 };
        std::uint32_t count    { 0 };
    };
    std::vector<LoadSample>                  samples_;
    std::unordered_map<std::uint64_t, Stat>  stats_;
};

}  // namespace cd::asset
