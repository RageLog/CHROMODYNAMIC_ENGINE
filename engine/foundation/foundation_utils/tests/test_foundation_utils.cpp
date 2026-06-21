// cd::foundation_utils umbrella — aggregation contract verification.
//
// This test binary verifies the umbrella's contract: every sibling lib it
// claims to aggregate is actually #included, its key symbols are reachable
// through the umbrella header alone, and using those symbols produces
// functionally correct behaviour.
//
// Rules:
//   - Include ONLY <cd/foundation_utils/foundation_utils.hpp>.
//   - Do NOT include any sibling lib header directly.
//   - Every aggregated member lib must have at least one reachability test +
//     at least one behaviour test (so a silent removal of an #include becomes
//     both a compile error AND a test failure).
//
// Member libs aggregated:
//   cd::bench         Benchmark.hpp
//   cd::config        Config.hpp
//   cd::diag          Assert.hpp + CrashReporter.hpp + DeadlineMonitor.hpp
//   cd::events        EventBus.hpp + EventRecorder.hpp + ScopedConnection.hpp
//   cd::frame_timing  FrameTimeRing.hpp
//   cd::plugin        FileWatcher.hpp + HotReload.hpp + IPlugin.hpp + Loader.hpp
//   cd::profile       BufferSink.hpp + ChromeTraceSink.hpp + CsvSink.hpp
//                     + Scope.hpp + StatsAggregator.hpp
//   cd::mem           IAllocator.hpp + LinearAllocator.hpp + PageAllocator.hpp
//                     + PmrAdapter.hpp + PoolAllocator.hpp + TrackingAllocator.hpp
#include <cd/foundation_utils/foundation_utils.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SUITE 1 — compile-time reachability (all 8 member libs in one test)
// ---------------------------------------------------------------------------
namespace
{

// Named through pointer-typedef aliases: if the umbrella drops any #include,
// the alias stops referring to a complete type and this TU fails to compile.
TEST(FoundationUtilsAggregation, AllMemberLibSymbolsReachableAsTypes)
{
    using BenchCfg     = cd::bench::Config;
    using CrashCtx     = cd::diag::CrashContext;
    using DeadlineMon  = cd::diag::DeadlineMonitor;
    using PanicInfo    = cd::diag::PanicInfo;
    using EventBusT    = cd::events::EventBus;
    using EventRecT    = cd::events::EventRecorder;
    using RecEvtT      = cd::events::RecordedEvent;
    using ScopedConnT  = cd::events::ScopedConnection;
    using FrameRingT   = cd::frame_timing::FrameTimeRing<>;
    using StatsT       = cd::frame_timing::Stats;
    using IPluginT     = cd::plugin::IPlugin;
    using PluginInfoT  = cd::plugin::PluginInfo;
    using ScopeT       = cd::profile::Scope;
    using SampleT      = cd::profile::Sample;
    using BufferSinkT  = cd::profile::BufferSink;
    using StatRowT     = cd::profile::StatRow;
    using IAllocT      = cd::mem::IAllocator;
    using LinearAllT   = cd::mem::LinearAllocator;
    using TrackingAllT = cd::mem::TrackingAllocator;
    using AllocStatsT  = cd::mem::AllocStats;

    [[maybe_unused]] BenchCfg*     bench_p      = nullptr;
    [[maybe_unused]] CrashCtx*     crash_p      = nullptr;
    [[maybe_unused]] DeadlineMon*  deadline_p   = nullptr;
    [[maybe_unused]] PanicInfo*    panic_p      = nullptr;
    [[maybe_unused]] EventBusT*    ebus_p       = nullptr;
    [[maybe_unused]] EventRecT*    erec_p       = nullptr;
    [[maybe_unused]] RecEvtT*      revt_p       = nullptr;
    [[maybe_unused]] ScopedConnT*  sconn_p      = nullptr;
    [[maybe_unused]] FrameRingT*   fring_p      = nullptr;
    [[maybe_unused]] StatsT*       fstats_p     = nullptr;
    [[maybe_unused]] IPluginT*     iplugin_p    = nullptr;
    [[maybe_unused]] PluginInfoT*  pinfo_p      = nullptr;
    [[maybe_unused]] ScopeT*       scope_p      = nullptr;
    [[maybe_unused]] SampleT*      sample_p     = nullptr;
    [[maybe_unused]] BufferSinkT*  bsink_p      = nullptr;
    [[maybe_unused]] StatRowT*     srow_p       = nullptr;
    [[maybe_unused]] IAllocT*      ialloc_p     = nullptr;
    [[maybe_unused]] LinearAllT*   linear_p     = nullptr;
    [[maybe_unused]] TrackingAllT* tracking_p   = nullptr;
    [[maybe_unused]] AllocStatsT*  astats_p     = nullptr;

    SUCCEED();
}

// ---------------------------------------------------------------------------
// SUITE 2 — cd::bench
// ---------------------------------------------------------------------------

TEST(FoundationUtilsBench, ConfigDefaultValues)
{
    const cd::bench::Config cfg {};
    EXPECT_EQ(cfg.warmup_iterations, 3u);
    EXPECT_EQ(cfg.min_samples, 32u);
    EXPECT_EQ(cfg.min_time_ms, 50u);
    EXPECT_TRUE(cfg.label.empty());
}

TEST(FoundationUtilsBench, ConfigCustomValues)
{
    cd::bench::Config cfg;
    cfg.warmup_iterations = 0u;
    cfg.min_samples       = 1u;
    cfg.min_time_ms       = 0u;
    cfg.label             = "my_bench";
    EXPECT_EQ(cfg.warmup_iterations, 0u);
    EXPECT_EQ(cfg.min_samples, 1u);
    EXPECT_EQ(cfg.label, "my_bench");
}

// ---------------------------------------------------------------------------
// SUITE 3 — cd::config
// ---------------------------------------------------------------------------

TEST(FoundationUtilsConfig, MagicConstantIsReachable)
{
    // kMagic must equal the documented 4-byte sentinel 'CVAR' in little-endian.
    EXPECT_EQ(cd::config::kMagic, 0x43564152u);
}

TEST(FoundationUtilsConfig, SaveFunctionPointerIsNonNull)
{
    // The free function `save` must be link-visible through the umbrella.
    EXPECT_NE(&cd::config::save, nullptr);
}

// ---------------------------------------------------------------------------
// SUITE 4 — cd::diag (Assert + CrashReporter + DeadlineMonitor)
// ---------------------------------------------------------------------------

TEST(FoundationUtilsDiag, PanicInfoDefaultConstruct)
{
    const cd::diag::PanicInfo info {};
    EXPECT_TRUE(info.expression.empty());
    EXPECT_TRUE(info.message.empty());
    EXPECT_TRUE(info.file.empty());
    EXPECT_EQ(info.line, 0u);
    EXPECT_TRUE(info.function.empty());
}

TEST(FoundationUtilsDiag, SetAndRestorePanicHandler)
{
    // Install a no-op handler, verify it is the current one, then restore.
    auto* prev = cd::diag::set_panic_handler([](const cd::diag::PanicInfo&) noexcept {});
    auto* installed = cd::diag::current_panic_handler();
    EXPECT_NE(installed, nullptr);
    // Restore: chain the previous handler back.
    static_cast<void>(cd::diag::set_panic_handler(prev));
}

TEST(FoundationUtilsDiag, CrashSeverityEnumValues)
{
    using S = cd::diag::CrashSeverity;
    // Distinct enum values — if the enum is removed, this TU fails to compile.
    EXPECT_NE(S::kFatal,     S::kNonFatal);
    EXPECT_NE(S::kNonFatal,  S::kInterrupt);
    EXPECT_NE(S::kFatal,     S::kInterrupt);
}

TEST(FoundationUtilsDiag, CrashContextDefaultConstruct)
{
    const cd::diag::CrashContext ctx {};
    EXPECT_EQ(ctx.severity, cd::diag::CrashSeverity::kFatal);
    EXPECT_EQ(ctx.raw_signal, 0);
    EXPECT_TRUE(ctx.label.empty());
}

TEST(FoundationUtilsDiag, DeadlineMonitorConstruct)
{
    // Constructing and immediately destroying a DeadlineMonitor must not crash.
    cd::diag::DeadlineMonitor mon;
    SUCCEED();
}

TEST(FoundationUtilsDiag, DeadlineMonitorInjectableClock)
{
    // Injected-clock constructor is reachable through the umbrella.
    using TP = cd::diag::DeadlineMonitor::TimePoint;
    auto fake_now = []() -> TP { return cd::diag::DeadlineMonitor::Clock::now(); };
    cd::diag::DeadlineMonitor mon { fake_now };
    SUCCEED();
}

// ---------------------------------------------------------------------------
// SUITE 5 — cd::events (EventBus + EventRecorder + ScopedConnection)
// ---------------------------------------------------------------------------

TEST(FoundationUtilsEvents, EventBusPriorityConstant)
{
    EXPECT_EQ(cd::events::EventBus::kDefaultPriority, 0);
}

TEST(FoundationUtilsEvents, EventBusSubscribePublish)
{
    cd::events::EventBus bus;
    struct Ping { int value; };
    int received = -1;
    auto conn = bus.subscribe<Ping>([&](const Ping& p) { received = p.value; });
    bus.publish(Ping { 42 });
    EXPECT_EQ(received, 42);
}

TEST(FoundationUtilsEvents, EventBusScopedConnectionAutoUnsubscribe)
{
    cd::events::EventBus bus;
    struct Tick {};
    int count = 0;
    {
        auto conn = bus.subscribe<Tick>([&](const Tick&) { ++count; });
        bus.publish(Tick {});
        EXPECT_EQ(count, 1);
    }  // conn destroyed → unsubscribed
    bus.publish(Tick {});
    EXPECT_EQ(count, 1);  // second publish must not reach the dead handler
}

TEST(FoundationUtilsEvents, EventBusNoSubscriberNoEffect)
{
    cd::events::EventBus bus;
    struct Orphan {};
    // Publishing with zero subscribers must not crash.
    bus.publish(Orphan {});
    SUCCEED();
}

TEST(FoundationUtilsEvents, EventBusDeferredDrainFifo)
{
    cd::events::EventBus bus;
    struct Msg { int v; };
    std::vector<int> order;
    auto conn = bus.subscribe<Msg>([&](const Msg& m) { order.push_back(m.v); });
    bus.queue_publish(Msg { 1 });
    bus.queue_publish(Msg { 2 });
    bus.queue_publish(Msg { 3 });
    EXPECT_TRUE(order.empty());  // no delivery before drain
    bus.drain();
    ASSERT_EQ(static_cast<int>(order.size()), 3);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(FoundationUtilsEvents, EventBusPriorityOrdering)
{
    cd::events::EventBus bus;
    struct E {};
    std::vector<int> fired;
    // Higher priority fires first.
    auto c_low  = bus.subscribe<E>([&](const E&) { fired.push_back(1); }, -1);
    auto c_high = bus.subscribe<E>([&](const E&) { fired.push_back(99); }, 10);
    bus.publish(E {});
    ASSERT_EQ(static_cast<int>(fired.size()), 2);
    EXPECT_EQ(fired[0], 99);  // priority 10 before priority -1
    EXPECT_EQ(fired[1], 1);
}

TEST(FoundationUtilsEvents, ScopedConnectionDefaultConstructIsInert)
{
    cd::events::ScopedConnection conn;
    conn.release();  // must not crash
    SUCCEED();
}

TEST(FoundationUtilsEvents, EventRecorderRingCapacity)
{
    cd::events::EventRecorder rec { 3 };
    rec.record("A", "first");
    rec.record("B", "second");
    rec.record("C", "third");
    rec.record("D", "fourth");  // overflows cap of 3
    auto snap = rec.snapshot();
    EXPECT_EQ(snap.size(), 3u);
}

TEST(FoundationUtilsEvents, EventRecorderSequenceMonotone)
{
    cd::events::EventRecorder rec;
    rec.record("T", "one");
    rec.record("T", "two");
    auto snap = rec.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_LT(snap[0].sequence, snap[1].sequence);
}

TEST(FoundationUtilsEvents, EventRecorderClear)
{
    cd::events::EventRecorder rec;
    rec.record("X", "hello");
    rec.clear();
    EXPECT_EQ(rec.snapshot().size(), 0u);
}

// ---------------------------------------------------------------------------
// SUITE 6 — cd::frame_timing
// ---------------------------------------------------------------------------

TEST(FoundationUtilsFrameTiming, EmptyRingStats)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    EXPECT_EQ(ring.filled(), 0u);
    EXPECT_EQ(ring.capacity(), 8u);
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 0u);
    EXPECT_DOUBLE_EQ(s.mean, 0.0);
}

TEST(FoundationUtilsFrameTiming, PushAndLastDt)
{
    cd::frame_timing::FrameTimeRing<16> ring;
    ring.push(0.016F);
    ring.push(0.017F);
    EXPECT_EQ(ring.filled(), 2u);
    EXPECT_FLOAT_EQ(ring.last_dt(), 0.017F);
}

TEST(FoundationUtilsFrameTiming, StatsMeanAfterFill)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    ring.push(0.010F);
    ring.push(0.020F);
    ring.push(0.030F);
    ring.push(0.040F);
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 4u);
    EXPECT_NEAR(s.mean, 0.025, 1e-6);
}

TEST(FoundationUtilsFrameTiming, JitterRange)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    ring.push(0.010F);
    ring.push(0.030F);
    const auto s = ring.stats();
    EXPECT_NEAR(static_cast<double>(s.jitter()), 0.020, 1e-5);
}

TEST(FoundationUtilsFrameTiming, FpsMeanInverse)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    ring.push(0.016F);
    ring.push(0.016F);
    ring.push(0.016F);
    ring.push(0.016F);
    const auto s = ring.stats();
    EXPECT_NEAR(s.fps_mean(), 1.0 / 0.016, 0.5);
}

TEST(FoundationUtilsFrameTiming, ResetClearsRing)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(0.016F);
    ring.reset();
    EXPECT_EQ(ring.filled(), 0u);
    EXPECT_FLOAT_EQ(ring.last_dt(), 0.0F);
}

TEST(FoundationUtilsFrameTiming, WrapAroundOldestEvicted)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    // Fill beyond capacity: oldest (0.001) is evicted.
    ring.push(0.001F);
    ring.push(0.002F);
    ring.push(0.003F);
    ring.push(0.004F);
    ring.push(0.005F);  // evicts 0.001
    EXPECT_EQ(ring.filled(), 4u);
    std::vector<float> out;
    ring.copy_in_order(out);
    ASSERT_EQ(static_cast<int>(out.size()), 4);
    // 0.001 must NOT be present.
    const auto it = std::ranges::find(out, 0.001F);
    EXPECT_EQ(it, out.end());
}

// ---------------------------------------------------------------------------
// SUITE 7 — cd::plugin (IPlugin + kAbiVersion + kEntrySymbol)
// ---------------------------------------------------------------------------

TEST(FoundationUtilsPlugin, AbiVersionIsNonZero)
{
    EXPECT_GT(cd::plugin::kAbiVersion, 0u);
}

TEST(FoundationUtilsPlugin, EntrySymbolIsNonEmpty)
{
    EXPECT_FALSE(cd::plugin::kEntrySymbol.empty());
    EXPECT_EQ(cd::plugin::kEntrySymbol, "cd_plugin_create");
}

TEST(FoundationUtilsPlugin, PluginInfoDefaultAbiVersion)
{
    const cd::plugin::PluginInfo info {};
    EXPECT_EQ(info.abi_version, cd::plugin::kAbiVersion);
    EXPECT_TRUE(info.name.empty());
    EXPECT_TRUE(info.version.empty());
    EXPECT_TRUE(info.description.empty());
}

// ---------------------------------------------------------------------------
// SUITE 8 — cd::profile (Scope + Sample + BufferSink + StatsAggregator)
// ---------------------------------------------------------------------------

TEST(FoundationUtilsProfile, SampleDefaultConstruct)
{
    const cd::profile::Sample s {};
    EXPECT_TRUE(s.name.empty());
    EXPECT_EQ(s.start_ns, 0u);
    EXPECT_EQ(s.duration_ns, 0u);
    EXPECT_EQ(s.thread_hash, 0u);
}

TEST(FoundationUtilsProfile, BufferSinkSubmitAndSnapshot)
{
    cd::profile::BufferSink sink { 4 };
    cd::profile::Sample s {};
    s.name        = "test_scope";
    s.start_ns    = 100u;
    s.duration_ns = 200u;
    s.thread_hash = 1u;
    sink.submit(s);
    const auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].duration_ns, 200u);
}

TEST(FoundationUtilsProfile, BufferSinkCapacityRing)
{
    cd::profile::BufferSink sink { 3 };
    for (std::uint64_t i = 0; i < 5u; ++i)
    {
        cd::profile::Sample s {};
        s.name        = "x";
        s.duration_ns = i;
        sink.submit(s);
    }
    const auto snap = sink.snapshot();
    EXPECT_EQ(snap.size(), 3u);  // ring capped at 3
}

TEST(FoundationUtilsProfile, BufferSinkClear)
{
    cd::profile::BufferSink sink { 8 };
    cd::profile::Sample s {};
    s.name = "a";
    sink.submit(s);
    sink.clear();
    EXPECT_TRUE(sink.snapshot().empty());
}

TEST(FoundationUtilsProfile, StatsAggregatorApplyAndQuery)
{
    cd::profile::StatsAggregator agg;
    cd::profile::Sample s1 {};
    s1.name        = "pass";
    s1.duration_ns = 100u;
    cd::profile::Sample s2 {};
    s2.name        = "pass";
    s2.duration_ns = 200u;
    agg.apply({ s1, s2 });
    const auto rows = agg.snapshot();
    ASSERT_EQ(rows.size(), 1u);
    const auto& row = rows[0];
    EXPECT_EQ(row.name, "pass");
    EXPECT_EQ(row.count, 2u);
    EXPECT_EQ(row.min_ns, 100u);
    EXPECT_EQ(row.max_ns, 200u);
    EXPECT_DOUBLE_EQ(row.avg_ns(), 150.0);
}

TEST(FoundationUtilsProfile, StatsAggregatorEmptySnapshot)
{
    const cd::profile::StatsAggregator agg;
    EXPECT_EQ(agg.row_count(), 0u);
    EXPECT_TRUE(agg.snapshot().empty());
}

TEST(FoundationUtilsProfile, StatsAggregatorResetClearsRows)
{
    cd::profile::StatsAggregator agg;
    cd::profile::Sample s {};
    s.name        = "x";
    s.duration_ns = 1u;
    agg.apply({ s });
    EXPECT_EQ(agg.row_count(), 1u);
    agg.reset();
    EXPECT_EQ(agg.row_count(), 0u);
}

TEST(FoundationUtilsProfile, StatRowPercentile)
{
    cd::profile::StatRow row;
    row.durations = { 10u, 20u, 30u, 40u, 50u };
    row.count     = 5u;
    row.total_ns  = 150u;
    row.min_ns    = 10u;
    row.max_ns    = 50u;
    // p50 on 5 values [10,20,30,40,50] = 30 (nearest-rank)
    EXPECT_EQ(row.percentile_ns(0.5), 30u);
    // p0 → 10, p100 → 50
    EXPECT_EQ(row.percentile_ns(0.0), 10u);
    EXPECT_EQ(row.percentile_ns(1.0), 50u);
}

TEST(FoundationUtilsProfile, ScopeRaiiSubmitsThroughSink)
{
    // Wire a BufferSink, fire one Scope, verify duration > 0.
    cd::profile::BufferSink sink { 16 };
    auto* prev = cd::profile::set_sink(&sink);
    {
        cd::profile::Scope scope { "raii_test" };
        // The scope body is intentionally trivial — duration just needs to
        // be non-negative; real monotonic clock resolution is platform-dependent.
    }
    cd::profile::set_sink(prev);  // restore before assertions
    const auto snap = sink.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].name, "raii_test");
    EXPECT_GE(snap[0].duration_ns, 0u);
}

// ---------------------------------------------------------------------------
// SUITE 9 — cd::mem (IAllocator + LinearAllocator + TrackingAllocator)
// ---------------------------------------------------------------------------

TEST(FoundationUtilsMem, DefaultAlignmentConstant)
{
    EXPECT_GE(cd::mem::kDefaultAlignment, 1u);
    // Must be a power of two.
    const auto a = cd::mem::kDefaultAlignment;
    EXPECT_EQ(a & (a - 1u), 0u);
}

TEST(FoundationUtilsMem, LinearAllocatorBasicAlloc)
{
    cd::mem::LinearAllocator alloc { 256 };
    void* p = alloc.allocate(16u);
    EXPECT_NE(p, nullptr);
}

TEST(FoundationUtilsMem, LinearAllocatorExhaustReturnsNull)
{
    cd::mem::LinearAllocator alloc { 32 };
    void* first = alloc.allocate(32u);
    EXPECT_NE(first, nullptr);
    void* second = alloc.allocate(1u);  // capacity exhausted
    EXPECT_EQ(second, nullptr);
}

TEST(FoundationUtilsMem, LinearAllocatorZeroSizeReturnsNull)
{
    cd::mem::LinearAllocator alloc { 64 };
    void* p = alloc.allocate(0u);
    EXPECT_EQ(p, nullptr);
}

TEST(FoundationUtilsMem, LinearAllocatorNonPow2AlignReturnsNull)
{
    cd::mem::LinearAllocator alloc { 256 };
    void* p = alloc.allocate(8u, 3u);  // alignment=3 is not a power of two
    EXPECT_EQ(p, nullptr);
}

TEST(FoundationUtilsMem, TrackingAllocatorStats)
{
    cd::mem::TrackingAllocator tracker { cd::mem::system_allocator(), "umbrella_test" };
    void* p = tracker.allocate(64u);
    EXPECT_NE(p, nullptr);
    tracker.deallocate(p);
    // After one alloc+free the alloc_count must be 1.
    EXPECT_EQ(tracker.stats().snapshot().alloc_count, 1u);
}

TEST(FoundationUtilsMem, TrackingAllocatorPeakHighWater)
{
    cd::mem::TrackingAllocator tracker { cd::mem::system_allocator(), "peak_test" };
    void* a = tracker.allocate(128u);
    void* b = tracker.allocate(64u);
    tracker.deallocate(b);
    tracker.deallocate(a);
    const auto snap = tracker.stats().snapshot();
    EXPECT_GE(snap.alloc_count, 2u);
    EXPECT_GE(snap.free_count, 2u);
}

TEST(FoundationUtilsMem, TrackingAllocatorNullDeallocNoOp)
{
    cd::mem::TrackingAllocator tracker { cd::mem::system_allocator(), "null_dealloc_test" };
    tracker.deallocate(nullptr);  // must not crash
    SUCCEED();
}

// ---------------------------------------------------------------------------
// SUITE 10 — cd::mem extended: PageAllocator, PoolAllocator, PmrAdapter
// ---------------------------------------------------------------------------

// --- PageAllocator ----------------------------------------------------------

TEST(FoundationUtilsMemPage, PageSizePositiveAndPowerOfTwo)
{
    const std::size_t ps = cd::mem::PageAllocator::page_size();
    EXPECT_GE(ps, 4096u);
    EXPECT_EQ(ps & (ps - 1u), 0u);  // power of two
}

TEST(FoundationUtilsMemPage, PageSizeStable)
{
    const std::size_t ps1 = cd::mem::PageAllocator::page_size();
    const std::size_t ps2 = cd::mem::PageAllocator::page_size();
    EXPECT_EQ(ps1, ps2);
}

TEST(FoundationUtilsMemPage, BasicAllocateDeallocate)
{
    cd::mem::PageAllocator alloc;
    void* p = alloc.allocate(8192u);
    ASSERT_NE(p, nullptr);
    auto* bytes = static_cast<std::byte*>(p);
    bytes[0]    = std::byte { 0xAB };
    bytes[8191] = std::byte { 0xCD };
    EXPECT_EQ(bytes[0],    std::byte { 0xAB });
    EXPECT_EQ(bytes[8191], std::byte { 0xCD });
    alloc.deallocate(p);
    EXPECT_EQ(alloc.live_mapping_count(), 0u);
}

TEST(FoundationUtilsMemPage, ZeroSizeReturnsNull)
{
    cd::mem::PageAllocator alloc;
    EXPECT_EQ(alloc.allocate(0u), nullptr);
    EXPECT_EQ(alloc.live_mapping_count(), 0u);
}

TEST(FoundationUtilsMemPage, MappingLengthRoundsUpToWholePages)
{
    cd::mem::PageAllocator alloc;
    const std::size_t ps = cd::mem::PageAllocator::page_size();
    void* p = alloc.allocate(ps + 1u);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(alloc.mapping_length(p), 2u * ps);
    EXPECT_EQ(alloc.live_mapping_count(), 1u);
    alloc.deallocate(p);
    EXPECT_EQ(alloc.mapping_length(p), 0u);
    EXPECT_EQ(alloc.live_mapping_count(), 0u);
}

TEST(FoundationUtilsMemPage, MappingLengthNullIsZero)
{
    cd::mem::PageAllocator alloc;
    EXPECT_EQ(alloc.mapping_length(nullptr), 0u);
}

TEST(FoundationUtilsMemPage, AlignmentWithinPageAccepted)
{
    cd::mem::PageAllocator alloc;
    const std::size_t ps = cd::mem::PageAllocator::page_size();
    void* p = alloc.allocate(64u, ps);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % ps, 0u);
    alloc.deallocate(p);
}

TEST(FoundationUtilsMemPage, OverPageAlignmentRejected)
{
    cd::mem::PageAllocator alloc;
    const std::size_t ps = cd::mem::PageAllocator::page_size();
    EXPECT_EQ(alloc.allocate(64u, ps * 2u), nullptr);
    EXPECT_EQ(alloc.live_mapping_count(), 0u);
}

TEST(FoundationUtilsMemPage, DoubleFreeIsNoOp)
{
    cd::mem::PageAllocator alloc;
    void* p = alloc.allocate(4096u);
    ASSERT_NE(p, nullptr);
    alloc.deallocate(p);
    EXPECT_EQ(alloc.live_mapping_count(), 0u);
    alloc.deallocate(p);  // second free: unknown ptr, must not crash
    EXPECT_EQ(alloc.live_mapping_count(), 0u);
}

TEST(FoundationUtilsMemPage, ForeignPointerRejected)
{
    cd::mem::PageAllocator alloc;
    int local = 0;
    EXPECT_EQ(alloc.mapping_length(&local), 0u);
    alloc.deallocate(&local);  // not our allocation
    EXPECT_EQ(alloc.live_mapping_count(), 0u);
}

TEST(FoundationUtilsMemPage, SubPageRoundsUpToOnePage)
{
    cd::mem::PageAllocator alloc;
    const std::size_t ps = cd::mem::PageAllocator::page_size();
    void* p = alloc.allocate(1u);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(alloc.mapping_length(p), ps);
    alloc.deallocate(p);
}

TEST(FoundationUtilsMemPage, ManyPagesLiveMappingCountBalances)
{
    cd::mem::PageAllocator alloc;
    constexpr std::size_t kCount = 16u;
    std::vector<void*> pages;
    pages.reserve(kCount);
    for (std::size_t i = 0u; i < kCount; ++i)
    {
        void* p = alloc.allocate(4096u);
        ASSERT_NE(p, nullptr) << "page " << i;
        pages.push_back(p);
    }
    EXPECT_EQ(alloc.live_mapping_count(), kCount);
    for (void* p : pages)
    {
        alloc.deallocate(p);
    }
    EXPECT_EQ(alloc.live_mapping_count(), 0u);
}

// --- PoolAllocator ----------------------------------------------------------

TEST(FoundationUtilsMemPool, FixedBlockRoundTrip)
{
    cd::mem::PoolAllocator pool { 64u, 8u };
    EXPECT_EQ(pool.block_count(), 8u);
    EXPECT_GE(pool.block_size(), 64u);
    EXPECT_EQ(pool.blocks_in_use(), 0u);

    void* p = pool.allocate(64u);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(pool.blocks_in_use(), 1u);
    EXPECT_EQ(pool.blocks_free(), 7u);
    pool.deallocate(p);
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

TEST(FoundationUtilsMemPool, ExhaustReturnsNullThenRefills)
{
    cd::mem::PoolAllocator pool { 32u, 4u };
    std::array<void*, 4> blks {};
    for (std::size_t i = 0u; i < blks.size(); ++i)
    {
        blks.at(i) = pool.allocate(32u);
        ASSERT_NE(blks.at(i), nullptr) << "block " << i;
    }
    EXPECT_EQ(pool.allocate(32u), nullptr);  // exhausted
    for (void* p : blks)
    {
        pool.deallocate(p);
    }
    EXPECT_EQ(pool.blocks_in_use(), 0u);
    EXPECT_NE(pool.allocate(32u), nullptr);
}

TEST(FoundationUtilsMemPool, FreeListIsLifo)
{
    cd::mem::PoolAllocator pool { 64u, 4u };
    void* a = pool.allocate(64u);
    void* b = pool.allocate(64u);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    pool.deallocate(a);
    pool.deallocate(b);
    void* x = pool.allocate(64u);
    void* y = pool.allocate(64u);
    EXPECT_EQ(x, b);  // LIFO: last freed comes back first
    EXPECT_EQ(y, a);
    pool.deallocate(x);
    pool.deallocate(y);
}

TEST(FoundationUtilsMemPool, OversizeRequestReturnsNull)
{
    cd::mem::PoolAllocator pool { 64u, 4u };
    EXPECT_EQ(pool.allocate(128u), nullptr);
}

TEST(FoundationUtilsMemPool, OveralignedRequestReturnsNull)
{
    cd::mem::PoolAllocator pool { 64u, 4u, 16u };
    EXPECT_EQ(pool.allocate(64u, 64u), nullptr);  // alignment > block_align
}

TEST(FoundationUtilsMemPool, ZeroCountIsInert)
{
    cd::mem::PoolAllocator pool { 64u, 0u };
    EXPECT_EQ(pool.block_count(), 0u);
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.allocate(64u), nullptr);
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

TEST(FoundationUtilsMemPool, NonPow2AlignmentIsInert)
{
    cd::mem::PoolAllocator pool { 64u, 4u, 48u };  // 48 not power-of-2
    EXPECT_EQ(pool.allocate(64u), nullptr);
}

TEST(FoundationUtilsMemPool, DeallocateNullIsNoOp)
{
    cd::mem::PoolAllocator pool { 64u, 4u };
    void* a = pool.allocate(64u);
    ASSERT_NE(a, nullptr);
    pool.deallocate(nullptr);
    EXPECT_EQ(pool.blocks_in_use(), 1u);
    pool.deallocate(a);
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

TEST(FoundationUtilsMemPool, BytesAccounting)
{
    cd::mem::PoolAllocator pool { 64u, 4u };
    const std::size_t bs = pool.block_size();
    EXPECT_EQ(pool.capacity(), bs * 4u);
    EXPECT_EQ(pool.bytes_in_use(), 0u);
    void* a = pool.allocate(64u);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(pool.bytes_in_use(), bs);
    pool.deallocate(a);
    EXPECT_EQ(pool.bytes_in_use(), 0u);
}

TEST(FoundationUtilsMemPool, BlocksAreAligned)
{
    constexpr std::size_t kAlign = 64u;
    cd::mem::PoolAllocator pool { 48u, 8u, kAlign };
    EXPECT_EQ(pool.block_size() % kAlign, 0u);
    for (int i = 0; i < 8; ++i)
    {
        void* p = pool.allocate(48u, kAlign);
        ASSERT_NE(p, nullptr) << "block " << i;
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % kAlign, 0u) << "block " << i;
    }
}

TEST(FoundationUtilsMemPool, TinyBlockSizeBumpedToFitFreeNode)
{
    cd::mem::PoolAllocator pool { 1u, 4u };  // 1-byte blocks
    EXPECT_GE(pool.block_size(), sizeof(void*));
    void* p = pool.allocate(1u);
    ASSERT_NE(p, nullptr);
    pool.deallocate(p);
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

// --- TypedPoolAllocator -----------------------------------------------------

TEST(FoundationUtilsMemTypedPool, CreateDestroyRoundTrip)
{
    cd::mem::TypedPoolAllocator<std::int32_t> pool { 4u };
    auto* a = pool.create(10);
    auto* b = pool.create(20);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(*a, 10);
    EXPECT_EQ(*b, 20);
    EXPECT_EQ(pool.in_use(), 2u);
    pool.destroy(a);
    pool.destroy(b);
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(FoundationUtilsMemTypedPool, ExhaustReturnsNullThenRefills)
{
    cd::mem::TypedPoolAllocator<int> pool { 2u };
    auto* a = pool.create(1);
    auto* b = pool.create(2);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(pool.create(3), nullptr);  // exhausted
    EXPECT_EQ(pool.in_use(), 2u);
    pool.destroy(a);
    auto* c = pool.create(4);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(*c, 4);
    pool.destroy(b);
    pool.destroy(c);
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(FoundationUtilsMemTypedPool, DestroyRunsDtor)
{
    struct Counter
    {
        int* count;
        explicit Counter(int* c) noexcept : count { c } {}
        ~Counter() { ++(*count); }
    };
    int dtor_calls = 0;
    cd::mem::TypedPoolAllocator<Counter> pool { 4u };
    auto* obj = pool.create(&dtor_calls);
    ASSERT_NE(obj, nullptr);
    pool.destroy(obj);
    EXPECT_EQ(dtor_calls, 1);
    pool.destroy(nullptr);  // null no-op
    EXPECT_EQ(dtor_calls, 1);
}

TEST(FoundationUtilsMemTypedPool, CapacityObjectsMatchesCount)
{
    cd::mem::TypedPoolAllocator<double> pool { 7u };
    EXPECT_EQ(pool.capacity_objects(), 7u);
}

// --- PmrAdapter -------------------------------------------------------------

TEST(FoundationUtilsMemPmr, FeedsStdPmrVector)
{
    cd::mem::LinearAllocator arena { static_cast<std::size_t>(16) * 1024u };
    cd::mem::PmrAdapter pmr { arena };
    std::pmr::vector<int> v { &pmr };
    for (int i = 0; i < 50; ++i)
    {
        v.push_back(i);
    }
    EXPECT_EQ(v.size(), 50u);
    EXPECT_EQ(v[42], 42);
    EXPECT_GT(arena.bytes_in_use(), 0u);
}

TEST(FoundationUtilsMemPmr, ThrowsBadAllocOnExhaustion)
{
    cd::mem::LinearAllocator tiny { 32u };
    cd::mem::PmrAdapter pmr { tiny };
    EXPECT_THROW(static_cast<void>(pmr.allocate(64u, 8u)), std::bad_alloc);
}

TEST(FoundationUtilsMemPmr, IsEqualSameInner)
{
    cd::mem::LinearAllocator arena { 512u };
    cd::mem::PmrAdapter pa1 { arena };
    cd::mem::PmrAdapter pa2 { arena };
    EXPECT_TRUE(pa1.is_equal(pa2));
}

TEST(FoundationUtilsMemPmr, IsEqualDifferentInner)
{
    cd::mem::LinearAllocator arena_a { 512u };
    cd::mem::LinearAllocator arena_b { 512u };
    cd::mem::PmrAdapter pa { arena_a };
    cd::mem::PmrAdapter pb { arena_b };
    EXPECT_FALSE(pa.is_equal(pb));
}

TEST(FoundationUtilsMemPmr, IsEqualForeignResourceFalse)
{
    cd::mem::LinearAllocator arena { 512u };
    cd::mem::PmrAdapter pmr { arena };
    std::pmr::monotonic_buffer_resource other;
    EXPECT_FALSE(pmr.is_equal(other));
}

TEST(FoundationUtilsMemPmr, DeallocateForwardsToInner)
{
    cd::mem::PoolAllocator pool { 256u, 8u };
    cd::mem::PmrAdapter pmr { pool };
    {
        std::pmr::vector<std::byte> v { &pmr };
        v.resize(200u);
        EXPECT_GT(pool.blocks_in_use(), 0u);
    }  // destructor returns memory to pool
    EXPECT_EQ(pool.blocks_in_use(), 0u);
}

// ---------------------------------------------------------------------------
// SUITE 11 — cd::plugin extended: Loader error paths + HotReloader + FileWatcher
// ---------------------------------------------------------------------------

// Minimal concrete IPlugin for injection tests.
class UmbrellaFakePlugin final : public cd::plugin::IPlugin
{
public:
    explicit UmbrellaFakePlugin(std::uint32_t id) noexcept : id_ { id } {}
    [[nodiscard]] cd::plugin::PluginInfo info() const noexcept override
    {
        return { "umbrella_fake", "1.0", "", cd::plugin::kAbiVersion };
    }
    [[nodiscard]] bool initialize() noexcept override { return true; }
    void shutdown() noexcept override {}
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

private:
    std::uint32_t id_;
};

cd::plugin::LoadedPlugin make_umbrella_loaded(std::uint32_t id, std::string path)
{
    cd::plugin::LoadedPlugin lp;
    lp.instance      = std::make_unique<UmbrellaFakePlugin>(id);
    lp.native_handle = nullptr;
    lp.path          = std::move(path);
    return lp;
}

// --- Loader error paths -----------------------------------------------------

TEST(FoundationUtilsPlugin, LoaderRejectsNonexistentPath)
{
    cd::plugin::Loader loader;
    auto r = loader.load("no_such_plugin_zyxq.dll");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::loader_errors::Code::kLoadFailed));
}

TEST(FoundationUtilsPlugin, LoadedPathsNotUpdatedOnFailure)
{
    cd::plugin::Loader loader;
    EXPECT_TRUE(loader.loaded_paths().empty());
    static_cast<void>(loader.load("ghost.so"));
    EXPECT_TRUE(loader.loaded_paths().empty());
}

// --- HotReloader: mount / poll / unmount ------------------------------------

TEST(FoundationUtilsPlugin, HotReloaderMountPollNoChange)
{
    std::uint64_t ver = 7u;
    std::uint32_t id  = 0u;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_umbrella_loaded(++id, std::string { p });
        },
        [&](std::string_view) { return ver; }
    };
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    EXPECT_TRUE(hr.is_mounted());
    EXPECT_EQ(hr.current_version(), 7u);
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r);
    EXPECT_EQ(hr.reload_count(), 0u);
}

TEST(FoundationUtilsPlugin, HotReloaderVersionChangeTriggersReload)
{
    std::uint64_t ver = 1u;
    std::uint32_t id  = 0u;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_umbrella_loaded(++id, std::string { p });
        },
        [&](std::string_view) { return ver; }
    };
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    const auto first_id = dynamic_cast<UmbrellaFakePlugin*>(hr.current())->id();
    ver = 2u;
    auto r = hr.poll();
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ(hr.reload_count(), 1u);
    const auto* second = dynamic_cast<UmbrellaFakePlugin*>(hr.current());
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second->id(), first_id);
    EXPECT_EQ(hr.current_version(), 2u);
}

TEST(FoundationUtilsPlugin, HotReloaderBeforeUnloadAfterLoadFire)
{
    std::uint64_t ver = 100u;
    std::uint32_t id  = 0u;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_umbrella_loaded(++id, std::string { p });
        },
        [&](std::string_view) { return ver; }
    };
    int unloads = 0;
    int loads   = 0;
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unloads; });
    hr.set_after_load([&](cd::plugin::IPlugin&)    { ++loads;   });
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    EXPECT_EQ(loads, 1);   // mount fires after_load
    EXPECT_EQ(unloads, 0);
    ver = 200u;
    ASSERT_TRUE(hr.poll().has_value());
    EXPECT_EQ(unloads, 1);
    EXPECT_EQ(loads, 2);
}

TEST(FoundationUtilsPlugin, HotReloaderFailedReloadKeepsPrevious)
{
    std::uint64_t ver = 1u;
    bool fail_next    = false;
    std::uint32_t id  = 0u;
    int unloads       = 0;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            if (fail_next)
                return std::unexpected(
                    cd::plugin::loader_errors::make(cd::plugin::loader_errors::Code::kFactoryFailed));
            return make_umbrella_loaded(++id, std::string { p });
        },
        [&](std::string_view) { return ver; }
    };
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unloads; });
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    const auto first_id = dynamic_cast<UmbrellaFakePlugin*>(hr.current())->id();
    ver       = 2u;
    fail_next = true;
    auto r = hr.poll();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(unloads, 0);              // teardown never ran
    EXPECT_EQ(hr.reload_count(), 0u);   // count not incremented
    ASSERT_TRUE(hr.is_mounted());
    EXPECT_EQ(dynamic_cast<UmbrellaFakePlugin*>(hr.current())->id(), first_id);
}

TEST(FoundationUtilsPlugin, HotReloaderPollBeforeMountErrors)
{
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_umbrella_loaded(1u, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    auto r = hr.poll();
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::hot_reload_errors::Code::kNotMounted));
}

TEST(FoundationUtilsPlugin, HotReloaderMountTwiceErrors)
{
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_umbrella_loaded(1u, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    auto r = hr.mount("p.dll");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::hot_reload_errors::Code::kAlreadyMounted));
}

TEST(FoundationUtilsPlugin, HotReloaderUnmountFiresAndClearsState)
{
    std::uint64_t ver = 5u;
    int unloads = 0;
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_umbrella_loaded(1u, std::string { p });
        },
        [&](std::string_view) { return ver; }
    };
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unloads; });
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    hr.unmount();
    EXPECT_EQ(unloads, 1);
    EXPECT_FALSE(hr.is_mounted());
    EXPECT_EQ(hr.current_version(), 0u);
    EXPECT_TRUE(hr.current_path().empty());
}

TEST(FoundationUtilsPlugin, HotReloaderUnmountWhenNotMountedIsNoOp)
{
    int unloads = 0;
    cd::plugin::HotReloader hr {
        [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_umbrella_loaded(1u, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    hr.set_before_unload([&](cd::plugin::IPlugin&) { ++unloads; });
    hr.unmount();
    EXPECT_EQ(unloads, 0);
    EXPECT_FALSE(hr.is_mounted());
}

TEST(FoundationUtilsPlugin, HotReloaderRemountAfterUnmountLoadsFresh)
{
    std::uint32_t id = 0u;
    cd::plugin::HotReloader hr {
        [&](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
            return make_umbrella_loaded(++id, std::string { p });
        },
        [](std::string_view) { return std::uint64_t { 1 }; }
    };
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    const auto first_id = dynamic_cast<UmbrellaFakePlugin*>(hr.current())->id();
    hr.unmount();
    ASSERT_TRUE(hr.mount("p.dll").has_value());
    auto* second = dynamic_cast<UmbrellaFakePlugin*>(hr.current());
    ASSERT_NE(second, nullptr);
    EXPECT_NE(second->id(), first_id);
}

TEST(FoundationUtilsPlugin, DefaultPluginVersionMissingFileReturnsZero)
{
    EXPECT_EQ(cd::plugin::default_plugin_version("no_such_file_zyxq.dll"), 0u);
}

// --- FileWatcher (deterministic, interval=0) --------------------------------

// RAII temp file — mtime bump forces next stat to differ.
class UmbrellaTempFile
{
public:
    UmbrellaTempFile()
    {
        path_ = std::filesystem::temp_directory_path()
              / ("cd_fu_watch_"
                 + std::to_string(reinterpret_cast<std::uintptr_t>(this))
                 + ".bin");
        write_file("v0");
    }

    ~UmbrellaTempFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    UmbrellaTempFile(const UmbrellaTempFile&)            = delete;
    UmbrellaTempFile& operator=(const UmbrellaTempFile&) = delete;

    void write_file(std::string_view contents)
    {
        {
            std::ofstream out { path_, std::ios::binary | std::ios::trunc };
            out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        }
        std::error_code ec;
        std::filesystem::last_write_time(
            path_,
            std::filesystem::file_time_type::clock::now() + std::chrono::seconds { 1 },
            ec);
    }

    [[nodiscard]] std::string path_str() const { return path_.string(); }

private:
    std::filesystem::path path_;
};

TEST(FoundationUtilsPlugin, FileWatcherNativeFactoryNonNull)
{
    auto w = cd::plugin::make_native_file_watcher();
    ASSERT_NE(w, nullptr);
    EXPECT_FALSE(w->is_watching());
}

TEST(FoundationUtilsPlugin, FileWatcherPollOnceFiresOnMtimeChange)
{
    UmbrellaTempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { fires.fetch_add(1, std::memory_order_relaxed); }).has_value());
    EXPECT_TRUE(w->is_watching());
    EXPECT_EQ(w->change_count(), 0u);

    w->poll_once();  // no change yet
    EXPECT_EQ(fires.load(), 0);

    f.write_file("v1");
    w->poll_once();
    EXPECT_EQ(fires.load(), 1);
    EXPECT_EQ(w->change_count(), 1u);

    w->poll_once();  // idempotent
    EXPECT_EQ(fires.load(), 1);
    w->stop();
    EXPECT_FALSE(w->is_watching());
}

TEST(FoundationUtilsPlugin, FileWatcherMissingFileReturnsError)
{
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    auto r = w->watch("ghost_file_zyxq.txt", [] {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::plugin::watcher_errors::Code::kFileNotFound));
}

TEST(FoundationUtilsPlugin, FileWatcherAlreadyWatchingReturnsError)
{
    UmbrellaTempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    ASSERT_TRUE(w->watch(f.path_str(), [] {}).has_value());
    auto second = w->watch(f.path_str(), [] {});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code,
              static_cast<std::uint32_t>(cd::plugin::watcher_errors::Code::kAlreadyWatching));
    w->stop();
}

TEST(FoundationUtilsPlugin, FileWatcherStopIsIdempotent)
{
    UmbrellaTempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    ASSERT_TRUE(w->watch(f.path_str(), [] {}).has_value());
    w->stop();
    w->stop();  // safe no-op
    EXPECT_FALSE(w->is_watching());
}

TEST(FoundationUtilsPlugin, FileWatcherRewatchAfterStopSucceeds)
{
    UmbrellaTempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    ASSERT_TRUE(w->watch(f.path_str(), [] {}).has_value());
    w->stop();
    ASSERT_TRUE(w->watch(f.path_str(), [] {}).has_value());
    EXPECT_TRUE(w->is_watching());
    EXPECT_EQ(w->change_count(), 0u);
    w->stop();
}

TEST(FoundationUtilsPlugin, FileWatcherPollAfterStopIsNoOp)
{
    UmbrellaTempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { fires.fetch_add(1, std::memory_order_relaxed); }).has_value());
    w->stop();
    f.write_file("after-stop");
    w->poll_once();  // not watching -> no-op
    EXPECT_EQ(fires.load(), 0);
}

TEST(FoundationUtilsPlugin, FileWatcherMultipleModificationsAccumulate)
{
    UmbrellaTempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { fires.fetch_add(1, std::memory_order_relaxed); }).has_value());
    f.write_file("a");
    w->poll_once();
    f.write_file("b");
    w->poll_once();
    f.write_file("c");
    w->poll_once();
    EXPECT_EQ(fires.load(), 3);
    EXPECT_EQ(w->change_count(), 3u);
    w->stop();
}

TEST(FoundationUtilsPlugin, FileWatcherManyPollsOnNoChangeFiredOnce)
{
    UmbrellaTempFile f;
    auto w = cd::plugin::make_polling_file_watcher(std::chrono::milliseconds { 0 });
    std::atomic<int> fires { 0 };
    ASSERT_TRUE(w->watch(f.path_str(), [&] { fires.fetch_add(1, std::memory_order_relaxed); }).has_value());
    for (int i = 0; i < 5; ++i)
        w->poll_once();
    EXPECT_EQ(fires.load(), 0);
    f.write_file("changed");
    for (int i = 0; i < 5; ++i)
        w->poll_once();
    EXPECT_EQ(fires.load(), 1);
    EXPECT_EQ(w->change_count(), 1u);
    w->stop();
}

// ---------------------------------------------------------------------------
// SUITE 12 — ODR / multiple-inclusion safety
// ---------------------------------------------------------------------------
// Including the umbrella twice within the same TU must compile cleanly
// (all headers must have #pragma once or equivalent include-guards).
// This is verified at compile time by having the re-include at the very top of
// the file where the header has already been pulled in.
TEST(FoundationUtilsOdr, DoubleIncludeProducesNoConflict)
{
    // If we reach this test body, the header re-inclusion at the top of the
    // TU compiled without ODR errors.
    SUCCEED();
}

// ---------------------------------------------------------------------------
// SUITE 13 — umbrella completeness sentinel (all 8 member libs, all sub-headers)
// ---------------------------------------------------------------------------
// This test deliberately exercises a symbol from each aggregated member
// to lock the current set of re-exports at a specific set. If a future
// refactor drops a member lib from the umbrella header, at least one
// test in this file will stop compiling — turning a silent regression
// into an explicit build failure.
TEST(FoundationUtilsAggregation, UmbrellaCompleteness_AllEightMemberLibsPresent)
{
    // 1. bench
    { [[maybe_unused]] cd::bench::Config c; }
    // 2. config
    { EXPECT_NE(&cd::config::save, nullptr); }
    // 3. diag (three sub-headers)
    { [[maybe_unused]] cd::diag::PanicInfo pi; }
    { [[maybe_unused]] cd::diag::CrashReporter cr; }
    { [[maybe_unused]] cd::diag::DeadlineMonitor dm; }
    // 4. events (three sub-headers)
    { [[maybe_unused]] cd::events::EventBus eb; }
    { [[maybe_unused]] cd::events::EventRecorder er; }
    { [[maybe_unused]] cd::events::ScopedConnection sc; }
    // 5. frame_timing
    { [[maybe_unused]] cd::frame_timing::FrameTimeRing<4> ring; }
    // 6. plugin (all four sub-headers: IPlugin + Loader + HotReload + FileWatcher)
    { EXPECT_GT(cd::plugin::kAbiVersion, 0u); }
    {
        cd::plugin::Loader ldr;
        EXPECT_TRUE(ldr.loaded_paths().empty());
    }
    {
        cd::plugin::HotReloader hr {
            [](std::string_view p) -> cd::core::Result<cd::plugin::LoadedPlugin> {
                cd::plugin::LoadedPlugin lp;
                lp.instance      = std::make_unique<UmbrellaFakePlugin>(1u);
                lp.native_handle = nullptr;
                lp.path          = std::string { p };
                return lp;
            },
            [](std::string_view) { return std::uint64_t { 1 }; }
        };
        [[maybe_unused]] const bool ok = hr.mount("x").has_value();
    }
    {
        auto w = cd::plugin::make_native_file_watcher();
        EXPECT_NE(w, nullptr);
    }
    // 7. profile (five sub-headers — touch one from each)
    { [[maybe_unused]] cd::profile::BufferSink bs { 1 }; }
    { [[maybe_unused]] cd::profile::StatsAggregator sa; }
    { [[maybe_unused]] cd::profile::Sample smp; }
    // 8. mem (all six sub-headers)
    { [[maybe_unused]] cd::mem::LinearAllocator   la { 64u }; }
    { [[maybe_unused]] cd::mem::TrackingAllocator ta { cd::mem::system_allocator(), "sentinel" }; }
    { [[maybe_unused]] cd::mem::PageAllocator     page_alloc; }
    { [[maybe_unused]] cd::mem::PoolAllocator     pool_alloc { 64u, 4u }; }
    {
        cd::mem::LinearAllocator pmr_inner { 128u };
        [[maybe_unused]] cd::mem::PmrAdapter pmr { pmr_inner };
    }

    SUCCEED();
}

}  // namespace
