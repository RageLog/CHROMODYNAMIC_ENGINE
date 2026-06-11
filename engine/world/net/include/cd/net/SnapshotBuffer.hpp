// =============================================================================
// CHROMODYNAMIC — cd/net/SnapshotBuffer.hpp
// Phase 64.A / Wave 232 — timestamped state snapshots with sample().
//
// Multiplayer client interpolation pattern: server sends discrete
// state snapshots at fixed tick rate; client buffers the most recent
// ~3 snapshots and renders one render-time `t` *behind* the newest
// snapshot, interpolating linearly between the two snapshots that
// bracket `t`.
//
// Template on T (the state payload) — typically a small struct of
// position/rotation/velocity. T must support `T + T` and `T * float`.
//
// API:
//   * `push(t, state)` — insert a snapshot at time t (monotonic).
//   * `sample(t)` — interpolated state at t. Returns std::nullopt if
//                    the buffer is empty.
//   * `drop_older_than(t)` — flush snapshots older than t.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <optional>

namespace cd::net
{

template <class T>
class SnapshotBuffer
{
public:
    struct Snapshot
    {
        double t;
        T      state;
    };

    void push(double t, T state)
    {
        // Maintain sorted-by-time order.
        if (snapshots_.empty() || t >= snapshots_.back().t)
        {
            snapshots_.push_back(Snapshot { t, std::move(state) });
            return;
        }
        auto it = std::lower_bound(snapshots_.begin(), snapshots_.end(), t,
            [](const Snapshot& s, double v) { return s.t < v; });
        snapshots_.insert(it, Snapshot { t, std::move(state) });
    }

    [[nodiscard]] std::optional<T> sample(double t) const
    {
        if (snapshots_.empty()) return std::nullopt;
        if (t <= snapshots_.front().t) return snapshots_.front().state;
        if (t >= snapshots_.back().t)  return snapshots_.back().state;
        auto it = std::lower_bound(snapshots_.begin(), snapshots_.end(), t,
            [](const Snapshot& s, double v) { return s.t < v; });
        const auto& b = *it;
        const auto& a = *(it - 1);
        const double span = b.t - a.t;
        if (span <= 0.0) return a.state;
        const auto u = static_cast<float>((t - a.t) / span);
        return a.state * (1.0F - u) + b.state * u;
    }

    void drop_older_than(double t)
    {
        while (!snapshots_.empty() && snapshots_.front().t < t)
            snapshots_.pop_front();
    }

    [[nodiscard]] std::size_t size() const noexcept { return snapshots_.size(); }
    [[nodiscard]] bool empty() const noexcept { return snapshots_.empty(); }

    void clear() noexcept { snapshots_.clear(); }

private:
    std::deque<Snapshot> snapshots_;
};

}  // namespace cd::net
