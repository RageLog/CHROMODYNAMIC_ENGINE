// =============================================================================
// CHROMODYNAMIC — cd/math/Statistics.hpp
// Phase 55.B / Wave 223 — running mean / variance / min / max accumulator.
//
// `Statistics` accumulates samples in one pass:
//   * `add(x)` — Welford's online algorithm for stable variance.
//   * `mean()`, `variance()`, `stddev()`, `min()`, `max()`.
//   * `count()`.
//
// Welford avoids the catastrophic cancellation of the naive
// "sum(x²) - (sum(x))²/n" formula when many samples cluster near a
// non-zero mean.
//
// Used by ProfilerView aggregates, telemetry histograms, frame-time
// jitter reports.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

namespace cd::math
{

class Statistics
{
public:
    void add(double x) noexcept
    {
        ++count_;
        const double delta = x - mean_;
        mean_ += delta / static_cast<double>(count_);
        const double delta2 = x - mean_;
        m2_ += delta * delta2;
        min_ = std::min(x, min_);
        max_ = std::max(x, max_);
    }

    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }

    [[nodiscard]] double mean() const noexcept { return mean_; }

    [[nodiscard]] double variance() const noexcept
    {
        return (count_ < 2) ? 0.0 : m2_ / static_cast<double>(count_ - 1);
    }

    [[nodiscard]] double stddev() const noexcept { return std::sqrt(variance()); }

    [[nodiscard]] double min() const noexcept
    {
        return (count_ == 0) ? 0.0 : min_;
    }

    [[nodiscard]] double max() const noexcept
    {
        return (count_ == 0) ? 0.0 : max_;
    }

    void reset() noexcept
    {
        count_ = 0;
        mean_ = 0.0;
        m2_ = 0.0;
        min_ = std::numeric_limits<double>::infinity();
        max_ = -std::numeric_limits<double>::infinity();
    }

private:
    std::uint64_t count_ { 0 };
    double        mean_ { 0.0 };
    double        m2_ { 0.0 };
    double        min_ { std::numeric_limits<double>::infinity() };
    double        max_ { -std::numeric_limits<double>::infinity() };
};

}  // namespace cd::math
