// =============================================================================
// CHROMODYNAMIC — cd/net/PredictionBuffer.hpp
// Phase 18.E / Wave 178 — header-only client-side prediction primitive.
//
// A ring buffer of (sequence, state) snapshots. The client predicts
// future state by replaying inputs against the latest authoritative
// snapshot; when a server correction arrives the buffer rolls back
// to the corrected snapshot and re-replays subsequent inputs.
//
// Used by client-side networking to smooth out server latency:
//   1. Client receives server snapshot at seq N with corrected state.
//   2. Client searches the buffer for its locally-predicted state at
//      seq N. If mismatch, replace with server state, then re-apply
//      every input from seq N+1 .. current.
//
// Type T must be value-copyable (no resource handles); the buffer
// stores copies. Sequence is std::uint32_t; wrap-around at 2^32 is
// the application's concern.
//
// Header-only. Depends on <vector> + <cstdint>.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cd::net
{

template <class T>
class PredictionBuffer
{
public:
    explicit PredictionBuffer(std::size_t capacity = 256)
        : buf_(capacity)
    {
    }

    /// Record a state snapshot at `sequence`. If the buffer is full
    /// the oldest entry is evicted.
    void record(std::uint32_t sequence, T state)
    {
        Entry e;
        e.seq = sequence;
        e.state = std::move(state);
        e.valid = true;
        buf_[head_] = std::move(e);
        head_ = (head_ + 1) % buf_.size();
        if (size_ < buf_.size()) ++size_;
    }

    /// Look up a recorded snapshot by sequence. Returns std::nullopt
    /// if the sequence has been evicted or never recorded.
    [[nodiscard]] std::optional<T> at(std::uint32_t sequence) const
    {
        for (const auto& e : buf_)
            if (e.valid && e.seq == sequence)
                return e.state;
        return std::nullopt;
    }

    /// Apply a server correction: replace the snapshot at
    /// `corrected_seq` with `corrected_state`, then re-replay every
    /// snapshot with seq > corrected_seq through the supplied
    /// `apply` functor. `apply(prev_state, seq) → next_state` runs
    /// once per step.
    ///
    /// Returns the number of snapshots replayed (zero if no
    /// snapshot with seq > corrected_seq existed).
    template <class Apply>
    std::size_t correct_and_replay(std::uint32_t corrected_seq,
                                   T corrected_state,
                                   Apply apply)
    {
        // Replace the corrected entry.
        for (auto& e : buf_)
            if (e.valid && e.seq == corrected_seq)
                e.state = corrected_state;

        // Find subsequent snapshots and replay.
        std::size_t replayed = 0;
        std::uint32_t cursor = corrected_seq;
        T prev = std::move(corrected_state);
        while (true)
        {
            const std::uint32_t next_seq = cursor + 1;
            Entry* found = nullptr;
            for (auto& e : buf_)
                if (e.valid && e.seq == next_seq) { found = &e; break; }
            if (found == nullptr) break;
            prev = apply(prev, next_seq);
            found->state = prev;
            cursor = next_seq;
            ++replayed;
        }
        return replayed;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return buf_.size(); }
    void clear() noexcept
    {
        for (auto& e : buf_) e.valid = false;
        head_ = 0;
        size_ = 0;
    }

private:
    struct Entry
    {
        std::uint32_t seq { 0 };
        T state {};
        bool valid { false };
    };

    std::vector<Entry> buf_;
    std::size_t head_ { 0 };
    std::size_t size_ { 0 };
};

}  // namespace cd::net
