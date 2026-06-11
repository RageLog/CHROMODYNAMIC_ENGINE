// =============================================================================
// CHROMODYNAMIC — cd/mem/TrackingAllocator.hpp
// ADR-005 §B Memory Budget (soft + tracking) + ADR-013 §B.2 Memory Tracking
//
// Decorator that wraps any IAllocator-derived backend and tracks live bytes,
// peak bytes, allocation count, and a per-instance tag. Zero-cost when
// disabled via compile-time flag CD_MEM_TRACKING=OFF.
// =============================================================================
#pragma once

#include <cd/mem/IAllocator.hpp>

#include <atomic>
#include <cstdint>
#include <string_view>

namespace cd::mem
{

#if !defined(CD_MEM_TRACKING)
    #define CD_MEM_TRACKING 1
#endif

/// Atomic stats payload shared by TrackingAllocator instances with the same
/// tag (e.g., all allocators tagged "Renderer" report into one Stats).
struct AllocStats
{
    std::atomic<std::uint64_t> live_bytes { 0 };
    std::atomic<std::uint64_t> peak_bytes { 0 };
    std::atomic<std::uint64_t> alloc_count { 0 };
    std::atomic<std::uint64_t> free_count { 0 };
    std::atomic<std::uint64_t> failure_count { 0 };

    void record_alloc(std::size_t n) noexcept
    {
        const auto current = live_bytes.fetch_add(n, std::memory_order_relaxed) + n;
        std::uint64_t old_peak = peak_bytes.load(std::memory_order_relaxed);
        while (current > old_peak && !peak_bytes.compare_exchange_weak(old_peak, current, std::memory_order_relaxed))
        {
            // CAS retry loop.
        }
        alloc_count.fetch_add(1, std::memory_order_relaxed);
    }

    void record_free(std::size_t n) noexcept
    {
        live_bytes.fetch_sub(n, std::memory_order_relaxed);
        free_count.fetch_add(1, std::memory_order_relaxed);
    }

    void record_failure() noexcept
    {
        failure_count.fetch_add(1, std::memory_order_relaxed);
    }

    // Snapshot (non-atomic) for reporting.
    struct Snapshot
    {
        std::uint64_t live_bytes;
        std::uint64_t peak_bytes;
        std::uint64_t alloc_count;
        std::uint64_t free_count;
        std::uint64_t failure_count;
    };

    [[nodiscard]] Snapshot snapshot() const noexcept
    {
        return Snapshot {
            live_bytes.load(std::memory_order_relaxed),    peak_bytes.load(std::memory_order_relaxed),
            alloc_count.load(std::memory_order_relaxed),   free_count.load(std::memory_order_relaxed),
            failure_count.load(std::memory_order_relaxed),
        };
    }
};

class TrackingAllocator final : public IAllocator
{
public:
    /// `inner` must outlive this decorator. `stats` is optional (if null, an
    /// internal Stats is owned). `tag` is a stable string view for log/profile.
    explicit TrackingAllocator(IAllocator& inner, std::string_view tag = "", AllocStats* stats = nullptr) noexcept
        : inner_ { &inner }
        , stats_ { stats != nullptr ? stats : &owned_stats_ }
        , tag_ { tag }
    {
    }

    [[nodiscard]] void* do_allocate(std::size_t size, std::size_t alignment) noexcept override
    {
        void* p = inner_->allocate(size, alignment);
#if CD_MEM_TRACKING
        if (p != nullptr)
        {
            stats_->record_alloc(size);
            // Stash size in a header for accurate free accounting.
            // To keep API simple, we use a side map alternative: when CD_MEM_TRACKING
            // is on, we only track success/failure counts and bytes via a parallel
            // path (size at free is unknown without a header). v1 design: caller
            // pairs allocate/deallocate, no header overhead; bytes counter is
            // therefore an upper-bound estimate. Sprint S2.1.b document this.
        }
        else if (size != 0)
        {
            stats_->record_failure();
        }
#endif
        return p;
    }

    void deallocate(void* ptr) noexcept override
    {
#if CD_MEM_TRACKING
        if (ptr != nullptr)
        {
            stats_->record_free(0);  // size unknown without header (see allocate note)
        }
#endif
        inner_->deallocate(ptr);
    }

    [[nodiscard]] std::size_t bytes_in_use() const noexcept override
    {
        return inner_->bytes_in_use();
    }

    [[nodiscard]] std::size_t capacity() const noexcept override
    {
        return inner_->capacity();
    }

    [[nodiscard]] const AllocStats& stats() const noexcept
    {
        return *stats_;
    }

    [[nodiscard]] std::string_view tag() const noexcept
    {
        return tag_;
    }

    [[nodiscard]] IAllocator& inner() const noexcept
    {
        return *inner_;
    }

private:
    IAllocator* inner_;
    AllocStats* stats_;
    AllocStats owned_stats_ {};
    std::string_view tag_;
};

}  // namespace cd::mem
