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
// v2 (phase1080, X1-FU-C): OPTIONAL hazard-pointer reclamation. When the
//            owner wires a HazardDomain (set_hazard_domain) and an
//            owner-side ThreadCache (set_owner_cache), grow() RETIRES the
//            outgrown buffer through the domain instead of retaining it;
//            thieves that pass their ThreadCache to steal() protect the
//            array pointer for the load+CAS window (Michael 2004
//            publish-then-reverify). Without the wiring the v1 retention
//            path is byte-identical — standalone consumers keep working
//            with zero new dependencies at runtime.
//            Lifetime rule: the domain must outlive the deque, and every
//            ThreadCache must be destroyed before the domain (the pool
//            guarantees both by declaration order + join-before-destroy).
//
// Notes:
//   - T must be trivially copyable or std::atomic-compatible (we store T in
//     std::atomic<T>). For non-trivial types, wrap pointers (T*).
//   - Empty() returning false is advisory; another thread may steal between
//     the check and the next steal()/pop().
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/concurrency/HazardPtr.hpp>
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

    /// Hazard-pointer reclamation domain (X1-FU-C). One slot per thief is
    /// enough (K = 1): a thief protects exactly the array pointer.
    using HazardDomainT = HazardDomain<1>;

    /// OWNER-only, before concurrent use: enables hazard reclamation.
    void set_hazard_domain(HazardDomainT* domain) noexcept
    {
        hazard_domain_ = domain;
    }

    /// OWNER-only: the owner thread's cache, used by grow() to retire
    /// outgrown buffers. Must belong to the same domain.
    void set_owner_cache(HazardDomainT::ThreadCache* cache) noexcept
    {
        owner_cache_ = cache;
    }

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
    /// `cache` (optional): the thief's hazard ThreadCache. When the owner
    /// wired a domain, passing it protects the array pointer for the
    /// load+CAS window so grow() can RETIRE old buffers instead of
    /// retaining them. Thieves of a domain-wired deque MUST pass a cache;
    /// the legacy nullptr path is only safe under v1 retention.
    StealStatus steal(T& out, HazardDomainT::ThreadCache* cache = nullptr)
    {
        auto t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const auto b = bottom_.load(std::memory_order_acquire);
        if (t >= b)
        {
            return StealStatus::Empty;
        }
        Array* a = nullptr;
        const bool guarded = (cache != nullptr) && (hazard_domain_ != nullptr);
        if (guarded)
        {
            // Michael 2004: publish hazard, re-verify source. After this
            // returns, scan() cannot free `a` until clear() below.
            a = cache->protect(array_, 0);
        }
        else
        {
            a = array_.load(std::memory_order_consume);
        }
        auto x = a->load(t);
        if (!top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {
            if (guarded)
                cache->clear(0);
            return StealStatus::Abort;
        }
        if (guarded)
            cache->clear(0);
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
        if (owner_cache_ != nullptr && hazard_domain_ != nullptr)
        {
            // X1-FU-C: hand the outgrown buffer to hazard reclamation —
            // freed by a later scan() once no thief protects it. We no
            // longer own it, so drop it from the dtor list.
            std::erase(arrays_, old_a);
            arrays_.emplace_back(new_a);
            array_.store(new_a, std::memory_order_release);
            owner_cache_->retire(old_a);
            return new_a;
        }
        // v1 retention: keep the old buffer alive for in-flight thieves
        // (freed at deque destruction only).
        arrays_.emplace_back(new_a);
        array_.store(new_a, std::memory_order_release);
        return new_a;
    }

    alignas(64) std::atomic<std::int64_t> top_ { 0 };
    alignas(64) std::atomic<std::int64_t> bottom_ { 0 };
    alignas(64) std::atomic<Array*> array_ { nullptr };
    std::vector<Array*> arrays_;  ///< Buffers we own, freed on destruction.
    HazardDomainT* hazard_domain_ { nullptr };            ///< X1-FU-C wiring
    HazardDomainT::ThreadCache* owner_cache_ { nullptr };  ///< owner thread's
};

}  // namespace cd::concurrency
