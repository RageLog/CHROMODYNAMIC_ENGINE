// =============================================================================
// CHROMODYNAMIC — cd::mem tests (Sprint S2.1.b)
// =============================================================================
#include <cd/mem/IAllocator.hpp>
#include <cd/mem/LinearAllocator.hpp>
#include <cd/mem/PageAllocator.hpp>
#include <cd/mem/PmrAdapter.hpp>
#include <cd/mem/PoolAllocator.hpp>
#include <cd/mem/TrackingAllocator.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <new>
#include <string>
#include <vector>

namespace
{

// ---------- detail utilities ------------------------------------------------
TEST(MemDetail, AlignUpRoundsToBoundary)
{
    using cd::mem::detail::align_up;
    EXPECT_EQ(align_up(0U, 8U), 0U);
    EXPECT_EQ(align_up(1U, 8U), 8U);
    EXPECT_EQ(align_up(8U, 8U), 8U);    // already aligned → unchanged
    EXPECT_EQ(align_up(9U, 8U), 16U);
    EXPECT_EQ(align_up(4095U, 4096U), 4096U);
    EXPECT_EQ(align_up(4097U, 4096U), 2U * 4096U);
}

TEST(MemDetail, AlignUpIsConstexpr)
{
    // align_up must be usable in a constant expression (constexpr contract).
    constexpr std::size_t kRounded = cd::mem::detail::align_up(13U, 16U);
    static_assert(kRounded == 16U, "align_up must fold at compile time");
    EXPECT_EQ(kRounded, 16U);
}

TEST(MemDetail, IsPowerOfTwo)
{
    using cd::mem::detail::is_power_of_two;
    EXPECT_FALSE(is_power_of_two(0U));  // zero is not a power of two
    EXPECT_TRUE(is_power_of_two(1U));
    EXPECT_TRUE(is_power_of_two(2U));
    EXPECT_FALSE(is_power_of_two(3U));
    EXPECT_TRUE(is_power_of_two(64U));
    EXPECT_FALSE(is_power_of_two(96U));
    EXPECT_TRUE(is_power_of_two(4096U));
}

// ---------- SystemAllocator -------------------------------------------------
TEST(SystemAllocator, BasicAllocateDeallocate)
{
    auto& a = cd::mem::system_allocator();
    void* p = a.allocate(128);
    ASSERT_NE(p, nullptr);
    a.deallocate(p);
}

TEST(SystemAllocator, ZeroSizeReturnsNull)
{
    auto& a = cd::mem::system_allocator();
    EXPECT_EQ(a.allocate(0), nullptr);
}

TEST(SystemAllocator, AlignmentRespected)
{
    auto& a = cd::mem::system_allocator();
    void* p = a.allocate(64, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 64u, 0u);
    a.deallocate(p);
}

// Sweep the documented alignment boundaries 1/8/64/4096; every returned block
// must satisfy the requested alignment and survive a write+read.
TEST(SystemAllocator, AlignmentBoundariesSweep)
{
    auto& a = cd::mem::system_allocator();
    for (std::size_t align : { std::size_t { 1 }, std::size_t { 8 }, std::size_t { 64 }, std::size_t { 4096 } })
    {
        void* p = a.allocate(256, align);
        ASSERT_NE(p, nullptr) << "align=" << align;
        // alignment of 1 has no constraint; >=8 must land on the boundary.
        if (align >= sizeof(void*))
        {
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % align, 0u) << "align=" << align;
        }
        auto* bytes = static_cast<std::byte*>(p);
        bytes[0] = std::byte { 0x5A };
        bytes[255] = std::byte { 0xA5 };
        EXPECT_EQ(bytes[0], std::byte { 0x5A });
        EXPECT_EQ(bytes[255], std::byte { 0xA5 });
        a.deallocate(p);
    }
}

// alignment == 0 means "allocator picks the default" — must still succeed.
TEST(SystemAllocator, ZeroAlignmentUsesDefault)
{
    auto& a = cd::mem::system_allocator();
    void* p = a.allocate(32, 0);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % cd::mem::kDefaultAlignment, 0u);
    a.deallocate(p);
}

// deallocate(nullptr) is a documented no-op (matches std::free).
TEST(SystemAllocator, DeallocateNullIsNoOp)
{
    auto& a = cd::mem::system_allocator();
    a.deallocate(nullptr);  // must not crash
    SUCCEED();
}

// ---------- LinearAllocator -------------------------------------------------
TEST(LinearAllocator, BumpAndReset)
{
    cd::mem::LinearAllocator arena { 1024 };
    EXPECT_EQ(arena.capacity(), 1024u);

    void* a = arena.allocate(100);
    void* b = arena.allocate(100);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b);
    EXPECT_GE(arena.bytes_in_use(), 200u);

    arena.reset();
    EXPECT_EQ(arena.bytes_in_use(), 0u);
    void* c = arena.allocate(100);
    EXPECT_EQ(c, a);  // bump pointer restarts at zero
}

TEST(LinearAllocator, AlignmentRespected)
{
    cd::mem::LinearAllocator arena { 4096 };
    (void)arena.allocate(1);  // misalign on purpose
    void* p = arena.allocate(64, 64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 64u, 0u);
}

TEST(LinearAllocator, OutOfMemoryReturnsNull)
{
    cd::mem::LinearAllocator arena { 128 };
    void* a = arena.allocate(100);
    ASSERT_NE(a, nullptr);
    void* b = arena.allocate(100);  // exceeds capacity
    EXPECT_EQ(b, nullptr);
}

// A request that exactly fills the remaining capacity must succeed; the very
// next byte must fail. Guards the off-by-one in the `> capacity_` check.
TEST(LinearAllocator, ExactCapacityBoundary)
{
    cd::mem::LinearAllocator arena { 256 };
    void* a = arena.allocate(256, 1);  // align 1 → no padding, fills exactly
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(arena.bytes_in_use(), 256u);
    EXPECT_EQ(arena.bytes_remaining(), 0u);
    EXPECT_EQ(arena.allocate(1, 1), nullptr);  // nothing left
}

TEST(LinearAllocator, ZeroSizeReturnsNull)
{
    cd::mem::LinearAllocator arena { 256 };
    EXPECT_EQ(arena.allocate(0), nullptr);
    EXPECT_EQ(arena.bytes_in_use(), 0u);  // failed alloc must not advance the bump pointer
}

// Non-power-of-two alignment is rejected (null) without disturbing the arena.
TEST(LinearAllocator, NonPowerOfTwoAlignmentRejected)
{
    cd::mem::LinearAllocator arena { 256 };
    EXPECT_EQ(arena.allocate(16, 24), nullptr);  // 24 is not a power of two
    EXPECT_EQ(arena.bytes_in_use(), 0u);
}

// bytes_remaining tracks capacity minus the running offset (incl. padding).
TEST(LinearAllocator, BytesRemainingTracksOffset)
{
    cd::mem::LinearAllocator arena { 1024 };
    EXPECT_EQ(arena.bytes_remaining(), 1024u);
    (void)arena.allocate(100, 1);
    EXPECT_EQ(arena.bytes_remaining(), 1024u - 100u);
    arena.reset();
    EXPECT_EQ(arena.bytes_remaining(), 1024u);
}

// deallocate() on a LinearAllocator is an intentional no-op — it must never
// advance/rewind the bump pointer or crash.
TEST(LinearAllocator, DeallocateIsNoOp)
{
    cd::mem::LinearAllocator arena { 256 };
    void* a = arena.allocate(64);
    ASSERT_NE(a, nullptr);
    const std::size_t before = arena.bytes_in_use();
    arena.deallocate(a);  // no-op
    arena.deallocate(nullptr);
    EXPECT_EQ(arena.bytes_in_use(), before);
}

// A backing allocator that fails (capacity 0 pool) yields a dead arena: every
// allocate returns null, no UB. Guards the `block_ == nullptr` branch.
TEST(LinearAllocator, FailedBackingYieldsDeadArena)
{
    cd::mem::PoolAllocator empty { 16, 0 };  // zero-count pool never allocates
    cd::mem::LinearAllocator arena { 1024, &empty };
    EXPECT_EQ(arena.allocate(8), nullptr);
    EXPECT_EQ(arena.bytes_in_use(), 0u);
}

// ---------- PoolAllocator ---------------------------------------------------
TEST(PoolAllocator, FixedBlockRoundTrip)
{
    cd::mem::PoolAllocator pool { 64, 16 };
    EXPECT_EQ(pool.block_size() % alignof(std::max_align_t), 0u);
    EXPECT_EQ(pool.block_count(), 16u);

    std::vector<void*> blocks;
    for (int i = 0; i < 16; ++i)
    {
        void* p = pool.allocate(64);
        ASSERT_NE(p, nullptr) << "block " << i;
        blocks.push_back(p);
    }
    EXPECT_EQ(pool.allocate(64), nullptr);  // exhausted
    EXPECT_EQ(pool.blocks_in_use(), 16u);

    for (void* p : blocks)
    {
        pool.deallocate(p);
    }
    EXPECT_EQ(pool.blocks_in_use(), 0u);
    EXPECT_NE(pool.allocate(64), nullptr);
}

TEST(PoolAllocator, OversizedReturnsNull)
{
    cd::mem::PoolAllocator pool { 64, 4 };
    EXPECT_EQ(pool.allocate(128), nullptr);
}

// A block freed last is handed back first (free-list is a LIFO stack). Pull two
// blocks, free in order a then b; the next two allocs return b then a.
TEST(PoolAllocator, FreeListIsLifo)
{
    cd::mem::PoolAllocator pool { 64, 4 };
    void* a = pool.allocate(64);
    void* b = pool.allocate(64);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    pool.deallocate(a);
    pool.deallocate(b);  // b pushed last → on top of the stack

    void* x = pool.allocate(64);
    void* y = pool.allocate(64);
    EXPECT_EQ(x, b);  // LIFO: last freed comes back first
    EXPECT_EQ(y, a);
}

// Exhaust the pool, free a subset, and confirm exactly that many slots refill.
TEST(PoolAllocator, ExhaustAndRefill)
{
    cd::mem::PoolAllocator pool { 32, 8 };
    std::array<void*, 8> blocks {};
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        blocks.at(i) = pool.allocate(32);
        ASSERT_NE(blocks.at(i), nullptr) << "block " << i;
    }
    EXPECT_EQ(pool.blocks_in_use(), 8u);
    EXPECT_EQ(pool.blocks_free(), 0u);
    EXPECT_EQ(pool.allocate(32), nullptr);  // exhausted

    pool.deallocate(blocks.at(0));
    pool.deallocate(blocks.at(1));
    pool.deallocate(blocks.at(2));
    EXPECT_EQ(pool.blocks_in_use(), 5u);
    EXPECT_EQ(pool.blocks_free(), 3u);

    EXPECT_NE(pool.allocate(32), nullptr);
    EXPECT_NE(pool.allocate(32), nullptr);
    EXPECT_NE(pool.allocate(32), nullptr);
    EXPECT_EQ(pool.allocate(32), nullptr);  // exhausted again
    EXPECT_EQ(pool.blocks_in_use(), 8u);
}

// Every block the pool hands out must honour the requested block alignment.
TEST(PoolAllocator, BlocksAreAligned)
{
    constexpr std::size_t kAlign = 64;
    cd::mem::PoolAllocator pool { 48, 8, kAlign };
    EXPECT_EQ(pool.block_size() % kAlign, 0u);  // size rounded up to alignment
    for (int i = 0; i < 8; ++i)
    {
        void* p = pool.allocate(48, kAlign);
        ASSERT_NE(p, nullptr) << "block " << i;
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % kAlign, 0u) << "block " << i;
    }
}

// Requesting an alignment stronger than the pool's block alignment fails.
TEST(PoolAllocator, OveralignedRequestRejected)
{
    cd::mem::PoolAllocator pool { 64, 4, 16 };
    EXPECT_EQ(pool.allocate(64, 64), nullptr);  // 64 > block_align_ (16)
}

// block_size is bumped up to at least sizeof(FreeNode) so the intrusive link
// fits even when the caller asks for a tiny block.
TEST(PoolAllocator, TinyBlockSizeBumpedToFitLink)
{
    cd::mem::PoolAllocator pool { 1, 4 };  // 1-byte blocks requested
    EXPECT_GE(pool.block_size(), sizeof(void*));
    void* p = pool.allocate(1);
    ASSERT_NE(p, nullptr);
    pool.deallocate(p);  // must round-trip without clobbering adjacent slots
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

// A zero-count pool is inert: no storage, every allocate fails, no UB.
TEST(PoolAllocator, ZeroCountIsInert)
{
    cd::mem::PoolAllocator pool { 64, 0 };
    EXPECT_EQ(pool.block_count(), 0u);
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.allocate(64), nullptr);
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

// A non-power-of-two block alignment is rejected at construction (inert pool).
TEST(PoolAllocator, NonPowerOfTwoAlignmentInert)
{
    cd::mem::PoolAllocator pool { 64, 4, 48 };  // 48 not a power of two
    EXPECT_EQ(pool.allocate(64), nullptr);
}

// deallocate(nullptr) must not corrupt the free list or the in-use counter.
TEST(PoolAllocator, DeallocateNullIsNoOp)
{
    cd::mem::PoolAllocator pool { 64, 4 };
    void* a = pool.allocate(64);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(pool.blocks_in_use(), 1u);
    pool.deallocate(nullptr);  // no-op
    EXPECT_EQ(pool.blocks_in_use(), 1u);
    pool.deallocate(a);
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

// bytes_in_use / capacity report block_size-scaled totals.
TEST(PoolAllocator, BytesAccounting)
{
    cd::mem::PoolAllocator pool { 64, 4 };
    const std::size_t bs = pool.block_size();
    EXPECT_EQ(pool.capacity(), bs * 4u);
    EXPECT_EQ(pool.bytes_in_use(), 0u);
    void* a = pool.allocate(64);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(pool.bytes_in_use(), bs);
    pool.deallocate(a);
    EXPECT_EQ(pool.bytes_in_use(), 0u);
}

// TypedPoolAllocator exhausts after object_count creates, then refills on destroy.
TEST(TypedPoolAllocator, ExhaustReturnsNull)
{
    cd::mem::TypedPoolAllocator<int> pool { 2 };
    int* a = pool.create(1);
    int* b = pool.create(2);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(pool.create(3), nullptr);  // exhausted
    EXPECT_EQ(pool.in_use(), 2u);
    pool.destroy(a);
    int* c = pool.create(4);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(*c, 4);
    pool.destroy(b);
    pool.destroy(c);
}

// destroy() runs the object's destructor exactly once before reclaiming the slot.
TEST(TypedPoolAllocator, DestroyRunsDtor)
{
    struct Counted
    {
        int* counter;

        explicit Counted(int* c) noexcept
            : counter { c }
        {
        }

        ~Counted()
        {
            ++(*counter);
        }
    };

    int dtor_calls = 0;
    cd::mem::TypedPoolAllocator<Counted> pool { 4 };
    auto* obj = pool.create(&dtor_calls);
    ASSERT_NE(obj, nullptr);
    pool.destroy(obj);
    EXPECT_EQ(dtor_calls, 1);
    pool.destroy(nullptr);  // null no-op must not run a dtor
    EXPECT_EQ(dtor_calls, 1);
}

TEST(TypedPoolAllocator, CreateDestroy)
{
    struct Node
    {
        int id;

        explicit Node(int v)
            : id { v }
        {
        }
    };

    cd::mem::TypedPoolAllocator<Node> pool { 8 };
    auto* a = pool.create(42);
    auto* b = pool.create(7);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->id, 42);
    EXPECT_EQ(b->id, 7);
    EXPECT_EQ(pool.in_use(), 2u);
    pool.destroy(a);
    pool.destroy(b);
    EXPECT_EQ(pool.in_use(), 0u);
}

// ---------- PageAllocator ---------------------------------------------------
TEST(PageAllocator, PageSizePositive)
{
    EXPECT_GE(cd::mem::PageAllocator::page_size(), 4096u);
}

TEST(PageAllocator, BasicAllocate)
{
    cd::mem::PageAllocator alloc;
    void* p = alloc.allocate(8192);
    ASSERT_NE(p, nullptr);
    // Write+read to verify the pages are actually committed.
    auto* bytes = static_cast<std::byte*>(p);
    bytes[0] = std::byte { 0xAB };
    bytes[8191] = std::byte { 0xCD };
    EXPECT_EQ(bytes[0], std::byte { 0xAB });
    EXPECT_EQ(bytes[8191], std::byte { 0xCD });
    alloc.deallocate(p);
}

// Records the rounded mapping length so POSIX munmap(ptr, len) reclaims the
// reservation instead of leaking it (the former v1 TODO). The length must be
// the size rounded up to a whole number of pages.
TEST(PageAllocator, MappingLengthRecordedAndRounded)
{
    cd::mem::PageAllocator alloc;
    const std::size_t ps = cd::mem::PageAllocator::page_size();

    // Request one byte past a page boundary → rounds up to two pages.
    void* p = alloc.allocate(ps + 1U);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(alloc.mapping_length(p), 2U * ps);
    EXPECT_EQ(alloc.live_mapping_count(), 1U);

    alloc.deallocate(p);
    // After free the side-table no longer knows the pointer.
    EXPECT_EQ(alloc.mapping_length(p), 0U);
    EXPECT_EQ(alloc.live_mapping_count(), 0U);
}

// Allocate and free many pages; the live-mapping side-table must balance back
// to zero (no leak, no length lost) — this is the regression guard for the
// POSIX munmap length fix.
TEST(PageAllocator, ManyPagesNoLeak)
{
    cd::mem::PageAllocator alloc;
    constexpr std::size_t kCount = 64;

    std::vector<void*> pages;
    pages.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i)
    {
        void* p = alloc.allocate(4096);
        ASSERT_NE(p, nullptr) << "page " << i;
        EXPECT_GT(alloc.mapping_length(p), 0U) << "page " << i;
        pages.push_back(p);
    }
    EXPECT_EQ(alloc.live_mapping_count(), kCount);

    for (void* p : pages)
    {
        alloc.deallocate(p);
    }
    EXPECT_EQ(alloc.live_mapping_count(), 0U);
}

// A second deallocate() of the same pointer is a no-op: the length was already
// erased, so the allocator refuses to unmap an unknown length (no double-free).
TEST(PageAllocator, DoubleFreeIsNoOp)
{
    cd::mem::PageAllocator alloc;
    void* p = alloc.allocate(4096);
    ASSERT_NE(p, nullptr);

    alloc.deallocate(p);
    EXPECT_EQ(alloc.live_mapping_count(), 0U);

    // Second free must not touch the (now-unknown) pointer.
    alloc.deallocate(p);
    EXPECT_EQ(alloc.live_mapping_count(), 0U);
}

// deallocate() of a pointer this allocator never handed out is rejected.
TEST(PageAllocator, ForeignPointerRejected)
{
    cd::mem::PageAllocator alloc;
    int local = 0;
    EXPECT_EQ(alloc.mapping_length(&local), 0U);
    alloc.deallocate(&local);  // must not crash / must not unmap
    EXPECT_EQ(alloc.live_mapping_count(), 0U);
}

// Zero-size request returns null and records nothing in the side-table.
TEST(PageAllocator, ZeroSizeReturnsNull)
{
    cd::mem::PageAllocator alloc;
    EXPECT_EQ(alloc.allocate(0), nullptr);
    EXPECT_EQ(alloc.live_mapping_count(), 0U);
}

// The OS guarantees page-aligned returns, so an alignment request up to the page
// size is honoured; anything larger than a page is refused (null).
TEST(PageAllocator, AlignmentWithinPageAccepted)
{
    cd::mem::PageAllocator alloc;
    const std::size_t ps = cd::mem::PageAllocator::page_size();
    void* p = alloc.allocate(64, ps);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % ps, 0u);  // page-aligned
    alloc.deallocate(p);
    EXPECT_EQ(alloc.live_mapping_count(), 0U);
}

TEST(PageAllocator, OverPageAlignmentRejected)
{
    cd::mem::PageAllocator alloc;
    const std::size_t ps = cd::mem::PageAllocator::page_size();
    EXPECT_EQ(alloc.allocate(64, ps * 2u), nullptr);  // > page size
    EXPECT_EQ(alloc.live_mapping_count(), 0U);
}

// page_size() is cached: repeated calls return the same power-of-two value.
TEST(PageAllocator, PageSizeStableAndPowerOfTwo)
{
    const std::size_t ps1 = cd::mem::PageAllocator::page_size();
    const std::size_t ps2 = cd::mem::PageAllocator::page_size();
    EXPECT_EQ(ps1, ps2);
    EXPECT_TRUE(cd::mem::detail::is_power_of_two(ps1));
}

// mapping_length(nullptr) is a defined query returning 0 (not UB).
TEST(PageAllocator, MappingLengthNullIsZero)
{
    cd::mem::PageAllocator alloc;
    EXPECT_EQ(alloc.mapping_length(nullptr), 0U);
}

// A sub-page request still maps exactly one whole page (round-up to page size).
TEST(PageAllocator, SubPageRoundsUpToOnePage)
{
    cd::mem::PageAllocator alloc;
    const std::size_t ps = cd::mem::PageAllocator::page_size();
    void* p = alloc.allocate(1U);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(alloc.mapping_length(p), ps);
    alloc.deallocate(p);
}

// ---------- TrackingAllocator ----------------------------------------------
TEST(TrackingAllocator, StatsRecorded)
{
    auto& sys = cd::mem::system_allocator();
    cd::mem::TrackingAllocator tracking { sys, "test" };
    void* a = tracking.allocate(64);
    void* b = tracking.allocate(128);
    auto snap = tracking.stats().snapshot();
    EXPECT_EQ(snap.alloc_count, 2u);
    EXPECT_EQ(snap.failure_count, 0u);
    EXPECT_GE(snap.live_bytes, 64u + 128u);
    EXPECT_GE(snap.peak_bytes, snap.live_bytes);
    tracking.deallocate(a);
    tracking.deallocate(b);
    EXPECT_EQ(tracking.stats().snapshot().free_count, 2u);
}

TEST(TrackingAllocator, TagPreserved)
{
    auto& sys = cd::mem::system_allocator();
    cd::mem::TrackingAllocator t { sys, "renderer" };
    EXPECT_EQ(t.tag(), "renderer");
}

// When the inner allocator fails (null), failure_count increments and no live
// bytes are recorded. A zero-count pool always returns null.
TEST(TrackingAllocator, FailurePathCounted)
{
    cd::mem::PoolAllocator dead { 64, 0 };  // inner always returns null
    cd::mem::TrackingAllocator tracking { dead, "fail" };
    EXPECT_EQ(tracking.allocate(64), nullptr);
    EXPECT_EQ(tracking.allocate(64), nullptr);
    auto snap = tracking.stats().snapshot();
    EXPECT_EQ(snap.failure_count, 2u);
    EXPECT_EQ(snap.alloc_count, 0u);
    EXPECT_EQ(snap.live_bytes, 0u);
}

// A zero-size request neither succeeds nor counts as a failure (size == 0 is the
// "no-op" contract, not an error).
TEST(TrackingAllocator, ZeroSizeNotCountedAsFailure)
{
    auto& sys = cd::mem::system_allocator();
    cd::mem::TrackingAllocator tracking { sys, "zero" };
    EXPECT_EQ(tracking.allocate(0), nullptr);
    auto snap = tracking.stats().snapshot();
    EXPECT_EQ(snap.failure_count, 0u);
    EXPECT_EQ(snap.alloc_count, 0u);
}

// Two trackers sharing an external AllocStats aggregate into one report (the
// documented "all allocators tagged X report into one Stats" use case).
TEST(TrackingAllocator, SharedExternalStatsAggregate)
{
    auto& sys = cd::mem::system_allocator();
    cd::mem::AllocStats shared {};
    cd::mem::TrackingAllocator t1 { sys, "a", &shared };
    cd::mem::TrackingAllocator t2 { sys, "b", &shared };

    void* p = t1.allocate(64);
    void* q = t2.allocate(128);
    ASSERT_NE(p, nullptr);
    ASSERT_NE(q, nullptr);

    auto snap = shared.snapshot();
    EXPECT_EQ(snap.alloc_count, 2u);
    EXPECT_GE(snap.live_bytes, 64u + 128u);

    t1.deallocate(p);
    t2.deallocate(q);
    EXPECT_EQ(shared.snapshot().free_count, 2u);
}

// peak_bytes is a high-water mark: it is retained even after the live bytes drop
// back down on free.
TEST(TrackingAllocator, PeakRetainedAfterFree)
{
    auto& sys = cd::mem::system_allocator();
    cd::mem::TrackingAllocator tracking { sys, "peak" };
    void* a = tracking.allocate(256);
    ASSERT_NE(a, nullptr);
    const std::uint64_t peak_after_alloc = tracking.stats().snapshot().peak_bytes;
    EXPECT_GE(peak_after_alloc, 256u);
    tracking.deallocate(a);
    auto snap = tracking.stats().snapshot();
    EXPECT_EQ(snap.peak_bytes, peak_after_alloc);  // high-water mark stays put
}

// inner() exposes the wrapped backend by reference (decorator identity).
TEST(TrackingAllocator, InnerReferenceIdentity)
{
    auto& sys = cd::mem::system_allocator();
    cd::mem::TrackingAllocator tracking { sys, "id" };
    EXPECT_EQ(&tracking.inner(), &sys);
}

// bytes_in_use / capacity forward to the inner allocator (decorator passthrough).
TEST(TrackingAllocator, ForwardsCapacityToInner)
{
    cd::mem::PoolAllocator pool { 64, 4 };
    cd::mem::TrackingAllocator tracking { pool, "fwd" };
    EXPECT_EQ(tracking.capacity(), pool.capacity());
    void* a = tracking.allocate(64);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(tracking.bytes_in_use(), pool.bytes_in_use());
    tracking.deallocate(a);
}

// ---------- PmrAdapter ------------------------------------------------------
TEST(PmrAdapter, FeedsStdPmrVector)
{
    cd::mem::LinearAllocator arena { static_cast<std::size_t>(16) * 1024 };
    cd::mem::PmrAdapter pmr { arena };
    std::pmr::vector<int> v { &pmr };
    for (int i = 0; i < 100; ++i)
    {
        v.push_back(i);
    }
    EXPECT_EQ(v.size(), 100u);
    EXPECT_EQ(v[42], 42);
    EXPECT_GT(arena.bytes_in_use(), 0u);
}

// std::pmr contract: a memory_resource must throw std::bad_alloc on failure
// (the engine API returns null, but the adapter translates that to a throw).
TEST(PmrAdapter, ThrowsBadAllocOnExhaustion)
{
    cd::mem::LinearAllocator tiny { 32 };
    cd::mem::PmrAdapter pmr { tiny };
    // 64 bytes from a 32-byte arena → inner returns null → adapter throws.
    EXPECT_THROW((void)pmr.allocate(64, 8), std::bad_alloc);
}

// do_is_equal: two adapters over the SAME inner allocator compare equal; over
// different inners they do not.
TEST(PmrAdapter, IsEqualReflectsInnerIdentity)
{
    cd::mem::LinearAllocator arena_a { 1024 };
    cd::mem::LinearAllocator arena_b { 1024 };
    cd::mem::PmrAdapter pa1 { arena_a };
    cd::mem::PmrAdapter pa2 { arena_a };  // same inner as pa1
    cd::mem::PmrAdapter pb { arena_b };   // different inner

    EXPECT_TRUE(pa1.is_equal(pa2));
    EXPECT_FALSE(pa1.is_equal(pb));
}

// A non-PmrAdapter memory_resource is never equal (dynamic_cast guard).
TEST(PmrAdapter, IsEqualRejectsForeignResource)
{
    cd::mem::LinearAllocator arena { 1024 };
    cd::mem::PmrAdapter pmr { arena };
    std::pmr::monotonic_buffer_resource other;
    EXPECT_FALSE(pmr.is_equal(other));
}

// A round-trip through the adapter releases memory back to the inner allocator
// so the pmr container's destructor returns the bytes (pool counter drops).
TEST(PmrAdapter, DeallocateForwardsToInner)
{
    cd::mem::PoolAllocator pool { 256, 8 };
    cd::mem::PmrAdapter pmr { pool };
    {
        std::pmr::vector<std::byte> v { &pmr };
        v.resize(200);
        EXPECT_GT(pool.blocks_in_use(), 0u);
    }
    // Container destroyed → adapter forwarded deallocate → pool reclaimed.
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

}  // namespace
