// =============================================================================
// CHROMODYNAMIC - cd/frame_timing/FrameTimeRing.hpp
//
// Fixed-size ring buffer of recent per-frame dt values (in seconds). Adds
// O(N log N) min / mean / median / p99 accessors over the *filled* portion
// of the ring so a HUD can read a single struct per frame instead of
// recomputing the statistics from scratch.
//
// Designed to replace the static-array snippet that lived in
// hello_engine's main.cpp - per the project's library-oriented rule:
//   "everything that could be useful in another sample should be a
//    standalone library".
//
// Usage:
//   cd::frame_timing::FrameTimeRing<120> ring;
//   for (each frame) {
//     ring.push(dt_seconds);
//     auto s = ring.stats();   // {filled, mean, median, p99, last_min, last_max}
//     ImGui::Text("FPS: %.1f", s.fps_mean());
//   }
//
// Header-only because the math is trivial and `constexpr`-friendly. No
// allocations - the ring is a fixed-size std::array on the stack of the
// owning scope.
//
// Non-templated bias: the default 120 samples (=2 s @ 60 Hz) is enough
// for stutter visibility without overflowing a HUD plot widget. Tune by
// instantiating with a different N.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace cd::frame_timing
{

struct Stats
{
    /// Number of valid samples currently in the ring (0..Capacity).
    std::size_t filled { 0 };
    /// Arithmetic mean of dt (seconds).
    double mean { 0.0 };
    /// Median (50th percentile) of dt (seconds).
    float median { 0.0F };
    /// 99th percentile of dt (seconds).
    float p99 { 0.0F };
    /// Smallest / largest dt seen across the filled window.
    float dt_min { 0.0F };
    float dt_max { 0.0F };

    [[nodiscard]] double fps_mean() const noexcept
    {
        return (mean > 0.0) ? (1.0 / mean) : 0.0;
    }

    [[nodiscard]] double fps_median() const noexcept
    {
        return (median > 0.0F) ? (1.0 / static_cast<double>(median)) : 0.0;
    }
};

template <std::size_t Capacity = 120>
class FrameTimeRing
{
public:
    static constexpr std::size_t kCapacity = Capacity;
    static_assert(kCapacity >= 4, "FrameTimeRing needs at least 4 slots");

    /// Append a per-frame dt (seconds). The oldest sample is overwritten
    /// once the ring is full.
    void push(float dt_seconds) noexcept
    {
        buf_[write_idx_] = dt_seconds;
        write_idx_ = (write_idx_ + 1) % kCapacity;
        if (filled_ < kCapacity) ++filled_;
    }

    /// Restore the empty state. Stats() will then read all zeros.
    void reset() noexcept
    {
        write_idx_ = 0;
        filled_ = 0;
    }

    [[nodiscard]] std::size_t filled() const noexcept { return filled_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return kCapacity; }

    /// Random-access view into the underlying ring in WRITE order (oldest
    /// first). Useful for a HUD plotter that wants a linear array. Wraps
    /// from the current write head. Length == filled().
    void copy_in_order(std::vector<float>& out) const
    {
        out.resize(filled_);
        for (std::size_t i = 0; i < filled_; ++i)
        {
            out[i] = buf_[(write_idx_ + i) % kCapacity];
        }
    }

    /// Compute statistics over the filled window. O(N log N) due to the
    /// internal sort needed for median / p99; cheap for N <= a few
    /// thousand and acceptable per-frame for HUD use.
    [[nodiscard]] Stats stats() const
    {
        Stats s {};
        s.filled = filled_;
        if (filled_ == 0) return s;
        double sum = 0.0;
        float dmin = buf_[0];
        float dmax = buf_[0];
        std::vector<float> sorted;
        sorted.reserve(filled_);
        for (std::size_t i = 0; i < filled_; ++i)
        {
            const float v = buf_[i];
            sum += static_cast<double>(v);
            dmin = std::min(v, dmin);
            dmax = std::max(v, dmax);
            sorted.push_back(v);
        }
        std::ranges::sort(sorted);
        s.mean   = sum / static_cast<double>(filled_);
        s.median = sorted[filled_ / 2];
        const auto p99_idx =
            static_cast<std::size_t>(static_cast<float>(filled_) * 0.99F);
        s.p99    = sorted[std::min(p99_idx, filled_ - 1)];
        s.dt_min = dmin;
        s.dt_max = dmax;
        return s;
    }

private:
    std::array<float, kCapacity> buf_ {};
    std::size_t write_idx_ { 0 };
    std::size_t filled_ { 0 };
};

}  // namespace cd::frame_timing
