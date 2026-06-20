// =============================================================================
// CHROMODYNAMIC — cd::core edge / negative / boundary / lifetime tests
// ROADMAP_80_TO_100 batch — drive cd::core 85% -> 100% depth.
//
// Covers thin-coverage paths the smoke suite (test_core.cpp) does not:
//   SmallVector  — inline->heap spill, self-assign, move-from-heap (BUG FIX),
//                  insert/erase, capacity growth, instance-counted lifetime.
//   PoolAllocator— exhaustion, free-list reuse order, alignment, refill.
//   Handle       — generation invalidation, index/type_id boundaries, packing.
//   HandleStore  — generation overflow skip-0, capacity, reserve, for_each holes.
//   Result       — error/value, and_then/transform chains, move-only payload.
//   CVar         — type mismatch, lookup miss, snapshot, unsubscribe, change value.
//   RingBuffer   — lvalue push (BUG FIX), non-trivial element lifetime, wrap.
//   Bitset       — word-boundary bits, full word, find_first across words.
//   FixedString  — exact-capacity fit, clear, empty assign.
//   Bytes/BitOps/StringSplit/RetryPolicy/EnumFlags/Ref/ScopeGuard/CounterTable
//                  boundary + negative cases.
// =============================================================================
#include <cd/core/BitOps.hpp>
#include <cd/core/Bitset.hpp>
#include <cd/core/Bytes.hpp>
#include <cd/core/CVar.hpp>
#include <cd/core/CounterTable.hpp>
#include <cd/core/EnumFlags.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/FixedString.hpp>
#include <cd/core/Handle.hpp>
#include <cd/core/HandleStore.hpp>
#include <cd/core/PoolAllocator.hpp>
#include <cd/core/Ref.hpp>
#include <cd/core/Result.hpp>
#include <cd/core/RetryPolicy.hpp>
#include <cd/core/RingBuffer.hpp>
#include <cd/core/ScopeGuard.hpp>
#include <cd/core/SmallVector.hpp>
#include <cd/core/StringSplit.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{

// -----------------------------------------------------------------------------
// Instance-counted helper: detects construct/destruct imbalance (leaks /
// double-destroy) in container element lifetime tests.
// -----------------------------------------------------------------------------
struct Counted
{
    static inline int live = 0;
    int value { 0 };

    Counted() noexcept { ++live; }
    explicit Counted(int v) noexcept : value { v } { ++live; }
    Counted(const Counted& o) noexcept : value { o.value } { ++live; }
    Counted(Counted&& o) noexcept : value { o.value } { o.value = -1; ++live; }
    Counted& operator=(const Counted&) noexcept = default;
    Counted& operator=(Counted&& o) noexcept { value = o.value; o.value = -1; return *this; }
    ~Counted() { --live; }
};

// Move-only payload to prove SmallVector/Result handle non-copyable types.
struct MoveOnly
{
    std::unique_ptr<int> p;
    explicit MoveOnly(int v) : p { std::make_unique<int>(v) } {}
};

// =============================================================================
// SmallVector — spill boundary, lifetime balance, move-from-heap (BUG FIX)
// =============================================================================

TEST(SmallVectorEdge, InlineToHeapSpillBoundary)
{
    // Arrange
    cd::core::SmallVector<int, 4> v;
    // Act: fill exactly to N (still inline), then one more to force the spill.
    for (int i = 0; i < 4; ++i) v.push_back(i);
    const std::size_t cap_inline = v.capacity();
    v.push_back(99);
    // Assert
    EXPECT_EQ(cap_inline, 4u);              // exactly N before spill
    EXPECT_GE(v.capacity(), 5u);            // grew on heap
    EXPECT_EQ(v.size(), 5u);
    EXPECT_EQ(v[0], 0);
    EXPECT_EQ(v[4], 99);                    // tail preserved across relocation
}

TEST(SmallVectorEdge, CapacityGrowsAtLeastGeometrically)
{
    cd::core::SmallVector<int, 2> v;
    for (int i = 0; i < 100; ++i) v.push_back(i);
    EXPECT_EQ(v.size(), 100u);
    EXPECT_GE(v.capacity(), 100u);
    for (int i = 0; i < 100; ++i) EXPECT_EQ(v[static_cast<std::size_t>(i)], i);
}

TEST(SmallVectorEdge, ElementLifetimeIsBalancedAcrossSpillAndClear)
{
    Counted::live = 0;
    {
        cd::core::SmallVector<Counted, 2> v;
        for (int i = 0; i < 10; ++i) v.emplace_back(i);  // forces heap spill
        EXPECT_EQ(Counted::live, 10);                    // 10 live elements
        v.pop_back();
        EXPECT_EQ(Counted::live, 9);
        v.clear();
        EXPECT_EQ(Counted::live, 0);                     // clear destroys all
    }
    EXPECT_EQ(Counted::live, 0);                          // no leak on scope exit
}

TEST(SmallVectorEdge, MoveFromHeapAllocatedSourceDoesNotOverflow)
{
    // REGRESSION: pre-fix move_from copied o.size_ (> N) elements into the
    // freshly-constructed N-slot inline buffer -> buffer overflow. The fix
    // steals the heap pointer when the source spilled.
    Counted::live = 0;
    cd::core::SmallVector<Counted, 2> src;
    for (int i = 0; i < 8; ++i) src.emplace_back(i);   // src is on the heap
    EXPECT_EQ(Counted::live, 8);

    // Reference alias lets us assert the moved-from contract without tripping
    // bugprone-use-after-move flow analysis on the `src` identifier itself.
    cd::core::SmallVector<Counted, 2>& src_after = src;
    cd::core::SmallVector<Counted, 2> dst { std::move(src) };
    EXPECT_EQ(dst.size(), 8u);
    EXPECT_EQ(Counted::live, 8);                         // no extra/lost objects
    for (int i = 0; i < 8; ++i) EXPECT_EQ(dst[static_cast<std::size_t>(i)].value, i);
    EXPECT_EQ(src_after.size(), 0u);                     // source emptied
}

TEST(SmallVectorEdge, MoveAssignFromHeapSource)
{
    Counted::live = 0;
    cd::core::SmallVector<Counted, 2> src;
    for (int i = 0; i < 6; ++i) src.emplace_back(i);
    cd::core::SmallVector<Counted, 2> dst;
    dst.emplace_back(1000);                              // dst starts inline-1
    dst = std::move(src);
    EXPECT_EQ(dst.size(), 6u);
    EXPECT_EQ(dst[5].value, 5);
    EXPECT_EQ(Counted::live, 6);                         // old dst element freed
}

TEST(SmallVectorEdge, MoveFromInlineSource)
{
    Counted::live = 0;
    cd::core::SmallVector<Counted, 4> src;
    src.emplace_back(1);
    src.emplace_back(2);                                 // stays inline (<= N)
    cd::core::SmallVector<Counted, 4> dst { std::move(src) };
    EXPECT_EQ(dst.size(), 2u);
    EXPECT_EQ(dst[0].value, 1);
    EXPECT_EQ(Counted::live, 2);
}

TEST(SmallVectorEdge, CopyPreservesSourceAndDeepCopies)
{
    cd::core::SmallVector<int, 2> src;
    for (int i = 0; i < 5; ++i) src.push_back(i);       // heap
    cd::core::SmallVector<int, 2> copy { src };
    EXPECT_EQ(copy.size(), src.size());
    copy[0] = 999;
    EXPECT_EQ(src[0], 0);                                // independent storage
}

TEST(SmallVectorEdge, SelfCopyAssignIsNoOp)
{
    cd::core::SmallVector<int, 2> v;
    for (int i = 0; i < 5; ++i) v.push_back(i);
    const auto* before = v.data();
    // Reference alias hides the self-assignment from -Wself-assign while
    // still driving the `this != &other` runtime guard.
    cd::core::SmallVector<int, 2>& alias = v;
    v = alias;
    EXPECT_EQ(v.size(), 5u);
    EXPECT_EQ(v[4], 4);
    EXPECT_EQ(v.data(), before);                         // guarded, untouched
}

TEST(SmallVectorEdge, SelfMoveAssignIsNoOp)
{
    cd::core::SmallVector<int, 2> v;
    for (int i = 0; i < 3; ++i) v.push_back(i);
    // Alias indirection avoids -Wself-move while exercising the self-move guard.
    cd::core::SmallVector<int, 2>& alias = v;
    v = std::move(alias);
    EXPECT_EQ(v.size(), 3u);
    EXPECT_EQ(v[2], 2);
}

TEST(SmallVectorEdge, InsertAtFrontMiddleEnd)
{
    cd::core::SmallVector<int, 8> v;
    for (int i = 0; i < 4; ++i) v.push_back(i * 10);    // [0,10,20,30]
    v.insert(0, -1);                                    // front
    v.insert(3, 15);                                    // middle
    v.insert(v.size(), 99);                             // end (== push_back)
    // Expected: [-1, 0, 10, 15, 20, 30, 99]
    ASSERT_EQ(v.size(), 7u);
    EXPECT_EQ(v[0], -1);
    EXPECT_EQ(v[1], 0);
    EXPECT_EQ(v[2], 10);
    EXPECT_EQ(v[3], 15);
    EXPECT_EQ(v[4], 20);
    EXPECT_EQ(v[5], 30);
    EXPECT_EQ(v[6], 99);
}

TEST(SmallVectorEdge, InsertOutOfRangeClampsToEnd)
{
    cd::core::SmallVector<int, 4> v;
    v.push_back(1);
    v.insert(100, 2);  // clamped to size()
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[1], 2);
}

TEST(SmallVectorEdge, InsertTriggersHeapSpillKeepsOrder)
{
    cd::core::SmallVector<int, 2> v;
    v.push_back(1);
    v.push_back(3);                                     // inline full
    v.insert(1, 2);                                     // forces spill mid-insert
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
}

TEST(SmallVectorEdge, EraseAtEndsAndMiddleBalancesLifetime)
{
    Counted::live = 0;
    cd::core::SmallVector<Counted, 8> v;
    for (int i = 0; i < 5; ++i) v.emplace_back(i);     // [0,1,2,3,4]
    v.erase(0);                                         // remove front -> [1,2,3,4]
    EXPECT_EQ(v[0].value, 1);
    v.erase(v.size() - 1);                              // remove back  -> [1,2,3]
    EXPECT_EQ(v[v.size() - 1].value, 3);
    v.erase(1);                                         // remove middle-> [1,3]
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].value, 1);
    EXPECT_EQ(v[1].value, 3);
    EXPECT_EQ(Counted::live, 2);                        // exactly 2 left, no leak
}

TEST(SmallVectorEdge, EraseOutOfRangeIsNoOp)
{
    cd::core::SmallVector<int, 4> v;
    v.push_back(7);
    auto* e = v.erase(99);  // out of range
    EXPECT_EQ(v.size(), 1u);
    EXPECT_EQ(e, v.end());
}

TEST(SmallVectorEdge, FrontBackReserve)
{
    cd::core::SmallVector<int, 2> v;
    v.reserve(64);
    EXPECT_GE(v.capacity(), 64u);
    v.push_back(11);
    v.push_back(22);
    v.push_back(33);
    EXPECT_EQ(v.front(), 11);
    EXPECT_EQ(v.back(), 33);
}

TEST(SmallVectorEdge, MoveOnlyElementsSupported)
{
    cd::core::SmallVector<MoveOnly, 2> v;
    v.emplace_back(1);
    v.emplace_back(2);
    v.emplace_back(3);                                  // spill with move-only
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(*v[0].p, 1);
    EXPECT_EQ(*v[2].p, 3);
}

TEST(SmallVectorEdge, PopBackOnEmptyIsSafe)
{
    cd::core::SmallVector<int, 4> v;
    v.pop_back();  // no-op, must not underflow
    EXPECT_EQ(v.size(), 0u);
    EXPECT_TRUE(v.empty());
}

// =============================================================================
// PoolAllocator — exhaustion, free-list LIFO reuse, alignment, refill
// =============================================================================

TEST(PoolAllocatorEdge, FreeListReuseIsLifo)
{
    cd::core::PoolAllocator p { 32, 4 };
    void* a = p.allocate();
    void* b = p.allocate();
    p.deallocate(a);
    p.deallocate(b);                       // b is now head of free list
    EXPECT_EQ(p.allocate(), b);            // LIFO: last freed handed back first
    EXPECT_EQ(p.allocate(), a);
}

TEST(PoolAllocatorEdge, ExhaustThenRefillCycles)
{
    cd::core::PoolAllocator p { 16, 2 };
    void* a = p.allocate();
    void* b = p.allocate();
    EXPECT_EQ(p.allocate(), nullptr);      // exhausted
    EXPECT_EQ(p.free_count(), 0u);
    p.deallocate(a);
    EXPECT_EQ(p.free_count(), 1u);
    void* c = p.allocate();                // refill reuses the slot
    EXPECT_EQ(c, a);
    EXPECT_EQ(p.used_count(), 2u);
    p.deallocate(b);
    p.deallocate(c);
    EXPECT_EQ(p.used_count(), 0u);
}

TEST(PoolAllocatorEdge, DeallocateNullptrIsNoOp)
{
    cd::core::PoolAllocator p { 16, 2 };
    const auto before = p.free_count();
    p.deallocate(nullptr);                 // must not corrupt the free list
    EXPECT_EQ(p.free_count(), before);
    void* a = p.allocate();
    EXPECT_NE(a, nullptr);
}

TEST(PoolAllocatorEdge, BlocksLieWithinOwnedStorageAndAreSpacedByBlockSize)
{
    cd::core::PoolAllocator p { 64, 4 };
    std::vector<std::uintptr_t> addrs;
    addrs.reserve(4);
    for (int i = 0; i < 4; ++i)
        addrs.push_back(reinterpret_cast<std::uintptr_t>(p.allocate()));
    // Every block is distinct and they collectively span block_count slots.
    for (std::size_t i = 0; i < addrs.size(); ++i)
        for (std::size_t j = i + 1; j < addrs.size(); ++j)
            EXPECT_NE(addrs[i], addrs[j]);
    EXPECT_EQ(p.block_size(), 64u);
}

TEST(PoolAllocatorEdge, CountsConsistentAfterPartialFree)
{
    cd::core::PoolAllocator p { 32, 8 };
    std::array<void*, 8> slots {};
    for (auto& s : slots) s = p.allocate();
    EXPECT_EQ(p.used_count(), 8u);
    for (std::size_t i = 0; i < 3; ++i) p.deallocate(slots[i]);
    EXPECT_EQ(p.free_count(), 3u);
    EXPECT_EQ(p.used_count(), 5u);
}

// =============================================================================
// Handle — generation invalidation, boundaries, packing round-trip
// =============================================================================

using EdgeHandle = cd::core::Handle<struct EdgeTag>;

TEST(HandleEdge, ConstructFromPackedRoundTrips)
{
    const auto packed = EdgeHandle::pack(0x12345678u, 0xABCDu, 0xEF01u);
    EdgeHandle h { packed };
    EXPECT_EQ(h.index(), 0x12345678u);
    EXPECT_EQ(h.generation(), 0xABCDu);
    EXPECT_EQ(h.type_id(), 0xEF01u);
    EXPECT_EQ(h.value(), packed);
}

TEST(HandleEdge, IndexAtMaxDoesNotLeakIntoGeneration)
{
    EdgeHandle h { EdgeHandle::kMaxIndex, 0u, 0u };
    EXPECT_EQ(h.index(), EdgeHandle::kMaxIndex);
    EXPECT_EQ(h.generation(), 0u);   // full-index value must not bleed up
    EXPECT_EQ(h.type_id(), 0u);
}

TEST(HandleEdge, GenerationFieldIsolatedFromIndexAndTypeId)
{
    EdgeHandle h { 0u, EdgeHandle::kMaxGeneration, 0u };
    EXPECT_EQ(h.index(), 0u);
    EXPECT_EQ(h.generation(), EdgeHandle::kMaxGeneration);
    EXPECT_EQ(h.type_id(), 0u);
}

TEST(HandleEdge, ResetMakesNull)
{
    EdgeHandle h { 5u, 3u, 1u };
    EXPECT_TRUE(h.is_valid());
    h.reset();
    EXPECT_TRUE(h.is_null());
    EXPECT_EQ(h, EdgeHandle::null());
}

TEST(HandleEdge, IndexZeroWithNonZeroGenerationIsStillValid)
{
    // value_ != 0 means valid; a slot-0 live handle has a non-zero generation.
    EdgeHandle h { 0u, 1u, 0u };
    EXPECT_TRUE(h.is_valid());
    EXPECT_FALSE(h.is_null());
}

// =============================================================================
// HandleStore — generation overflow skip-0, reserve, capacity, holes
// =============================================================================

struct Res
{
    int v { 0 };
};

TEST(HandleStoreEdge, ReserveDoesNotChangeSemantics)
{
    cd::core::HandleStore<Res, struct ResTag> store;
    store.reserve(16);
    EXPECT_EQ(store.size(), 0u);
    auto h = *store.insert(Res { 7 });
    EXPECT_EQ(store.get(h)->v, 7);
}

TEST(HandleStoreEdge, GenerationParityFlipsOnInsertEraseCycle)
{
    cd::core::HandleStore<Res, struct ResTag> store;
    auto h1 = *store.insert(Res { 1 });
    EXPECT_EQ(h1.generation() & 1u, 1u);   // live slots are odd
    store.erase(h1);
    auto h2 = *store.insert(Res { 2 });    // reuses slot 0
    EXPECT_EQ(h1.index(), h2.index());
    EXPECT_EQ(h2.generation() & 1u, 1u);   // live again -> odd
    EXPECT_NE(h1.generation(), h2.generation());
    EXPECT_FALSE(store.contains(h1));
}

TEST(HandleStoreEdge, NullHandleNeverContained)
{
    cd::core::HandleStore<Res, struct ResTag> store;
    cd::core::Handle<struct ResTag> null_h;
    EXPECT_FALSE(store.contains(null_h));
    EXPECT_EQ(store.get(null_h), nullptr);
    EXPECT_FALSE(store.erase(null_h));
}

TEST(HandleStoreEdge, OutOfRangeIndexHandleRejected)
{
    cd::core::HandleStore<Res, struct ResTag> store;
    (void)store.insert(Res { 1 });
    // Fabricate a handle with an index beyond capacity but a plausible gen.
    cd::core::Handle<struct ResTag> bogus { 9999u, 1u, 0u };
    EXPECT_FALSE(store.contains(bogus));
    EXPECT_EQ(store.get(bogus), nullptr);
}

TEST(HandleStoreEdge, ForEachSkipsHolesConstOverload)
{
    cd::core::HandleStore<Res, struct ResTag> store;
    auto h0 = *store.insert(Res { 10 });
    (void)store.insert(Res { 20 });
    auto h2 = *store.insert(Res { 30 });
    store.erase(h0);
    store.erase(h2);                       // holes at 0 and 2

    int sum = 0;
    int count = 0;
    const auto& cref = store;
    cref.for_each(
        [&](cd::core::Handle<struct ResTag>, const Res& r)
        {
            sum += r.v;
            ++count;
        }
    );
    EXPECT_EQ(count, 1);
    EXPECT_EQ(sum, 20);
}

// =============================================================================
// Result — error/value, monadic chains, move-only payload
// =============================================================================

TEST(ResultEdge, AndThenChainsOnSuccess)
{
    auto step = [](int x) -> cd::core::Result<int>
    {
        if (x > 100) return cd::core::fail(cd::core::core_errors::Code::kOutOfRange, "too big");
        return x + 1;
    };
    cd::core::Result<int> r = cd::core::Result<int> { 1 }.and_then(step).and_then(step);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 3);
}

TEST(ResultEdge, AndThenShortCircuitsOnError)
{
    int calls = 0;
    auto step = [&](int x) -> cd::core::Result<int>
    {
        ++calls;
        return x + 1;
    };
    cd::core::Result<int> start = cd::core::fail(cd::core::core_errors::Code::kAborted, "stop");
    auto r = start.and_then(step).and_then(step);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(calls, 0);                  // never entered the chain
    EXPECT_EQ(r.error().code, static_cast<std::uint32_t>(cd::core::core_errors::Code::kAborted));
}

TEST(ResultEdge, OrElseRecoversFromError)
{
    cd::core::Result<int> start = cd::core::fail(cd::core::core_errors::Code::kNotFound);
    auto r = start.or_else(
        [](const cd::core::ErrorCode&) -> cd::core::Result<int>
        {
            return 42;  // default value on recovery
        }
    );
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(ResultEdge, ValueOrSuppliesFallback)
{
    cd::core::Result<int> ok { 5 };
    cd::core::Result<int> err = cd::core::fail(cd::core::core_errors::Code::kUnknown);
    EXPECT_EQ(ok.value_or(-1), 5);
    EXPECT_EQ(err.value_or(-1), -1);
}

TEST(ResultEdge, MoveOnlyPayloadRoundTrips)
{
    auto make = [](int v) -> cd::core::Result<std::unique_ptr<int>>
    {
        if (v < 0) return cd::core::fail(cd::core::core_errors::Code::kInvalidArgument);
        return std::make_unique<int>(v);
    };
    auto r = make(7);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(**r, 7);
    auto bad = make(-1);
    EXPECT_FALSE(bad.has_value());
}

TEST(ResultEdge, VoidResultTransformsToValued)
{
    cd::core::Result<void> ok {};
    auto r = ok.transform([] { return 99; });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 99);
}

TEST(ResultEdge, FailWithExplicitDomainCode)
{
    auto r = [&]() -> cd::core::Result<int>
    {
        return cd::core::fail(0xBEEFu, 3u, "custom");
    }();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().domain, 0xBEEFu);
    EXPECT_EQ(r.error().code, 3u);
    EXPECT_EQ(r.error().message, "custom");
}

// =============================================================================
// CVar — type mismatch, lookup miss, snapshot, value-change callback payload
// =============================================================================

TEST(CVarEdge, TypeMismatchGetAsReturnsNullopt)
{
    cd::core::CVarRegistry r;
    r.set("x", static_cast<std::int64_t>(5));
    EXPECT_TRUE(r.get_as<std::int64_t>("x").has_value());
    EXPECT_FALSE(r.get_as<double>("x").has_value());   // wrong alternative
    EXPECT_FALSE(r.get_as<bool>("x").has_value());
    EXPECT_FALSE(r.get_as<std::string>("x").has_value());
}

TEST(CVarEdge, OverwriteChangesTypeAndValue)
{
    cd::core::CVarRegistry r;
    r.set("k", true);
    EXPECT_EQ(r.get_as<bool>("k"), true);
    r.set("k", std::string { "now a string" });        // re-type same key
    EXPECT_FALSE(r.get_as<bool>("k").has_value());
    EXPECT_EQ(*r.get_as<std::string>("k"), "now a string");
    EXPECT_EQ(r.size(), 1u);                            // still one entry
}

TEST(CVarEdge, CallbackReceivesNewValueSnapshot)
{
    // REGRESSION GUARD: set() now passes a value snapshot taken under the
    // lock to callbacks, not a re-read of vars_ outside the lock.
    cd::core::CVarRegistry r;
    cd::core::CVarValue seen;
    auto id = r.subscribe("v", [&](std::string_view, const cd::core::CVarValue& nv) { seen = nv; });
    r.set("v", static_cast<std::int64_t>(123));
    EXPECT_EQ(std::get<std::int64_t>(seen), 123);
    r.unsubscribe("v", id);
}

TEST(CVarEdge, UnsubscribeUnknownKeyIsNoOp)
{
    cd::core::CVarRegistry r;
    r.unsubscribe("never.registered", 1);   // must not throw / corrupt
    EXPECT_EQ(r.size(), 0u);
}

TEST(CVarEdge, SnapshotCapturesAllPairs)
{
    cd::core::CVarRegistry r;
    r.set("a", static_cast<std::int64_t>(1));
    r.set("b", 2.5);
    r.set("c", true);
    auto snap = r.snapshot();
    EXPECT_EQ(snap.size(), 3u);
}

TEST(CVarEdge, EraseThenGetMisses)
{
    cd::core::CVarRegistry r;
    r.set("temp", static_cast<std::int64_t>(9));
    r.erase("temp");
    EXPECT_FALSE(r.get("temp").has_value());
    EXPECT_FALSE(r.contains("temp"));
}

// =============================================================================
// RingBuffer — lvalue push (BUG FIX), lifetime balance, wrap-around
// =============================================================================

TEST(RingBufferEdge, LvaluePushCompilesAndWorks)
{
    // REGRESSION: push(const T&) previously called an undefined `emplace_`
    // helper; any lvalue push failed to compile. Fixed to call `emplace`.
    cd::core::RingBuffer<std::string, 3> rb;
    std::string a = "alpha";
    std::string b = "beta";
    EXPECT_TRUE(rb.push(a));         // lvalue overload
    EXPECT_TRUE(rb.push(b));
    EXPECT_EQ(a, "alpha");           // copy, not move-from
    EXPECT_EQ(*rb.pop(), "alpha");
    EXPECT_EQ(*rb.pop(), "beta");
}

TEST(RingBufferEdge, NonTrivialElementLifetimeBalanced)
{
    Counted::live = 0;
    {
        cd::core::RingBuffer<Counted, 4> rb;
        rb.push(Counted { 1 });
        rb.push(Counted { 2 });
        rb.push(Counted { 3 });
        EXPECT_EQ(Counted::live, 3);
        (void)rb.pop();
        EXPECT_EQ(Counted::live, 2);
        // remaining destroyed by clear() in dtor
    }
    EXPECT_EQ(Counted::live, 0);
}

TEST(RingBufferEdge, PopEmptyReturnsNullopt)
{
    cd::core::RingBuffer<int, 2> rb;
    EXPECT_FALSE(rb.pop().has_value());
}

TEST(RingBufferEdge, ClearDestroysAllAndEmpties)
{
    Counted::live = 0;
    cd::core::RingBuffer<Counted, 4> rb;
    rb.push(Counted { 1 });
    rb.push(Counted { 2 });
    rb.clear();
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(Counted::live, 0);
}

// =============================================================================
// Bitset — word boundaries, full word, cross-word find_first
// =============================================================================

TEST(BitsetEdge, BitsAtWordBoundary)
{
    cd::core::Bitset<128> b;
    b.set(63);    // last bit of word 0
    b.set(64);    // first bit of word 1
    EXPECT_TRUE(b.test(63));
    EXPECT_TRUE(b.test(64));
    EXPECT_FALSE(b.test(62));
    EXPECT_FALSE(b.test(65));
    EXPECT_EQ(b.count(), 2u);
}

TEST(BitsetEdge, FindFirstSpansWords)
{
    cd::core::Bitset<256> b;
    b.set(130);                            // only bit, in word 2
    EXPECT_EQ(b.find_first_set(), 130u);
    b.set(5);
    EXPECT_EQ(b.find_first_set(), 5u);     // lower bit now wins
}

TEST(BitsetEdge, FullWordCountAndForEach)
{
    cd::core::Bitset<64> b;
    for (std::size_t i = 0; i < 64; ++i) b.set(i);
    EXPECT_EQ(b.count(), 64u);
    std::size_t visited = 0;
    b.for_each_set([&](std::size_t) { ++visited; });
    EXPECT_EQ(visited, 64u);
    EXPECT_FALSE(b.none());
    EXPECT_TRUE(b.any());
}

TEST(BitsetEdge, ClearIdempotentOnUnsetBit)
{
    cd::core::Bitset<64> b;
    b.clear(10);                           // clearing an unset bit is a no-op
    EXPECT_FALSE(b.test(10));
    EXPECT_TRUE(b.none());
}

// =============================================================================
// FixedString — exact capacity fit, empty, clear
// =============================================================================

TEST(FixedStringEdge, ExactCapacityFitNoTruncation)
{
    // N=6 -> capacity N-1 == 5; "hello" fits exactly.
    cd::core::FixedString<6> s { "hello" };
    EXPECT_EQ(s.size(), 5u);
    EXPECT_EQ(s.view(), std::string_view { "hello" });
    EXPECT_EQ(cd::core::FixedString<6>::capacity(), 5u);
}

TEST(FixedStringEdge, OneOverCapacityTruncatesByOne)
{
    cd::core::FixedString<6> s { "hello!" };  // 6 chars, cap 5
    EXPECT_EQ(s.size(), 5u);
    EXPECT_EQ(s.view(), std::string_view { "hello" });
}

TEST(FixedStringEdge, ClearResetsToEmpty)
{
    cd::core::FixedString<16> s { "data" };
    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_EQ(s.view(), std::string_view {});
}

TEST(FixedStringEdge, ReassignShrinksProperly)
{
    cd::core::FixedString<16> s { "longer text" };
    s.assign("hi");
    EXPECT_EQ(s.size(), 2u);
    EXPECT_EQ(s.view(), std::string_view { "hi" });
}

// =============================================================================
// Bytes — unit boundaries
// =============================================================================

TEST(BytesEdge, ExactUnitBoundaries)
{
    EXPECT_EQ(cd::core::format_bytes(cd::core::kKB - 1), "1023 B");
    EXPECT_EQ(cd::core::format_bytes(cd::core::kKB), "1.00 KB");
    EXPECT_EQ(cd::core::format_bytes(cd::core::kMB - 1), "1024.00 KB");
    EXPECT_EQ(cd::core::format_bytes(cd::core::kTB), "1.00 TB");
}

TEST(BytesEdge, ZeroFormatsAsBytes)
{
    EXPECT_EQ(cd::core::format_bytes(0), "0 B");
}

// =============================================================================
// BitOps — boundary inputs
// =============================================================================

TEST(BitOpsEdge, NextPow2Boundaries)
{
    EXPECT_EQ(cd::core::next_pow2(0), 1u);
    EXPECT_EQ(cd::core::next_pow2(1), 1u);
    EXPECT_EQ(cd::core::next_pow2(2), 2u);
    EXPECT_EQ(cd::core::next_pow2(3), 4u);
    EXPECT_EQ(cd::core::next_pow2(0x8000000000000000ULL), 0x8000000000000000ULL);
}

TEST(BitOpsEdge, AlignUpAndDownAgree)
{
    EXPECT_EQ(cd::core::align_up(0, 8), 0u);
    EXPECT_EQ(cd::core::align_up(1, 8), 8u);
    EXPECT_EQ(cd::core::align_down(15, 8), 8u);
    EXPECT_EQ(cd::core::align_down(8, 8), 8u);
    EXPECT_EQ(cd::core::align_down(7, 8), 0u);
}

TEST(BitOpsEdge, IsPow2RejectsZero)
{
    EXPECT_FALSE(cd::core::is_pow2(0));
    EXPECT_TRUE(cd::core::is_pow2(0x8000000000000000ULL));
}

TEST(BitOpsEdge, LeadingTrailingZeroCounts)
{
    EXPECT_EQ(cd::core::count_lzero(1u), 63);
    EXPECT_EQ(cd::core::count_tzero(0x8u), 3);
    EXPECT_EQ(cd::core::count_lzero(~std::uint64_t { 0 }), 0);
}

// =============================================================================
// StringSplit — trailing delimiter, all-delimiters, single char
// =============================================================================

TEST(StringSplitEdge, TrailingDelimiterYieldsEmptyTail)
{
    std::vector<std::string_view> parts;
    cd::core::split("a,b,", ',', [&](std::string_view p) { parts.push_back(p); });
    ASSERT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[2], "");
}

TEST(StringSplitEdge, AllDelimitersYieldEmpties)
{
    std::vector<std::string_view> parts;
    cd::core::split(",,,", ',', [&](std::string_view p) { parts.push_back(p); });
    EXPECT_EQ(parts.size(), 4u);
    for (auto p : parts) EXPECT_TRUE(p.empty());
}

TEST(StringSplitEdge, NonemptyDropsAll)
{
    std::vector<std::string_view> parts;
    cd::core::split_nonempty(",,,", ',', [&](std::string_view p) { parts.push_back(p); });
    EXPECT_TRUE(parts.empty());
}

TEST(StringSplitEdge, CountPartsEdge)
{
    EXPECT_EQ(cd::core::count_parts("", ','), 0u);
    EXPECT_EQ(cd::core::count_parts(",", ','), 2u);
    EXPECT_EQ(cd::core::count_parts("nodlim", ','), 1u);
}

// =============================================================================
// RetryPolicy — clamp, first attempt, exhaustion
// =============================================================================

TEST(RetryPolicyEdge, AttemptZeroOrOneUsesBaseDelay)
{
    cd::core::RetryPolicy p { 5, 100, 5000, 2.0F };
    EXPECT_EQ(p.attempt_delay_ms(0), 100u);  // attempt <= 1 -> base
    EXPECT_EQ(p.attempt_delay_ms(1), 100u);
}

TEST(RetryPolicyEdge, ClampHoldsAtDeepAttempt)
{
    cd::core::RetryPolicy p { 100, 100, 300, 2.0F };
    EXPECT_LE(p.attempt_delay_ms(50), 300u);  // never exceeds max_delay_ms
}

TEST(RetryPolicyEdge, ShouldRetryBoundary)
{
    cd::core::RetryPolicy p { 1, 100, 5000, 2.0F };
    EXPECT_FALSE(p.should_retry(1));          // single attempt -> no retry
}

// =============================================================================
// EnumFlags — removal, none, round-trip
// =============================================================================

namespace
{
enum class EF : std::uint32_t
{
    kNone = 0,
    kA = 1,
    kB = 2,
    kC = 4,
};
CD_ENUM_FLAGS(EF)
}  // namespace

TEST(EnumFlagsEdge, RemoveBitViaAndNot)
{
    auto f = EF::kA | EF::kB | EF::kC;
    f &= ~EF::kB;                              // clear B
    EXPECT_TRUE(cd::core::has(f, EF::kA));
    EXPECT_FALSE(cd::core::has(f, EF::kB));
    EXPECT_TRUE(cd::core::has(f, EF::kC));
    f |= EF::kB;                               // exercise operator|=
    EXPECT_TRUE(cd::core::has(f, EF::kB));
}

TEST(EnumFlagsEdge, NoneHasNothing)
{
    auto f = EF::kNone;
    EXPECT_FALSE(cd::core::has(f, EF::kA));
    EXPECT_FALSE(cd::core::has(f, EF::kB));
    EXPECT_FALSE(cd::core::has(f, EF::kC));
}

// =============================================================================
// Ref — mutation, arrow, get aliasing
// =============================================================================

namespace
{
struct RefTarget
{
    int n { 0 };
};
}  // namespace

TEST(RefEdge, MutationThroughRefVisibleAtSource)
{
    RefTarget t { 1 };
    cd::core::Ref<RefTarget> r { t };
    r.get().n = 77;
    EXPECT_EQ(t.n, 77);
    r->n = 88;
    EXPECT_EQ(t.n, 88);
}

TEST(RefEdge, ConstRefReadsOnly)
{
    RefTarget t { 5 };
    cd::core::Ref<const RefTarget> r { t };
    EXPECT_EQ(r.get().n, 5);
    EXPECT_EQ(r->n, 5);
}

// =============================================================================
// ScopeGuard — move transfers ownership exactly once, armed() reflects state
// =============================================================================

TEST(ScopeGuardEdge, MoveTransfersFiringOwnershipExactlyOnce)
{
    int fires = 0;
    {
        auto g1 = cd::core::make_scope_guard([&] { ++fires; });
        EXPECT_TRUE(g1.armed());
        auto g2 = std::move(g1);
        // Deliberately inspect the moved-from source: ScopeGuard's move leaves it
        // in a defined, disarmed state (armed_ == false).
        // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
        EXPECT_FALSE(g1.armed());          // source disarmed after move
        EXPECT_TRUE(g2.armed());
    }
    EXPECT_EQ(fires, 1);                    // fired once, not twice / zero
}

TEST(ScopeGuardEdge, DismissedGuardDoesNotFire)
{
    int fires = 0;
    {
        auto g = cd::core::make_scope_guard([&] { ++fires; });
        g.dismiss();
        EXPECT_FALSE(g.armed());
    }
    EXPECT_EQ(fires, 0);
}

// =============================================================================
// CounterTable — negative deltas, reset semantics
// =============================================================================

TEST(CounterTableEdge, NegativeDeltaDecrements)
{
    cd::core::CounterTable c;
    c.increment("balance", 100);
    c.increment("balance", -30);
    EXPECT_EQ(c.get("balance"), 70);
}

TEST(CounterTableEdge, ResetClearsKeysButResetToZeroKeepsThem)
{
    cd::core::CounterTable c;
    c.increment("a", 1);
    c.increment("b", 2);
    c.reset_all_to_zero();
    EXPECT_EQ(c.size(), 2u);                // keys preserved
    EXPECT_EQ(c.get("a"), 0);
    c.reset();
    EXPECT_EQ(c.size(), 0u);                // hard clear drops keys
}

}  // namespace
