// =============================================================================
// CHROMODYNAMIC — cd::concurrency::WorkStealingDeque tests (Sprint S2.5)
// =============================================================================
#include <cd/concurrency/WorkStealingDeque.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{

using cd::concurrency::StealStatus;

TEST(WorkStealingDeque, EmptyOnConstruction)
{
    cd::concurrency::WorkStealingDeque<std::int64_t> d;
    EXPECT_EQ(d.approx_size(), 0u);
    EXPECT_FALSE(d.pop().has_value());
    std::int64_t out {};
    EXPECT_EQ(d.steal(out), StealStatus::Empty);
}

TEST(WorkStealingDeque, OwnerPushPopLifo)
{
    cd::concurrency::WorkStealingDeque<std::int64_t> d;
    for (std::int64_t i = 0; i < 10; ++i)
        d.push(i);
    EXPECT_EQ(d.approx_size(), 10u);
    for (std::int64_t i = 9; i >= 0; --i)
    {
        auto v = d.pop();
        ASSERT_TRUE(v.has_value());
        EXPECT_EQ(*v, i);
    }
    EXPECT_FALSE(d.pop().has_value());
}

TEST(WorkStealingDeque, StealReturnsBottommostFirst)
{
    // Thieves steal from the top (oldest pushed items first).
    cd::concurrency::WorkStealingDeque<std::int64_t> d;
    d.push(100);
    d.push(200);
    d.push(300);
    std::int64_t out {};
    ASSERT_EQ(d.steal(out), StealStatus::Success);
    EXPECT_EQ(out, 100);
    ASSERT_EQ(d.steal(out), StealStatus::Success);
    EXPECT_EQ(out, 200);
    // Owner pops the last one from the bottom (LIFO).
    auto v = d.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 300);
}

TEST(WorkStealingDeque, AutoGrowsBeyondInitialCapacity)
{
    cd::concurrency::WorkStealingDeque<std::int64_t> d { 8 };
    EXPECT_EQ(d.capacity(), 8u);
    for (std::int64_t i = 0; i < 1000; ++i)
        d.push(i);
    EXPECT_GE(d.capacity(), 1024u);
    std::int64_t sum = 0;
    while (auto v = d.pop())
        sum += *v;
    EXPECT_EQ(sum, 999 * 1000 / 2);
}

TEST(WorkStealingDeque, MultipleThievesNoLossNoDuplication)
{
    // Owner pushes N items, M thieves steal concurrently with owner pop.
    // Every produced id must be consumed exactly once across pop+steal.
    constexpr std::int64_t kN = 20'000;
    constexpr int kThieves = 4;

    cd::concurrency::WorkStealingDeque<std::int64_t> d { 1024 };
    std::atomic<bool> producer_done { false };
    std::vector<std::vector<std::int64_t>> stolen(kThieves);
    std::vector<std::int64_t> popped;
    popped.reserve(static_cast<std::size_t>(kN));

    std::vector<std::thread> thieves;
    thieves.reserve(kThieves);
    for (int t = 0; t < kThieves; ++t)
    {
        thieves.emplace_back(
            [&, t]
            {
                while (!producer_done.load(std::memory_order_acquire) || d.approx_size() > 0)
                {
                    std::int64_t v {};
                    const auto st = d.steal(v);
                    if (st == StealStatus::Success)
                    {
                        stolen[static_cast<std::size_t>(t)].push_back(v);
                    }
                    else if (st == StealStatus::Abort)
                    {
                        std::this_thread::yield();
                    }
                }
            }
        );
    }

    // Owner thread interleaves push and pop.
    for (std::int64_t i = 0; i < kN; ++i)
    {
        d.push(i);
        if ((i & 0x7) == 0)
        {
            if (auto v = d.pop())
                popped.push_back(*v);
        }
    }
    while (auto v = d.pop())
        popped.push_back(*v);
    producer_done.store(true, std::memory_order_release);
    for (auto& th : thieves)
        th.join();

    // Drain anything left behind by late thieves.
    while (auto v = d.pop())
        popped.push_back(*v);

    // Aggregate all observed items.
    std::unordered_set<std::int64_t> seen;
    seen.reserve(static_cast<std::size_t>(kN));
    for (auto v : popped)
    {
        ASSERT_TRUE(seen.insert(v).second) << "duplicate from pop: " << v;
    }
    for (const auto& vec : stolen)
    {
        for (auto v : vec)
        {
            ASSERT_TRUE(seen.insert(v).second) << "duplicate across pop/steal: " << v;
        }
    }
    EXPECT_EQ(seen.size(), static_cast<std::size_t>(kN));
    for (std::int64_t i = 0; i < kN; ++i)
    {
        EXPECT_NE(seen.find(i), seen.end()) << "missing id: " << i;
    }
}

TEST(WorkStealingDeque, PointerPayload)
{
    // T must be trivially copyable — verify T* works for object payloads.
    cd::concurrency::WorkStealingDeque<int*> d { 4 };
    int a = 1, b = 2, c = 3;
    d.push(&a);
    d.push(&b);
    d.push(&c);
    auto v = d.pop();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, &c);
    int* out {};
    ASSERT_EQ(d.steal(out), StealStatus::Success);
    EXPECT_EQ(out, &a);
}


// ---------------------------------------------------------------------------
// phase1080 (X1-FU-C): hazard-pointer reclamation mode.
// ---------------------------------------------------------------------------

// Domain-wired deque: repeated grows retire old buffers while 4 thieves
// hammer steal() with their caches. Exactly-once delivery is asserted
// per item; ASAN (Release+ASAN lane) is the use-after-free referee for
// the retire/protect window.
TEST(WorkStealingDeque, HazardModeGrowUnderConcurrentSteals)
{
    using Deque = cd::concurrency::WorkStealingDeque<std::size_t>;
    Deque::HazardDomainT domain;
    Deque dq { 4 };  // tiny start: many grows
    dq.set_hazard_domain(&domain);

    constexpr std::size_t kItems = 100000;
    std::vector<std::atomic<int>> seen(kItems);
    std::atomic<bool> done { false };
    std::atomic<std::size_t> consumed { 0 };

    std::vector<std::jthread> thieves;
    thieves.reserve(4);
    for (int t = 0; t < 4; ++t)
    {
        thieves.emplace_back(
            [&dq, &domain, &seen, &done, &consumed]
            {
                Deque::HazardDomainT::ThreadCache cache { domain };
                while (!done.load(std::memory_order_acquire) ||
                       consumed.load(std::memory_order_acquire) < kItems)
                {
                    std::size_t v {};
                    const auto st = dq.steal(v, &cache);
                    if (st == cd::concurrency::StealStatus::Success)
                    {
                        seen[v].fetch_add(1, std::memory_order_relaxed);
                        consumed.fetch_add(1, std::memory_order_acq_rel);
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            }
        );
    }

    {
        // Owner thread: its cache feeds grow()'s retire path.
        Deque::HazardDomainT::ThreadCache owner_cache { domain };
        dq.set_owner_cache(&owner_cache);
        for (std::size_t i = 0; i < kItems; ++i)
        {
            dq.push(i);  // backlog forces repeated grows under live steals
            if ((i & 1023U) == 0U)
            {
                // Occasionally pop from the owner side too.
                if (auto v = dq.pop())
                {
                    seen[*v].fetch_add(1, std::memory_order_relaxed);
                    consumed.fetch_add(1, std::memory_order_acq_rel);
                }
            }
        }
        // Drain leftovers from the owner side.
        while (auto v = dq.pop())
        {
            seen[*v].fetch_add(1, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_acq_rel);
        }
        done.store(true, std::memory_order_release);
        thieves.clear();  // join
        owner_cache.flush_retired();
        dq.set_owner_cache(nullptr);
    }

    ASSERT_EQ(consumed.load(), kItems);
    for (std::size_t i = 0; i < kItems; ++i)
        ASSERT_EQ(seen[i].load(), 1) << "item " << i << " delivered != once";
    EXPECT_GT(dq.capacity(), 4u) << "test never grew - reclamation path unexercised";
}

// Legacy mode (no domain) still byte-identical: grow retains, dtor frees.
TEST(WorkStealingDeque, LegacyModeStillRetainsWithoutDomain)
{
    cd::concurrency::WorkStealingDeque<std::size_t> dq { 4 };
    for (std::size_t i = 0; i < 1000; ++i)
        dq.push(i);
    std::size_t sum = 0;
    while (auto v = dq.pop())
        sum += *v;
    EXPECT_EQ(sum, 999u * 1000u / 2u);
    EXPECT_GT(dq.capacity(), 4u);
}

}  // namespace
