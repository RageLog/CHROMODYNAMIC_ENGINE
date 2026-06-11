// =============================================================================
// CHROMODYNAMIC — cd/concurrency/WorkStealingDeque.hpp
// ADR-015 + ADR-017 P3
//
// Chase-Lev single-producer / multi-consumer work-stealing deque.
//
//   Owner thread:    push() / pop()       — bottom-end operations, no CAS
//   Thief threads:   steal()              — top-end operations, CAS-protected
//
// References:
//   - Chase & Lev, "Dynamic Circular Work-Stealing Deque", SPAA 2005
//   - Le, Pop, Cohen, Nardelli, "Correct and Efficient Work-Stealing for
//     Weak Memory Models", PPoPP 2013 (the canonical acquire/release version)
//
// v1 (S2.5): single-resize doubling on growth, old buffers retained in
//            a vector for safe access by in-flight thieves (no hazard ptrs).
//            Acceptable for engine-lifecycle pools; a leak-free reclamation
//            scheme arrives with HazardPtr (next S2.5 item).
//
// Notes:
//   - T must be trivially copyable or std::atomic-compatible (we store T in
//     std::atomic<T>). For non-trivial types, wrap pointers (T*).
//   - Empty() returning false is advisory; another thread may steal between
//     the check and the next steal()/pop().
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace cd::concurrency
{

enum class StealStatus : std::uint8_t
{
    Success,  ///< Returned value is meaningful.
    Empty,    ///< Deque was observed empty.
    Abort,    ///< CAS lost a race with another thief; caller should retry/backoff.
};

template <class T>
class WorkStealingDeque
{
    static_assert(
        std::is_trivially_copyable_v<T>,
        "WorkStealingDeque<T> requires a trivially copyable T (use T* for objects)"
    );

    struct Array
    {
        explicit Array(std::size_t cap)
            : capacity { cap }
            , mask { cap - 1 }
            , slots { std::make_unique<std::atomic<T>[]>(cap) }
        {
        }

        std::size_t capacity;
        std::size_t mask;
        std::unique_ptr<std::atomic<T>[]> slots;

        [[nodiscard]] T load(std::int64_t i) const noexcept
        {
            return slots[static_cast<std::size_t>(i) & mask].load(std::memory_order_relaxed);
        }

        void store(std::int64_t i, T v) noexcept
        {
            slots[static_cast<std::size_t>(i) & mask].store(v, std::memory_order_relaxed);
        }
    };

public:
    explicit WorkStealingDeque(std::size_t initial_capacity = 64)
    {
        initial_capacity = std::max<size_t>(initial_capacity, 4);
        // Round up to power-of-two for cheap masking.
        std::size_t cap = 1;
        while (cap < initial_capacity)
            cap <<= 1u;
        auto* a = new Array(cap);
        arrays_.emplace_back(a);
        array_.store(a, std::memory_order_relaxed);
    }

    ~WorkStealingDeque()
    {
        for (auto* p : arrays_)
        {
            delete p;
        }
    }

    WorkStealingDeque(const WorkStealingDeque&) = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;
    WorkStealingDeque(WorkStealingDeque&&) = delete;
    WorkStealingDeque& operator=(WorkStealingDeque&&) = delete;

    /// OWNER-only. Push to bottom. Grows the deque if necessary.
    void push(T value)
    {
        const auto b = bottom_.load(std::memory_order_relaxed);
        const auto t = top_.load(std::memory_order_acquire);
        Array* a = array_.load(std::memory_order_relaxed);
        if (b - t >= static_cast<std::int64_t>(a->capacity) - 1)
        {
            a = grow(a, b, t);
        }
        a->store(b, value);
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
    }

    /// OWNER-only. Try to pop from bottom (LIFO for owner). Returns nullopt if
    /// the deque is empty.
    std::optional<T> pop()
    {
        const auto b = bottom_.load(std::memory_order_relaxed) - 1;
        Array* a = array_.load(std::memory_order_relaxed);
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        auto t = top_.load(std::memory_order_relaxed);
        if (t <= b)
        {
            // Non-empty.
            auto x = a->load(b);
            if (t == b)
            {
                // Last element — race with thieves.
                if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
                {
                    bottom_.store(b + 1, std::memory_order_relaxed);
                    return std::nullopt;
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
            return x;
        }
        // Empty.
        bottom_.store(b + 1, std::memory_order_relaxed);
        return std::nullopt;
    }

    /// THIEF. Try to steal from top. Returns Success/Empty/Abort.
    /// On Abort the caller should yield and try again (CAS race lost).
    StealStatus steal(T& out)
    {
        auto t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const auto b = bottom_.load(std::memory_order_acquire);
        if (t >= b)
        {
            return StealStatus::Empty;
        }
        Array* a = array_.load(std::memory_order_consume);
        auto x = a->load(t);
        if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {
            return StealStatus::Abort;
        }
        out = x;
        return StealStatus::Success;
    }

    /// Advisory snapshot. May race with concurrent push/pop/steal.
    [[nodiscard]] std::size_t approx_size() const noexcept
    {
        const auto b = bottom_.load(std::memory_order_relaxed);
        const auto t = top_.load(std::memory_order_relaxed);
        return b > t ? static_cast<std::size_t>(b - t) : 0;
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return array_.load(std::memory_order_relaxed)->capacity;
    }

private:
    Array* grow(Array* old_a, std::int64_t b, std::int64_t t)
    {
        auto* new_a = new Array(old_a->capacity * 2);
        for (auto i = t; i < b; ++i)
        {
            new_a->store(i, old_a->load(i));
        }
        // Retain the old buffer for in-flight thieves (leaks on shutdown only).
        arrays_.emplace_back(new_a);
        array_.store(new_a, std::memory_order_release);
        return new_a;
    }

    alignas(64) std::atomic<std::int64_t> top_ { 0 };
    alignas(64) std::atomic<std::int64_t> bottom_ { 0 };
    alignas(64) std::atomic<Array*> array_ { nullptr };
    std::vector<Array*> arrays_;  ///< Buffers we own, freed on destruction.
};

}  // namespace cd::concurrency
