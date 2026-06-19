// =============================================================================
// CHROMODYNAMIC — cd::concurrency lock-free deepening (≥80→100 marathon)
//
// Stress / edge / negative coverage for the Chase-Lev WorkStealingDeque and the
// HazardDomain reclamation engine that existing suites do NOT touch:
//   - SPSC single-element owner-pop vs steal boundary race (the t==b CAS path)
//   - empty-steal storm: Success never fabricates a value not pushed
//   - capacity stays a power of two across grows
//   - K=1 single-slot hazard domain (the exact config the deque uses)
//   - scan() auto-trigger at the 32-entry batch threshold
//   - slot recycle bound across many short-lived caches
//   - try_reclaim on an empty domain is a clean no-op
//
// Behaviour-preserving: only PUBLIC API is exercised; no sync logic touched.
// All synchronisation in-test is atomic/latch based (no sleep_for) so the
// 120 s ctest TIMEOUT is never a flakiness factor.
// =============================================================================
#include <cd/concurrency/HazardPtr.hpp>
#include <cd/concurrency/Latch.hpp>
#include <cd/concurrency/WorkStealingDeque.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{

using cd::concurrency::StealStatus;

// Lifetime-tracked payload shared by the hazard tests below. Lives at namespace
// scope so it may legally carry a static `live` counter (a local class cannot).
struct Tracked
{
    static std::atomic<int> live;
    int v { 0 };
    Tracked() noexcept { live.fetch_add(1, std::memory_order_relaxed); }
    explicit Tracked(int x) noexcept : v { x } { live.fetch_add(1, std::memory_order_relaxed); }
    ~Tracked() { live.fetch_sub(1, std::memory_order_relaxed); }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
};

std::atomic<int> Tracked::live { 0 };

// --- WorkStealingDeque: SPSC single-element boundary ------------------------
// Owner repeatedly pushes ONE item then pops it while a single thief races the
// same slot. The t == b last-element CAS path is the trickiest part of the
// Chase-Lev protocol; this hammers exactly that window. Every produced id must
// be observed exactly once across {owner pop, thief steal}; none lost, none
// duplicated.
TEST(WsdDeepening, SingleElementOwnerPopVsStealNoLossNoDup)
{
    constexpr std::int64_t kRounds = 200'000;
    cd::concurrency::WorkStealingDeque<std::int64_t> d { 8 };

    std::atomic<bool> producer_done { false };
    std::atomic<std::int64_t> stolen_count { 0 };
    std::atomic<std::int64_t> stolen_xor { 0 };
    std::atomic<std::int64_t> popped_count { 0 };
    std::atomic<std::int64_t> popped_xor { 0 };

    std::thread thief {
        [&]
        {
            while (!producer_done.load(std::memory_order_acquire))
            {
                std::int64_t v {};
                const auto st = d.steal(v);
                if (st == StealStatus::Success)
                {
                    stolen_count.fetch_add(1, std::memory_order_relaxed);
                    stolen_xor.fetch_xor(v, std::memory_order_relaxed);
                }
            }
        }
    };

    std::int64_t expected_xor = 0;
    for (std::int64_t i = 1; i <= kRounds; ++i)
    {
        expected_xor ^= i;
        d.push(i);
        if (auto v = d.pop())
        {
            popped_count.fetch_add(1, std::memory_order_relaxed);
            popped_xor.fetch_xor(*v, std::memory_order_relaxed);
        }
    }
    producer_done.store(true, std::memory_order_release);
    thief.join();

    // Drain whatever the thief left behind.
    while (auto v = d.pop())
    {
        popped_count.fetch_add(1, std::memory_order_relaxed);
        popped_xor.fetch_xor(*v, std::memory_order_relaxed);
    }

    EXPECT_EQ(popped_count.load() + stolen_count.load(), kRounds)
        << "an item was lost or double-counted on the t==b boundary";
    // XOR of every observed id must equal XOR of every produced id (1..kRounds):
    // catches a duplicate (would flip a bit back) or a drop.
    EXPECT_EQ(popped_xor.load() ^ stolen_xor.load(), expected_xor);
}

// --- WorkStealingDeque: empty-steal storm -----------------------------------
// Many thieves hammer steal() on a deque that the owner keeps essentially
// empty. The contract: steal on an empty deque returns Empty, never a spurious
// Success. Abort is permitted (lost CAS race) but a Success must correspond to
// a real pushed value. We verify total Successes == total pushes.
TEST(WsdDeepening, EmptyStealStormNeverFabricatesValues)
{
    constexpr int kThieves = 6;
    constexpr std::int64_t kPushes = 50'000;
    cd::concurrency::WorkStealingDeque<std::int64_t> d { 16 };

    std::atomic<bool> done { false };
    std::atomic<std::int64_t> success_total { 0 };
    std::atomic<std::int64_t> bad_value { 0 };

    std::vector<std::thread> thieves;
    thieves.reserve(kThieves);
    for (int t = 0; t < kThieves; ++t)
    {
        thieves.emplace_back(
            [&]
            {
                while (!done.load(std::memory_order_acquire))
                {
                    std::int64_t v { -1 };
                    const auto st = d.steal(v);
                    if (st == StealStatus::Success)
                    {
                        success_total.fetch_add(1, std::memory_order_relaxed);
                        // Every legit value is in [1, kPushes].
                        if (v < 1 || v > kPushes)
                            bad_value.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        );
    }

    std::int64_t owner_pops = 0;
    for (std::int64_t i = 1; i <= kPushes; ++i)
    {
        d.push(i);
        // Owner usually pops it back immediately, keeping the deque near-empty
        // so most thief steals see Empty/Abort.
        if (auto v = d.pop())
            ++owner_pops;
    }
    done.store(true, std::memory_order_release);
    for (auto& th : thieves)
        th.join();
    while (d.pop())
        ++owner_pops;

    EXPECT_EQ(bad_value.load(), 0) << "steal() returned a value never pushed";
    EXPECT_EQ(success_total.load() + owner_pops, kPushes)
        << "Success count + owner pops must total the produced items";
}

// --- WorkStealingDeque: capacity stays power-of-two after grows -------------
TEST(WsdDeepening, CapacityIsPowerOfTwoAfterGrowth)
{
    cd::concurrency::WorkStealingDeque<std::int64_t> d { 6 };  // rounds up to 8
    EXPECT_EQ(d.capacity(), 8u);
    for (std::int64_t i = 0; i < 5000; ++i)
        d.push(i);
    const auto cap = d.capacity();
    EXPECT_NE(cap, 0u);
    EXPECT_EQ(cap & (cap - 1), 0u) << "capacity must remain a power of two: " << cap;
}

// --- HazardDomain<1>: the deque's exact single-slot config ------------------
// All existing hazard tests use K=2. The deque uses K=1, so exercise that
// instantiation directly: protect + retire + reclaim with one slot per thread.
TEST(HazardDeepening, SingleSlotProtectRetireReclaim)
{
    cd::concurrency::HazardDomain<1> domain;
    cd::concurrency::HazardDomain<1>::ThreadCache cache { domain };

    Tracked::live.store(0);
    std::atomic<Tracked*> shared { new Tracked { 1 } };
    EXPECT_EQ(Tracked::live.load(), 1);

    Tracked* p = cache.protect(shared, 0);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->v, 1);

    // Retire the protected pointer — must survive.
    Tracked* victim = shared.exchange(new Tracked { 2 });
    cache.retire(victim);
    EXPECT_EQ(Tracked::live.load(), 2) << "protected pointer freed prematurely";

    cache.clear(0);
    cache.flush_retired();
    EXPECT_EQ(Tracked::live.load(), 1);

    cache.retire(shared.exchange(nullptr));
    cache.flush_retired();
    EXPECT_EQ(Tracked::live.load(), 0);
}

// --- HazardDomain: scan auto-fires at the 32-entry batch threshold ----------
// retire() calls scan() once retired_.size() hits kBatchThreshold (32). Retire
// exactly 64 unprotected pointers and verify reclaimed_total advanced WITHOUT
// an explicit flush_retired() — proving the in-line batch scan path runs.
TEST(HazardDeepening, BatchThresholdAutoScans)
{
    cd::concurrency::HazardDomain<1> domain;
    cd::concurrency::HazardDomain<1>::ThreadCache cache { domain };

    Tracked::live.store(0);
    // 64 retires => at least one auto-scan at the 32 threshold (nothing
    // protected => everything reclaimable).
    for (int i = 0; i < 64; ++i)
        cache.retire(new Tracked {});
    EXPECT_GT(domain.reclaimed_total(), 0u)
        << "batch threshold scan never fired before flush";
    cache.flush_retired();
    EXPECT_EQ(Tracked::live.load(), 0);
}

// --- HazardDomain: slot recycle is bounded by live thread count -------------
// Spawn caches sequentially across many short-lived threads; each releases its
// slot on dtor so the next reuses it. slot_count must stay tiny (1), not grow
// per thread — proves the free-list recycle path.
TEST(HazardDeepening, SequentialThreadsRecycleOneSlot)
{
    cd::concurrency::HazardDomain<1> domain;
    for (int i = 0; i < 64; ++i)
    {
        std::thread t {
            [&]
            {
                cd::concurrency::HazardDomain<1>::ThreadCache cache { domain };
                (void)cache.slot();
            }
        };
        t.join();
    }
    EXPECT_EQ(domain.slot_count(), 1u)
        << "sequential single-threaded caches must recycle one slot";
}

// --- HazardDomain: concurrent caches each get a distinct slot ---------------
TEST(HazardDeepening, ConcurrentCachesAllocateDistinctSlots)
{
    constexpr int kThreads = 8;
    cd::concurrency::HazardDomain<1> domain;
    cd::concurrency::Latch ready { kThreads };
    cd::concurrency::Latch release { 1 };

    std::vector<std::thread> ts;
    ts.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        ts.emplace_back(
            [&]
            {
                cd::concurrency::HazardDomain<1>::ThreadCache cache { domain };
                (void)cache.slot();  // claim a slot
                ready.count_down();
                release.wait();  // hold all slots simultaneously
            }
        );
    }
    ready.wait();                            // all 8 caches hold a slot at once
    EXPECT_EQ(domain.slot_count(), 8u)       // no recycle possible => 8 distinct
        << "concurrent caches must each get a distinct slot";
    release.count_down();
    for (auto& t : ts)
        t.join();
}

// --- HazardDomain: try_reclaim on empty domain is a no-op -------------------
TEST(HazardDeepening, TryReclaimEmptyDomainIsNoOp)
{
    cd::concurrency::HazardDomain<1> domain;
    EXPECT_EQ(domain.pending_count(), 0u);
    domain.try_reclaim();  // must not crash / not change counters
    EXPECT_EQ(domain.pending_count(), 0u);
    EXPECT_EQ(domain.reclaimed_total(), 0u);
}

}  // namespace
