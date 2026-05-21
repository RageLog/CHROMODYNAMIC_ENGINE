// =============================================================================
// CHROMODYNAMIC — cd::concurrency::HazardDomain tests (Sprint S2.5)
// =============================================================================
#include <cd/concurrency/HazardPtr.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{

struct TrackedNode
{
    static std::atomic<int> live;
    int value { 0 };

    TrackedNode() noexcept
    {
        live.fetch_add(1, std::memory_order_relaxed);
    }

    explicit TrackedNode(int v) noexcept
        : value { v }
    {
        live.fetch_add(1, std::memory_order_relaxed);
    }

    ~TrackedNode()
    {
        live.fetch_sub(1, std::memory_order_relaxed);
    }

    TrackedNode(const TrackedNode&) = delete;
    TrackedNode& operator=(const TrackedNode&) = delete;
};

std::atomic<int> TrackedNode::live { 0 };

class HazardPtrTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        TrackedNode::live.store(0);
    }
};

TEST_F(HazardPtrTest, SlotAcquireReleaseCycle)
{
    cd::concurrency::HazardDomain<2> domain;
    {
        cd::concurrency::HazardDomain<2>::ThreadCache cache { domain };
        auto& s = cache.slot();
        (void)s;
        EXPECT_EQ(domain.slot_count(), 1u);
    }
    // Slot returns to free list; reuse must hit count==1 still.
    {
        cd::concurrency::HazardDomain<2>::ThreadCache cache { domain };
        cache.slot();
        EXPECT_EQ(domain.slot_count(), 1u);
    }
}

TEST_F(HazardPtrTest, ProtectAndRetireReclaims)
{
    cd::concurrency::HazardDomain<2> domain;
    cd::concurrency::HazardDomain<2>::ThreadCache cache { domain };

    std::atomic<TrackedNode*> shared { new TrackedNode { 7 } };
    EXPECT_EQ(TrackedNode::live.load(), 1);

    TrackedNode* p = cache.protect(shared);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->value, 7);

    // Retire while still protected — must NOT free yet.
    TrackedNode* victim = shared.exchange(new TrackedNode { 8 });
    cache.retire(victim);
    EXPECT_EQ(TrackedNode::live.load(), 2);

    // Clear hazard, then trigger scan via flush.
    cache.clear();
    cache.flush_retired();
    EXPECT_EQ(TrackedNode::live.load(), 1);

    // Cleanup
    cache.retire(shared.exchange(nullptr));
    cache.flush_retired();
    EXPECT_EQ(TrackedNode::live.load(), 0);
}

TEST_F(HazardPtrTest, BatchScanFreesUnprotected)
{
    cd::concurrency::HazardDomain<2> domain;
    cd::concurrency::HazardDomain<2>::ThreadCache cache { domain };
    for (int i = 0; i < 100; ++i)
    {
        cache.retire(new TrackedNode { i });
    }
    cache.flush_retired();
    EXPECT_EQ(TrackedNode::live.load(), 0);
    EXPECT_GT(domain.reclaimed_total(), 0u);
}

TEST_F(HazardPtrTest, ConcurrentReadersAndOneWriter)
{
    cd::concurrency::HazardDomain<2> domain;
    std::atomic<TrackedNode*> shared { new TrackedNode { 0 } };
    std::atomic<bool> stop { false };

    constexpr int kReaders = 4;
    constexpr int kWrites = 5000;

    std::vector<std::jthread> readers;
    readers.reserve(kReaders);
    for (int i = 0; i < kReaders; ++i)
    {
        readers.emplace_back(
            [&]()
            {
                cd::concurrency::HazardDomain<2>::ThreadCache cache { domain };
                std::uint64_t local_sum = 0;
                while (!stop.load(std::memory_order_acquire))
                {
                    TrackedNode* p = cache.protect(shared);
                    if (p)
                        local_sum += static_cast<std::uint64_t>(p->value);
                    cache.clear();
                }
                (void)local_sum;
            }
        );
    }

    {
        cd::concurrency::HazardDomain<2>::ThreadCache writer_cache { domain };
        for (int i = 1; i <= kWrites; ++i)
        {
            auto* fresh = new TrackedNode { i };
            TrackedNode* old = shared.exchange(fresh, std::memory_order_acq_rel);
            writer_cache.retire(old);
        }
        writer_cache.flush_retired();
    }

    stop.store(true, std::memory_order_release);
    readers.clear();  // joins

    // Final cleanup
    {
        cd::concurrency::HazardDomain<2>::ThreadCache cleanup { domain };
        cleanup.retire(shared.exchange(nullptr));
        cleanup.flush_retired();
    }
    domain.try_reclaim();
    EXPECT_EQ(TrackedNode::live.load(), 0);
}

TEST_F(HazardPtrTest, AbsorbPendingOnThreadExit)
{
    cd::concurrency::HazardDomain<2> domain;
    std::atomic<TrackedNode*> live { new TrackedNode { 42 } };

    // Worker retires a still-live pointer (live one held by main). Main retains
    // hazard so worker must absorb pending to the domain.
    cd::concurrency::HazardDomain<2>::ThreadCache main_cache { domain };
    TrackedNode* p = main_cache.protect(live);
    ASSERT_NE(p, nullptr);

    std::thread worker { [&]()
                         {
                             cd::concurrency::HazardDomain<2>::ThreadCache wc { domain };
                             TrackedNode* victim = live.exchange(nullptr);
                             wc.retire(victim);
                             // wc destructor: flushes; scan can't free (main has hazard); absorbs.
                         } };
    worker.join();

    EXPECT_EQ(TrackedNode::live.load(), 1) << "victim must survive while main holds hazard";
    EXPECT_GE(domain.pending_count(), 1u);

    main_cache.clear();
    domain.try_reclaim();
    EXPECT_EQ(TrackedNode::live.load(), 0);
}

}  // namespace
