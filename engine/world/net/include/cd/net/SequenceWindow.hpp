// =============================================================================
// CHROMODYNAMIC — cd/net/SequenceWindow.hpp
// Phase 39.A / Wave 207 — sliding-window duplicate detection.
//
// UDP packets arrive out-of-order, duplicated, or missing. To deliver
// the application a *clean* stream we keep a small sliding window of
// recently-seen sequence numbers and reject duplicates without
// blocking forward progress.
//
// State:
//   * `latest_` — highest sequence number seen so far.
//   * `mask_`   — bitmask over the previous `N-1` sequence numbers,
//                 where bit `k` represents `latest_ - k`.
//
// `accept(seq)` returns true when `seq` is new (insert + return true)
// and false when `seq` was already seen or is older than the window.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>

namespace cd::net
{

template <std::uint32_t N = 64>
class SequenceWindow
{
    static_assert(N > 0 && N <= 64, "SequenceWindow N in [1,64]");

public:
    /// Returns true iff `seq` had not been seen before and falls
    /// inside the window. False = duplicate or too-old.
    bool accept(std::uint32_t seq) noexcept
    {
        if (!any_seen_)
        {
            latest_ = seq;
            mask_ = 0;
            any_seen_ = true;
            return true;
        }
        if (seq > latest_)
        {
            const std::uint32_t shift = seq - latest_;
            if (shift >= N) mask_ = 0;
            else            mask_ = static_cast<std::uint64_t>(mask_ << shift) | (1ULL << (shift - 1));
            latest_ = seq;
            return true;
        }
        const std::uint32_t age = latest_ - seq;
        if (age >= N) return false;        // too old
        if (age == 0) return false;        // duplicate (same as latest)
        const std::uint64_t bit = 1ULL << (age - 1);
        if (mask_ & bit) return false;     // duplicate (in window)
        mask_ |= bit;
        return true;
    }

    [[nodiscard]] std::uint32_t latest() const noexcept { return latest_; }

    void reset() noexcept { latest_ = 0; mask_ = 0; any_seen_ = false; }

private:
    std::uint32_t latest_ { 0 };
    std::uint64_t mask_ { 0 };
    bool          any_seen_ { false };
};

}  // namespace cd::net
