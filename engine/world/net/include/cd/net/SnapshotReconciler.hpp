// =============================================================================
// CHROMODYNAMIC — cd/net/SnapshotReconciler.hpp
// Phase 468 / M0 wave — server snapshot vs client prediction diff/replay.
//
// `SnapshotReconciler<T>` is the client-side "did I predict right?"
// arbiter. It sits between the network layer (which delivers
// authoritative server snapshots keyed by input-sequence) and the
// local prediction pipeline (which records the state the client
// predicted after each input).
//
// Workflow per tick:
//   1. Local input N applied → client predicts state P_N → caller
//      `record_predicted(N, P_N)`.
//   2. Server snapshot arrives for input M (M ≤ latest predicted) →
//      caller `apply_authoritative(M, S_M, reapply)`.
//   3. Reconciler:
//        a. Looks up its recorded prediction P_M.
//        b. If `equal_fn_(P_M, S_M)` returns true → no correction;
//           bump `agree_count_`.
//        c. Otherwise:
//             * Overwrite P_M with S_M.
//             * Call `reapply(prev_state, seq)` once per recorded
//               prediction with seq in (M, latest], reseeding each
//               recorded entry with the new replay result.
//             * Bump `correction_count_`; record (latest - M) as the
//               error window via `last_correction_window()`.
//
// Snapshots whose input-seq is OLDER than the oldest record (evicted
// from the ring) are treated as "lost training data" and counted under
// `stale_snapshot_count()` — no replay, no fault.
//
// T must be value-copyable. `EqualFn` defaults to `operator==`.
//
// Header-only.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace cd::net
{

template <class T>
class SnapshotReconciler
{
public:
    /// Equality predicate for "client prediction matches server".
    /// Defaults to operator==. Callers needing tolerance (e.g. epsilon
    /// on floats) supply their own.
    using EqualFn = std::function<bool(const T&, const T&)>;

    explicit SnapshotReconciler(std::size_t capacity = 256,
                                EqualFn equal_fn = {})
        : ring_(capacity)
        , equal_fn_ { equal_fn ? std::move(equal_fn)
                               : EqualFn { [](const T& a, const T& b) { return a == b; } } }
    {
    }

    /// Record the client's predicted state at input-sequence `seq`.
    /// Evicts the oldest entry when the ring is full.
    void record_predicted(std::uint32_t seq, T state)
    {
        Entry e;
        e.seq = seq;
        e.state = std::move(state);
        e.valid = true;
        ring_[head_] = std::move(e);
        head_ = (head_ + 1) % ring_.size();
        if (size_ < ring_.size())
            ++size_;
        if (!latest_recorded_.has_value() || seq > *latest_recorded_)
            latest_recorded_ = seq;
    }

    /// Look up a previously recorded prediction. nullopt if not in ring.
    [[nodiscard]] std::optional<T> predicted_at(std::uint32_t seq) const
    {
        for (const auto& e : ring_)
            if (e.valid && e.seq == seq)
                return e.state;
        return std::nullopt;
    }

    /// Result of a single `apply_authoritative` call.
    struct ReconcileResult
    {
        enum class Outcome : std::uint8_t
        {
            kAgree,         // prediction matched, no replay
            kCorrected,     // server differed → replay performed
            kStale,         // snapshot too old / not in ring
        };
        Outcome outcome { Outcome::kAgree };
        std::size_t replayed { 0 };
    };

    /// Apply an authoritative server snapshot at input-seq `seq`.
    /// `reapply` is `T(prev_state, seq)` — the same per-step function
    /// the client uses to advance state from one input to the next.
    template <class Reapply>
    ReconcileResult apply_authoritative(std::uint32_t seq,
                                        T authoritative,
                                        Reapply reapply)
    {
        Entry* slot = find_(seq);
        if (slot == nullptr)
        {
            ++stale_snapshot_count_;
            return { ReconcileResult::Outcome::kStale, 0 };
        }
        if (equal_fn_(slot->state, authoritative))
        {
            ++agree_count_;
            return { ReconcileResult::Outcome::kAgree, 0 };
        }
        // Mismatch → correct + replay.
        slot->state = authoritative;
        std::size_t replayed = 0;
        T cursor = authoritative;
        std::uint32_t cursor_seq = seq;
        while (true)
        {
            const std::uint32_t next_seq = cursor_seq + 1;
            Entry* nxt = find_(next_seq);
            if (nxt == nullptr)
                break;
            cursor = reapply(cursor, next_seq);
            nxt->state = cursor;
            cursor_seq = next_seq;
            ++replayed;
        }
        ++correction_count_;
        last_correction_window_ = replayed;
        return { ReconcileResult::Outcome::kCorrected, replayed };
    }

    [[nodiscard]] std::size_t agree_count() const noexcept { return agree_count_; }
    [[nodiscard]] std::size_t correction_count() const noexcept { return correction_count_; }
    [[nodiscard]] std::size_t stale_snapshot_count() const noexcept
    {
        return stale_snapshot_count_;
    }
    [[nodiscard]] std::size_t last_correction_window() const noexcept
    {
        return last_correction_window_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return ring_.size(); }
    [[nodiscard]] std::optional<std::uint32_t> latest_recorded() const noexcept
    {
        return latest_recorded_;
    }
    void clear() noexcept
    {
        for (auto& e : ring_) e.valid = false;
        head_ = 0;
        size_ = 0;
        latest_recorded_.reset();
    }

private:
    struct Entry
    {
        std::uint32_t seq { 0 };
        T state {};
        bool valid { false };
    };

    [[nodiscard]] Entry* find_(std::uint32_t seq) noexcept
    {
        for (auto& e : ring_)
            if (e.valid && e.seq == seq)
                return &e;
        return nullptr;
    }

    std::vector<Entry> ring_;
    std::size_t head_ { 0 };
    std::size_t size_ { 0 };
    EqualFn equal_fn_;
    std::size_t agree_count_ { 0 };
    std::size_t correction_count_ { 0 };
    std::size_t stale_snapshot_count_ { 0 };
    std::size_t last_correction_window_ { 0 };
    std::optional<std::uint32_t> latest_recorded_ {};
};

}  // namespace cd::net
