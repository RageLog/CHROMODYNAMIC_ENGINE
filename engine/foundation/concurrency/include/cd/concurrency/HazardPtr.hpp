// =============================================================================
// CHROMODYNAMIC — cd/concurrency/HazardPtr.hpp
// ADR-015 + ADR-017 P3 (Sprint S2.5)
//
// Hazard-pointer reclamation à la Michael (2004), with the modern Folly/JEMA
// refinements:
//
//   * A HazardDomain holds a singly-linked list of HazardSlot nodes. Threads
//     "acquire" a slot (claim a free node or insert a new one) and "release"
//     it back to the free list on destruction.
//   * To protect a pointer the caller writes the pointer into a slot, then
//     **re-loads** the atomic source to confirm no concurrent retire happened
//     between load and write. Failure → retry.
//   * retire(ptr, deleter) parks (ptr, deleter) on a per-thread retire list.
//     When the list exceeds a threshold, scan() walks ALL hazard slots, sets
//     up a hash, and frees every retired pointer not currently protected.
//
// Use case in this engine:
//   * WorkStealingDeque buffer reclamation on grow (currently retains).
//   * Lock-free hash maps (asset registry, resource cache) — later sprints.
//
// Notes:
//   * Per-thread state lives in `thread_local` slot+retire-list pointers,
//     scoped to a domain via small thread-cache structures (HazardThreadCache).
//   * Slots are never deleted; they're recycled. This bounds memory by
//     `(distinct threads ever active) × kSlotsPerThread`.
//   * K (slots per thread) is a template parameter — enough for typical
//     lock-free algorithms (CL deque uses 1, ABA-free linked stacks 2).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace cd::concurrency
{

template <std::size_t K = 2>
class HazardDomain
{
public:
    static_assert(K >= 1 && K <= 8, "K must be in [1, 8]");

    struct Slot
    {
        std::array<std::atomic<void*>, K> ptrs {};
        std::atomic<bool> in_use { false };
        std::atomic<Slot*> next { nullptr };

        Slot() noexcept
        {
            for (auto& p : ptrs)
                p.store(nullptr, std::memory_order_relaxed);
        }
    };

    struct Retired
    {
        void* ptr;
        void (*deleter)(void*);
    };

    /// Per-thread cache. RAII: acquires a Slot on first use, releases on dtor.
    /// Owns a small retire list which is drained back to the domain on dtor.
    class ThreadCache
    {
    public:
        explicit ThreadCache(HazardDomain& domain) noexcept
            : domain_ { &domain }
        {
        }

        ~ThreadCache()
        {
            flush_retired();
            if (slot_)
            {
                for (auto& p : slot_->ptrs)
                    p.store(nullptr, std::memory_order_release);
                slot_->in_use.store(false, std::memory_order_release);
                slot_ = nullptr;
            }
        }

        ThreadCache(const ThreadCache&) = delete;
        ThreadCache& operator=(const ThreadCache&) = delete;
        ThreadCache(ThreadCache&&) = delete;
        ThreadCache& operator=(ThreadCache&&) = delete;

        Slot& slot()
        {
            if (!slot_)
                slot_ = domain_->acquire_slot();
            return *slot_;
        }

        /// Protect a pointer loaded from `src` in hazard slot `index`. Returns the
        /// safely-protected pointer (nullptr if `src` was null).
        template <class T>
        T* protect(std::atomic<T*>& src, std::size_t index = 0)
        {
            auto& sl = slot();
            T* p = src.load(std::memory_order_acquire);
            while (true)
            {
                sl.ptrs[index].store(static_cast<void*>(p), std::memory_order_release);
                T* check = src.load(std::memory_order_acquire);
                if (check == p)
                    return p;
                p = check;
            }
        }

        void clear(std::size_t index = 0)
        {
            if (slot_)
                slot_->ptrs[index].store(nullptr, std::memory_order_release);
        }

        template <class T>
        void retire(T* ptr)
        {
            if (ptr == nullptr)
                return;
            retired_.push_back(
                Retired { static_cast<void*>(ptr),
                          +[](void* p)
                          {
                              delete static_cast<T*>(p);
                          } }
            );
            if (retired_.size() >= kBatchThreshold)
            {
                domain_->scan(retired_);
            }
        }

        void flush_retired()
        {
            if (retired_.empty())
                return;
            domain_->scan(retired_);
            // Whatever the scan couldn't reclaim now becomes the domain's responsibility.
            domain_->absorb_pending(retired_);
            retired_.clear();
        }

    private:
        static constexpr std::size_t kBatchThreshold = 32;
        HazardDomain* domain_;
        Slot* slot_ { nullptr };
        std::vector<Retired> retired_;
    };

    HazardDomain() noexcept = default;

    ~HazardDomain()
    {
        // Reclaim everything still parked, regardless of hazard state (program
        // shutdown — all threads with ThreadCaches must have been destroyed).
        for (auto& r : pending_)
        {
            r.deleter(r.ptr);
        }
        pending_.clear();
        Slot* cur = head_.load(std::memory_order_acquire);
        while (cur)
        {
            Slot* nx = cur->next.load(std::memory_order_acquire);
            delete cur;
            cur = nx;
        }
    }

    HazardDomain(const HazardDomain&) = delete;
    HazardDomain& operator=(const HazardDomain&) = delete;
    HazardDomain(HazardDomain&&) = delete;
    HazardDomain& operator=(HazardDomain&&) = delete;

    /// Acquire a free slot or allocate a new one (CAS-pushed onto the list).
    Slot* acquire_slot()
    {
        // First try to claim an existing free slot.
        Slot* cur = head_.load(std::memory_order_acquire);
        while (cur)
        {
            bool expected = false;
            if (cur->in_use
                    .compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                for (auto& p : cur->ptrs)
                    p.store(nullptr, std::memory_order_relaxed);
                return cur;
            }
            cur = cur->next.load(std::memory_order_acquire);
        }
        // None free — allocate and CAS-link to head.
        auto* sl = new Slot();
        sl->in_use.store(true, std::memory_order_relaxed);
        Slot* head = head_.load(std::memory_order_acquire);
        do
        {
            sl->next.store(head, std::memory_order_relaxed);
        }
        while (!head_.compare_exchange_weak(head, sl, std::memory_order_acq_rel, std::memory_order_acquire));
        slot_count_.fetch_add(1, std::memory_order_relaxed);
        return sl;
    }

    /// Walk all slots, collect protected pointers, then drop reclaimable ones
    /// from `retired_in_out`. Reclaimable entries are deleted; the rest are
    /// shrunk back into `retired_in_out` for a later attempt.
    void scan(std::vector<Retired>& retired_in_out)
    {
        if (retired_in_out.empty())
            return;

        std::unordered_set<void*> hazards;
        hazards.reserve(64);
        for (Slot* s = head_.load(std::memory_order_acquire); s != nullptr; s = s->next.load(std::memory_order_acquire))
        {
            for (auto& p : s->ptrs)
            {
                void* pv = p.load(std::memory_order_acquire);
                if (pv != nullptr)
                    hazards.insert(pv);
            }
        }

        std::vector<Retired> kept;
        kept.reserve(retired_in_out.size());
        std::size_t freed = 0;
        for (auto& r : retired_in_out)
        {
            if (hazards.find(r.ptr) == hazards.end())
            {
                r.deleter(r.ptr);
                ++freed;
            }
            else
            {
                kept.push_back(r);
            }
        }
        retired_in_out.swap(kept);
        reclaimed_total_.fetch_add(freed, std::memory_order_relaxed);
    }

    /// Pull `retired` into the domain's persistent pending list (thread is
    /// going away). Future scans (driven by other threads) may reclaim them.
    void absorb_pending(std::vector<Retired>& retired)
    {
        std::scoped_lock guard { pending_mutex_ };
        pending_.insert(pending_.end(), retired.begin(), retired.end());
        retired.clear();
    }

    /// Force a domain-wide scan: pulls all pending + locally requested retired
    /// pointers and tries to free them.
    void try_reclaim()
    {
        std::scoped_lock guard { pending_mutex_ };
        scan(pending_);
    }

    [[nodiscard]] std::size_t slot_count() const noexcept
    {
        return slot_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t reclaimed_total() const noexcept
    {
        return reclaimed_total_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t pending_count() const
    {
        std::scoped_lock guard { pending_mutex_ };
        return pending_.size();
    }

private:
    std::atomic<Slot*> head_ { nullptr };
    std::atomic<std::size_t> slot_count_ { 0 };
    std::atomic<std::uint64_t> reclaimed_total_ { 0 };

    mutable std::mutex pending_mutex_;
    std::vector<Retired> pending_;
};

}  // namespace cd::concurrency
