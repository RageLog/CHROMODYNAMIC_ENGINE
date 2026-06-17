// =============================================================================
// CHROMODYNAMIC — cd/math/Histogram.hpp
// Phase 62.B / Wave 230 — fixed-bucket frequency counter.
//
// Bins samples into `bucket_count` equal-width buckets over `[min, max]`.
// Out-of-range samples land in the first or last bucket (clamp-to-edge).
//
//   * `record(x)` — increment the bucket for `x`.
//   * `count(i)` — sample count in bucket `i`.
//   * `mode_bucket()` — index of the busiest bucket.
//   * `total()` — total recorded samples.
//
// Use for frame-time histograms, draw-call count distributions,
// telemetry stream visualizations. Partner with `Statistics`
// (Phase 56) for summary stats; histogram visualizes the *shape*.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cd::math
{

class Histogram
{
public:
    Histogram(std::size_t bucket_count, double min_v, double max_v)
        : counts_(bucket_count == 0 ? 1u : bucket_count, 0),
          min_ { min_v },
          max_ { (max_v > min_v) ? max_v : (min_v + 1.0) }
    {
    }

    void record(double x) noexcept
    {
        const double t = (x - min_) / (max_ - min_);
        auto idx = static_cast<std::size_t>(t * static_cast<double>(counts_.size()));
        if (t < 0.0) idx = 0;
        if (idx >= counts_.size()) idx = counts_.size() - 1;
        ++counts_[idx];
        ++total_;
    }

    [[nodiscard]] std::uint64_t count(std::size_t i) const noexcept
    {
        return (i < counts_.size()) ? counts_[i] : 0u;
    }

    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }

    [[nodiscard]] std::size_t bucket_count() const noexcept { return counts_.size(); }

    [[nodiscard]] std::size_t mode_bucket() const noexcept
    {
        std::size_t best = 0;
        std::uint64_t best_n = 0;
        for (std::size_t i = 0; i < counts_.size(); ++i)
        {
            if (counts_[i] > best_n)
            {
                best_n = counts_[i];
                best = i;
            }
        }
        return best;
    }

    void clear() noexcept
    {
        std::ranges::fill(counts_, 0u);
        total_ = 0;
    }

private:
    std::vector<std::uint64_t> counts_;
    double                     min_;
    double                     max_;
    std::uint64_t              total_ { 0 };
};

}  // namespace cd::math
