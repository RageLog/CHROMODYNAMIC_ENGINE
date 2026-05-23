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

#include <string>
#include <vector>

namespace
{

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

TEST(TypedPoolAllocator, CreateDestroy)
{
    struct Node
    {
        int id;

        Node(int v)
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

// ---------- PmrAdapter ------------------------------------------------------
TEST(PmrAdapter, FeedsStdPmrVector)
{
    cd::mem::LinearAllocator arena { 16 * 1024 };
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

}  // namespace
