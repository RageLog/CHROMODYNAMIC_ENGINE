// =============================================================================
// CHROMODYNAMIC — cd::runtime::EngineContext tests (Sprint S2.10)
// =============================================================================
#include <cd/asset/AssetId.hpp>
#include <cd/asset/IAssetLoader.hpp>
#include <cd/asset/SchemaRegistry.hpp>
#include <cd/diag/DeadlineMonitor.hpp>
#include <cd/log/ConsoleLogger.hpp>
#include <cd/log/ILogger.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <cd/runtime/EngineContext.hpp>
#include <cd/runtime/EngineLoop.hpp>
#include <cd/vfs/MemorySource.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace
{

TEST(EngineContext, DefaultConstructsWithEagerPool)
{
    cd::runtime::EngineContext ctx;
    EXPECT_TRUE(ctx.has_thread_pool());
    EXPECT_GT(ctx.worker_count(), 0u);
}

TEST(EngineContext, LazyThreadPool)
{
    cd::runtime::EngineContextConfig cfg;
    cfg.eager_thread_pool = false;
    cd::runtime::EngineContext ctx { cfg };
    EXPECT_FALSE(ctx.has_thread_pool());
    (void)ctx.thread_pool();  // touches lazy init
    EXPECT_TRUE(ctx.has_thread_pool());
}

TEST(EngineContext, CVarRegistryUsable)
{
    cd::runtime::EngineContext ctx;
    ctx.cvars().set("foo", static_cast<std::int64_t>(42));
    EXPECT_EQ(*ctx.cvars().get_as<std::int64_t>("foo"), 42);
}

TEST(EngineContext, VfsLayersComposable)
{
    cd::runtime::EngineContext ctx;
    auto mem = std::make_shared<cd::vfs::MemorySource>("mem");
    mem->put_text("hello.txt", "hi");
    ctx.vfs().mount_back(mem);
    EXPECT_TRUE(ctx.vfs().exists("hello.txt"));
    auto r = ctx.vfs().read("hello.txt");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 2u);
}

TEST(EngineContext, ThreadPoolRunsJobs)
{
    cd::runtime::EngineContext ctx;
    std::atomic<int> n { 0 };
    for (int i = 0; i < 100; ++i)
    {
        ctx.thread_pool().submit_detached(
            [&n]
            {
                n.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }
    ctx.thread_pool().wait_all();
    EXPECT_EQ(n.load(), 100);
}

TEST(EngineContext, LoggerInjectable)
{
    cd::runtime::EngineContext ctx;
    EXPECT_EQ(ctx.logger(), nullptr);
    auto logger = std::make_shared<cd::log::ConsoleLogger>(cd::log::LogLevel::Info);
    ctx.set_logger(logger);
    EXPECT_EQ(ctx.logger(), logger.get());
    ctx.set_logger(nullptr);
    EXPECT_EQ(ctx.logger(), nullptr);
}

TEST(EngineContext, ShutdownIsIdempotent)
{
    cd::runtime::EngineContext ctx;
    ctx.shutdown();
    ctx.shutdown();  // must not crash
    EXPECT_FALSE(ctx.has_thread_pool());
}

TEST(EngineContext, AssetRegistryLoadsThroughVfs)
{
    cd::runtime::EngineContext ctx;
    auto mem = std::make_shared<cd::vfs::MemorySource>("mem");
    mem->put_text("readme.txt", "hello-engine");
    ctx.vfs().mount_back(mem);
    ctx.assets().register_loader(std::make_unique<cd::asset::TextAssetLoader>());

    auto id = ctx.assets().load("text", "readme.txt");
    ASSERT_TRUE(id.has_value());
    auto* txt = dynamic_cast<cd::asset::TextAsset*>(ctx.assets().find(*id));
    ASSERT_NE(txt, nullptr);
    EXPECT_EQ(txt->text(), "hello-engine");
}

TEST(EngineContext, WatchdogIntegration)
{
    cd::runtime::EngineContext ctx;
    std::atomic<int> stalls { 0 };
    ctx.watchdog().on_stall(
        [&](std::string_view, std::chrono::milliseconds)
        {
            stalls.fetch_add(1);
        }
    );
    ctx.watchdog().register_subsystem("test-sub", std::chrono::milliseconds { 1 });
    std::this_thread::sleep_for(std::chrono::milliseconds { 10 });
    EXPECT_GE(ctx.watchdog().evaluate(), 1u);
    EXPECT_GE(stalls.load(), 1);
}

// ---------------------------------------------------------------------------
// BAND5-foundation topup — untested branches of the lazy-DI service context.
// These pin behaviour for the SEALED DI-context-v1 scope (see
// ADR-20260616-band5-foundation-scope.md §runtime).
// ---------------------------------------------------------------------------

// Lazy-init idempotence: thread_pool() must return the SAME pool on every call
// (the `if (!pool_)` guard fires only once), not spin up a new pool each time.
TEST(EngineContext, LazyThreadPoolInitIsIdempotent)
{
    cd::runtime::EngineContextConfig cfg;
    cfg.eager_thread_pool = false;
    cd::runtime::EngineContext ctx { cfg };

    ASSERT_FALSE(ctx.has_thread_pool());
    auto& first  = ctx.thread_pool();  // creates
    auto& second = ctx.thread_pool();  // must reuse
    EXPECT_EQ(&first, &second);
    EXPECT_TRUE(ctx.has_thread_pool());
}

// Explicit worker count is honoured exactly (config-driven ctor branch).
TEST(EngineContext, ExplicitWorkerCountHonoured)
{
    cd::runtime::EngineContextConfig cfg;
    cfg.worker_threads = 2;
    cfg.eager_thread_pool = true;
    cd::runtime::EngineContext ctx { cfg };
    EXPECT_EQ(ctx.worker_count(), 2u);
}

// worker_count() with no pool present (lazy, untouched) reports 0 — the
// "missing service" arm of the accessor, distinct from a live pool.
TEST(EngineContext, WorkerCountZeroWhenPoolAbsent)
{
    cd::runtime::EngineContextConfig cfg;
    cfg.eager_thread_pool = false;
    cd::runtime::EngineContext ctx { cfg };
    EXPECT_FALSE(ctx.has_thread_pool());
    EXPECT_EQ(ctx.worker_count(), 0u);
}

// shutdown() flushes the bound logger exactly once (the `if (logger_)` arm),
// and a second shutdown() does NOT flush again (idempotence past the guard).
TEST(EngineContext, ShutdownFlushesLoggerOnce)
{
    // Minimal ILogger that counts flush() calls.
    class CountingLogger final : public cd::log::ILogger
    {
    public:
        void set_level(cd::log::LogLevel l) noexcept override { level_ = l; }
        [[nodiscard]] cd::log::LogLevel level() const noexcept override { return level_; }
        void flush() noexcept override { ++flushes; }
        int flushes { 0 };

    protected:
        [[nodiscard]] bool should_log(cd::log::LogLevel) const noexcept override { return false; }
        void log_impl(cd::log::LogLevel, const std::source_location*, std::string_view) override {}

    private:
        cd::log::LogLevel level_ { cd::log::LogLevel::Info };
    };

    auto logger = std::make_shared<CountingLogger>();
    {
        cd::runtime::EngineContext ctx;
        ctx.set_logger(logger);
        EXPECT_EQ(logger->flushes, 0);
        ctx.shutdown();
        EXPECT_EQ(logger->flushes, 1);
        ctx.shutdown();  // guard returns early — no second flush
        EXPECT_EQ(logger->flushes, 1);
    }  // dtor runs shutdown() again; still early-returns — no extra flush
    EXPECT_EQ(logger->flushes, 1);
}

// Re-acquiring the pool AFTER shutdown re-creates a usable pool (the lazy
// guard re-fires; the one-shot shutdown flag does not block re-init). This
// documents the context's "services are re-obtainable" contract under the
// sealed scope; no crash, and the new pool runs jobs.
TEST(EngineContext, ThreadPoolReacquirableAfterShutdown)
{
    cd::runtime::EngineContext ctx;
    ctx.shutdown();
    ASSERT_FALSE(ctx.has_thread_pool());

    auto& pool = ctx.thread_pool();  // lazy re-create
    EXPECT_TRUE(ctx.has_thread_pool());

    std::atomic<int> n { 0 };
    pool.submit_detached([&n] { n.fetch_add(1, std::memory_order_relaxed); });
    pool.wait_all();
    EXPECT_EQ(n.load(), 1);
}

// ---------------------------------------------------------------------------
// Tier-70 gap-fill — additional coverage of DI-context accessors and guards.
// ---------------------------------------------------------------------------

// CVarRegistry through context: bool type stored and retrieved correctly.
TEST(EngineContext, CvarBoolType)
{
    cd::runtime::EngineContext ctx;
    ctx.cvars().set("dbg.show_fps", true);
    const auto v = ctx.cvars().get_as<bool>("dbg.show_fps");
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(*v);
}

// CVarRegistry through context: double type round-trips without conversion loss.
TEST(EngineContext, CvarDoubleType)
{
    cd::runtime::EngineContext ctx;
    ctx.cvars().set("r.gamma", 2.2);
    const auto v = ctx.cvars().get_as<double>("r.gamma");
    ASSERT_TRUE(v.has_value());
    EXPECT_DOUBLE_EQ(*v, 2.2);
}

// CVarRegistry through context: string type stores and retrieves the string.
TEST(EngineContext, CvarStringType)
{
    cd::runtime::EngineContext ctx;
    ctx.cvars().set("app.title", std::string { "ChromoEngine" });
    const auto v = ctx.cvars().get_as<std::string>("app.title");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "ChromoEngine");
}

// Negative: getting a key that was never set returns nullopt.
TEST(EngineContext, CvarMissingKeyReturnsNullopt)
{
    cd::runtime::EngineContext ctx;
    EXPECT_FALSE(ctx.cvars().get("nonexistent.key").has_value());
    EXPECT_FALSE(ctx.cvars().get_as<std::int64_t>("nonexistent.key").has_value());
}

// Overwrite: the second set() wins; the key is not duplicated.
TEST(EngineContext, CvarOverwriteUpdatesValue)
{
    cd::runtime::EngineContext ctx;
    ctx.cvars().set("net.port", static_cast<std::int64_t>(7000));
    ctx.cvars().set("net.port", static_cast<std::int64_t>(8080));
    EXPECT_EQ(*ctx.cvars().get_as<std::int64_t>("net.port"), static_cast<std::int64_t>(8080));
    EXPECT_EQ(ctx.cvars().size(), 1u);  // still one entry
}

// VFS priority: a front-mounted source shadows a back-mounted one for the
// same path. This pins the priority-resolution contract of VirtualFileSystem
// through the context accessor.
TEST(EngineContext, VfsFrontMountShadowsBack)
{
    cd::runtime::EngineContext ctx;
    auto base = std::make_shared<cd::vfs::MemorySource>("base");
    base->put_text("cfg.txt", "base-value");
    auto override_src = std::make_shared<cd::vfs::MemorySource>("override");
    override_src->put_text("cfg.txt", "override-value");

    ctx.vfs().mount_back(base);
    ctx.vfs().mount_front(override_src);  // higher priority

    const auto r = ctx.vfs().read("cfg.txt");
    ASSERT_TRUE(r.has_value());
    const std::string content(reinterpret_cast<const char*>(r->data()), r->size());
    EXPECT_EQ(content, "override-value");
}

// Negative: reading a path that no mounted source contains returns an error.
TEST(EngineContext, VfsNotFoundReturnsError)
{
    cd::runtime::EngineContext ctx;
    auto mem = std::make_shared<cd::vfs::MemorySource>("mem");
    ctx.vfs().mount_back(mem);
    EXPECT_FALSE(ctx.vfs().exists("ghost.txt"));
    EXPECT_FALSE(ctx.vfs().read("ghost.txt").has_value());
}

// worker_count() after lazy init must report the actual pool size, not 0.
TEST(EngineContext, LazyWorkerCountAfterInitIsNonZero)
{
    cd::runtime::EngineContextConfig cfg;
    cfg.eager_thread_pool = false;
    cd::runtime::EngineContext ctx { cfg };

    ASSERT_EQ(ctx.worker_count(), 0u);  // before init
    (void)ctx.thread_pool();            // trigger lazy init
    EXPECT_GT(ctx.worker_count(), 0u);  // after init
}

// Pool stats: tasks_completed is observable through the pool returned by the
// context accessor after wait_all() drains all submitted work.
TEST(EngineContext, PoolStatsTasksCompletedObservable)
{
    cd::runtime::EngineContext ctx;
    constexpr int kJobs = 20;
    for (int i = 0; i < kJobs; ++i)
    {
        ctx.thread_pool().submit_detached([] {});
    }
    ctx.thread_pool().wait_all();
    EXPECT_GE(ctx.thread_pool().stats().tasks_completed.load(), static_cast<std::uint64_t>(kJobs));
}

// Watchdog via injected clock: heartbeat refreshed → evaluate() sees 0 stalls.
// Uses a synthetic clock to avoid any sleep_for.
TEST(EngineContext, WatchdogNoStallWhenHeartbeatRefreshed)
{
    using Clock = cd::diag::DeadlineMonitor::Clock;
    auto base_tp = Clock::now();
    // Clock starts at base_tp; we advance it manually via shared mutable.
    struct FakeClock
    {
        Clock::time_point tp;
    };
    auto fc = std::make_shared<FakeClock>();
    fc->tp = base_tp;

    // EngineContext wires its DeadlineMonitor with the default Clock::now()
    // constructor, so we build a standalone monitor with the injected clock
    // to prove the watchdog contract pinned by the context.
    cd::diag::DeadlineMonitor monitor { [fc] { return fc->tp; } };

    std::atomic<int> stalls { 0 };
    monitor.on_stall([&](std::string_view, std::chrono::milliseconds) { stalls.fetch_add(1); });
    monitor.register_subsystem("s", std::chrono::milliseconds { 100 });

    // Advance time by 50 ms — within deadline; heartbeat refreshes the window.
    fc->tp = base_tp + std::chrono::milliseconds { 50 };
    monitor.heartbeat("s");

    // Advance past original deadline (150 ms total), but heartbeat was at 50 ms
    // so only 100 ms have elapsed since the last beat → still within window.
    fc->tp = base_tp + std::chrono::milliseconds { 149 };
    EXPECT_EQ(monitor.evaluate(), 0u);
    EXPECT_EQ(stalls.load(), 0);
}

// Watchdog via injected clock: a disarmed subsystem is skipped by evaluate()
// even if its deadline has expired.
TEST(EngineContext, WatchdogDisarmSkipsEvaluation)
{
    using Clock = cd::diag::DeadlineMonitor::Clock;
    const auto base_tp = Clock::now();

    auto fc = std::make_shared<Clock::time_point>(base_tp);
    cd::diag::DeadlineMonitor monitor { [fc] { return *fc; } };

    std::atomic<int> stalls { 0 };
    monitor.on_stall([&](std::string_view, std::chrono::milliseconds) { stalls.fetch_add(1); });
    monitor.register_subsystem("worker", std::chrono::milliseconds { 10 });

    monitor.disarm("worker");

    // Advance clock well past the deadline.
    *fc = base_tp + std::chrono::milliseconds { 200 };
    EXPECT_EQ(monitor.evaluate(), 0u);  // disarmed → skipped
    EXPECT_EQ(stalls.load(), 0);
}

// Two EngineContext instances must be fully independent: CVars set on one
// must not be visible on the other.
TEST(EngineContext, ContextsAreIndependent)
{
    cd::runtime::EngineContext a;
    cd::runtime::EngineContext b;

    a.cvars().set("key", static_cast<std::int64_t>(1));
    b.cvars().set("key", static_cast<std::int64_t>(2));

    EXPECT_EQ(*a.cvars().get_as<std::int64_t>("key"), static_cast<std::int64_t>(1));
    EXPECT_EQ(*b.cvars().get_as<std::int64_t>("key"), static_cast<std::int64_t>(2));

    // Loggers are also independent (nullptr by default on both).
    EXPECT_EQ(a.logger(), nullptr);
    EXPECT_EQ(b.logger(), nullptr);
    auto logger = std::make_shared<cd::log::ConsoleLogger>(cd::log::LogLevel::Info);
    a.set_logger(logger);
    EXPECT_NE(a.logger(), nullptr);
    EXPECT_EQ(b.logger(), nullptr);
}

// Negative: loading an asset with a path that does not exist in VFS returns
// an error (kVfsReadFailed), not a valid AssetId.
TEST(EngineContext, AssetLoadMissingPathReturnsError)
{
    cd::runtime::EngineContext ctx;
    ctx.assets().register_loader(std::make_unique<cd::asset::TextAssetLoader>());
    // No VFS sources mounted → path cannot resolve.
    const auto result = ctx.assets().load("text", "nonexistent/asset.txt");
    EXPECT_FALSE(result.has_value());
}

// Asset load idempotence: two load() calls for the same path return the same
// AssetId, and find() returns the same pointer both times.
TEST(EngineContext, AssetLoadIsIdempotent)
{
    cd::runtime::EngineContext ctx;
    auto mem = std::make_shared<cd::vfs::MemorySource>("mem");
    mem->put_text("data.txt", "content");
    ctx.vfs().mount_back(mem);
    ctx.assets().register_loader(std::make_unique<cd::asset::TextAssetLoader>());

    const auto id1 = ctx.assets().load("text", "data.txt");
    const auto id2 = ctx.assets().load("text", "data.txt");
    ASSERT_TRUE(id1.has_value());
    ASSERT_TRUE(id2.has_value());
    EXPECT_EQ(*id1, *id2);
    EXPECT_EQ(ctx.assets().find(*id1), ctx.assets().find(*id2));
    EXPECT_EQ(ctx.assets().cached_count(), 1u);
}

// ---------------------------------------------------------------------------
// Phase 1268 — cd::runtime charter close-out: ref-counting, dependency
// tracking, hot-reload bookkeeping, schema/type registry wired through
// EngineContext. AAA + edge + negative coverage.
// ---------------------------------------------------------------------------

// --- AssetRefCount through context -----------------------------------------

// acquire() increments and returns the new count through the context accessor.
TEST(EngineContextRefCount, AcquireIncrementsCount)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("tex/wall.cdtex");
    EXPECT_EQ(ctx.ref_count().acquire(id), 1);
    EXPECT_EQ(ctx.ref_count().acquire(id), 2);
    EXPECT_EQ(ctx.ref_count().count_of(id), 2);
}

// release() decrements; hitting zero removes the tracking entry.
TEST(EngineContextRefCount, ReleaseDecrements)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("mesh/soldier.cdmesh");
    ctx.ref_count().acquire(id);
    ctx.ref_count().acquire(id);
    EXPECT_EQ(ctx.ref_count().release(id), 1);
    EXPECT_EQ(ctx.ref_count().release(id), 0);
    EXPECT_FALSE(ctx.ref_count().is_held(id));
    EXPECT_EQ(ctx.ref_count().tracked_count(), 0u);
}

// Double-free guard: releasing an asset that was never acquired returns 0
// and does not underflow.
TEST(EngineContextRefCount, DoubleFreeGuardNeverUnderflows)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("audio/ambient.wav");
    EXPECT_EQ(ctx.ref_count().release(id), 0);  // never acquired
    ctx.ref_count().acquire(id);
    EXPECT_EQ(ctx.ref_count().release(id), 0);  // back to zero
    EXPECT_EQ(ctx.ref_count().release(id), 0);  // extra release — no-op
    EXPECT_EQ(ctx.ref_count().tracked_count(), 0u);
}

// Invalid AssetId (null) is silently rejected; tracked_count stays 0.
TEST(EngineContextRefCount, NullIdRejected)
{
    cd::runtime::EngineContext ctx;
    EXPECT_EQ(ctx.ref_count().acquire(cd::asset::AssetId {}), 0);
    EXPECT_EQ(ctx.ref_count().tracked_count(), 0u);
}

// Multiple distinct assets are tracked independently.
TEST(EngineContextRefCount, MultipleAssetsIndependent)
{
    cd::runtime::EngineContext ctx;
    const auto id_a = cd::asset::AssetId::from_path("a.cdtex");
    const auto id_b = cd::asset::AssetId::from_path("b.cdmesh");
    ctx.ref_count().acquire(id_a);
    ctx.ref_count().acquire(id_a);
    ctx.ref_count().acquire(id_b);
    EXPECT_EQ(ctx.ref_count().count_of(id_a), 2);
    EXPECT_EQ(ctx.ref_count().count_of(id_b), 1);
    EXPECT_EQ(ctx.ref_count().tracked_count(), 2u);
}

// clear() resets all counts.
TEST(EngineContextRefCount, ClearResetsAll)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("x.wav");
    ctx.ref_count().acquire(id);
    ctx.ref_count().clear();
    EXPECT_EQ(ctx.ref_count().tracked_count(), 0u);
    EXPECT_FALSE(ctx.ref_count().is_held(id));
}

// const context accessor returns the same ref_count state.
TEST(EngineContextRefCount, ConstAccessorReflectsState)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("const.cdtex");
    ctx.ref_count().acquire(id);

    const cd::runtime::EngineContext& cctx = ctx;
    EXPECT_EQ(cctx.ref_count().count_of(id), 1);
    EXPECT_TRUE(cctx.ref_count().is_held(id));
}

// --- DependencyGraph through context ----------------------------------------

// depend() registers a parent→child edge; dependents_of(child) returns parent.
TEST(EngineContextDepGraph, OneHopDependency)
{
    cd::runtime::EngineContext ctx;
    const auto mat = cd::asset::AssetId::from_path("mat/standard.json");
    const auto tex = cd::asset::AssetId::from_path("tex/albedo.cdtex");
    ctx.dep_graph().depend(mat, tex);
    const auto deps = ctx.dep_graph().dependents_of(tex);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], mat);
}

// transitive_dependents() walks the full reverse closure (BFS).
TEST(EngineContextDepGraph, TransitiveDependentsClosureBfs)
{
    cd::runtime::EngineContext ctx;
    const auto scene = cd::asset::AssetId::from_path("scene/main.json");
    const auto mat   = cd::asset::AssetId::from_path("mat/pbr.json");
    const auto tex   = cd::asset::AssetId::from_path("tex/normal.cdtex");
    ctx.dep_graph().depend(scene, mat);
    ctx.dep_graph().depend(mat, tex);
    const auto closure = ctx.dep_graph().transitive_dependents(tex);
    EXPECT_EQ(closure.size(), 2u);
}

// No edges → dependents_of returns empty vector.
TEST(EngineContextDepGraph, NoDependentsReturnsEmpty)
{
    cd::runtime::EngineContext ctx;
    const auto lone = cd::asset::AssetId::from_path("lone.cdmesh");
    EXPECT_TRUE(ctx.dep_graph().dependents_of(lone).empty());
    EXPECT_TRUE(ctx.dep_graph().transitive_dependents(lone).empty());
}

// Invalid id (null) edges are silently discarded; edge_node_count stays 0.
TEST(EngineContextDepGraph, NullIdEdgeRejected)
{
    cd::runtime::EngineContext ctx;
    const auto valid = cd::asset::AssetId::from_path("valid.cdtex");
    ctx.dep_graph().depend(cd::asset::AssetId {}, valid);
    ctx.dep_graph().depend(valid, cd::asset::AssetId {});
    EXPECT_EQ(ctx.dep_graph().edge_node_count(), 0u);
}

// clear() removes all edges.
TEST(EngineContextDepGraph, ClearDropsAllEdges)
{
    cd::runtime::EngineContext ctx;
    const auto a = cd::asset::AssetId::from_path("a.json");
    const auto b = cd::asset::AssetId::from_path("b.cdtex");
    ctx.dep_graph().depend(a, b);
    ctx.dep_graph().clear();
    EXPECT_EQ(ctx.dep_graph().edge_node_count(), 0u);
    EXPECT_TRUE(ctx.dep_graph().dependents_of(b).empty());
}

// const accessor: read-only dependents_of visible through const context ref.
TEST(EngineContextDepGraph, ConstAccessorVisible)
{
    cd::runtime::EngineContext ctx;
    const auto p = cd::asset::AssetId::from_path("p.json");
    const auto c = cd::asset::AssetId::from_path("c.cdtex");
    ctx.dep_graph().depend(p, c);

    const cd::runtime::EngineContext& cctx = ctx;
    const auto deps = cctx.dep_graph().dependents_of(c);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], p);
}

// --- HotReloadQueue through context -----------------------------------------

// notify() marks an asset dirty; is_pending() reflects it.
TEST(EngineContextHotReload, NotifyMarksPending)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("shaders/lit.frag.glsl");
    ctx.hot_reload().notify(id);
    EXPECT_TRUE(ctx.hot_reload().is_pending(id));
    EXPECT_EQ(ctx.hot_reload().pending_count(), 1u);
}

// Duplicate notify() coalesces to a single pending entry.
TEST(EngineContextHotReload, DuplicateNotifyCoalesces)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("a.frag.glsl");
    ctx.hot_reload().notify(id);
    ctx.hot_reload().notify(id);
    ctx.hot_reload().notify(id);
    EXPECT_EQ(ctx.hot_reload().pending_count(), 1u);
}

// drain() fires the callback once per dirty asset then clears the queue.
TEST(EngineContextHotReload, DrainFiresEachOnceAndClears)
{
    cd::runtime::EngineContext ctx;
    const auto id_a = cd::asset::AssetId::from_path("a.vert");
    const auto id_b = cd::asset::AssetId::from_path("b.frag");
    ctx.hot_reload().notify(id_a);
    ctx.hot_reload().notify(id_b);

    int fired = 0;
    ctx.hot_reload().drain([&fired](cd::asset::AssetId) { ++fired; });
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(ctx.hot_reload().pending_count(), 0u);
}

// Notify with null id is ignored.
TEST(EngineContextHotReload, NullIdIgnored)
{
    cd::runtime::EngineContext ctx;
    ctx.hot_reload().notify(cd::asset::AssetId {});
    EXPECT_EQ(ctx.hot_reload().pending_count(), 0u);
}

// clear() drops all pending entries without firing callbacks.
TEST(EngineContextHotReload, ClearNoDrain)
{
    cd::runtime::EngineContext ctx;
    ctx.hot_reload().notify(cd::asset::AssetId::from_path("x.cdtex"));
    ctx.hot_reload().clear();
    EXPECT_EQ(ctx.hot_reload().pending_count(), 0u);
}

// Integration: notify dependency children → drain collects them all.
TEST(EngineContextHotReload, NotifyMultipleAssetsDrainedOnce)
{
    cd::runtime::EngineContext ctx;
    const auto tex_a = cd::asset::AssetId::from_path("t/a.cdtex");
    const auto tex_b = cd::asset::AssetId::from_path("t/b.cdtex");
    const auto mat   = cd::asset::AssetId::from_path("m/pbr.json");
    ctx.hot_reload().notify(tex_a);
    ctx.hot_reload().notify(tex_b);
    ctx.hot_reload().notify(mat);

    int count = 0;
    ctx.hot_reload().drain([&count](cd::asset::AssetId) { ++count; });
    EXPECT_EQ(count, 3);
}

// const accessor reports pending state correctly.
TEST(EngineContextHotReload, ConstAccessorIsPending)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("q.cdtex");
    ctx.hot_reload().notify(id);

    const cd::runtime::EngineContext& cctx = ctx;
    EXPECT_TRUE(cctx.hot_reload().is_pending(id));
}

// --- SchemaRegistry (type registry) through context -------------------------

// register_schema + find round-trip through the context.
TEST(EngineContextSchemas, RegisterAndFind)
{
    cd::runtime::EngineContext ctx;
    cd::asset::Schema s;
    s.name    = "material.pbr";
    s.version = 1;
    s.fields  = {
        { "albedo",    cd::asset::FieldType::kVec4f, false },
        { "roughness", cd::asset::FieldType::kFloat, false },
        { "normal_map",cd::asset::FieldType::kHandleTexture, true },
    };
    ctx.schemas().register_schema(s);
    EXPECT_EQ(ctx.schemas().size(), 1u);
    const auto* found = ctx.schemas().find("material.pbr");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->fields.size(), 3u);
    EXPECT_NE(found->find_field("albedo"), nullptr);
    EXPECT_EQ(found->find_field("missing"), nullptr);
}

// Negative: find() for an unregistered schema name returns nullptr.
TEST(EngineContextSchemas, FindUnknownReturnsNull)
{
    cd::runtime::EngineContext ctx;
    EXPECT_EQ(ctx.schemas().find("nope.schema"), nullptr);
}

// validate_payload: all required fields present → success.
TEST(EngineContextSchemas, ValidatePayloadAllRequiredPresent)
{
    cd::runtime::EngineContext ctx;
    cd::asset::Schema s;
    s.name    = "event.spawn";
    s.version = 1;
    s.fields  = {
        { "entity_id", cd::asset::FieldType::kUint64, false },
        { "position",  cd::asset::FieldType::kVec3f,  false },
        { "debug_tag", cd::asset::FieldType::kString,  true  },
    };
    ctx.schemas().register_schema(std::move(s));

    const std::vector<std::string> present { "entity_id", "position" };
    EXPECT_TRUE(ctx.schemas().validate_payload("event.spawn", present).has_value());
}

// Negative: missing a required field → kMissingRequiredField error.
TEST(EngineContextSchemas, ValidatePayloadMissingRequiredField)
{
    cd::runtime::EngineContext ctx;
    cd::asset::Schema s;
    s.name    = "event.damage";
    s.version = 1;
    s.fields  = {
        { "amount",    cd::asset::FieldType::kFloat,  false },
        { "source_id", cd::asset::FieldType::kUint64, false },
    };
    ctx.schemas().register_schema(std::move(s));

    const std::vector<std::string> only_one { "amount" };
    const auto result = ctx.schemas().validate_payload("event.damage", only_one);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code,
              static_cast<std::uint32_t>(cd::asset::schema_errors::Code::kMissingRequiredField));
}

// Negative: validate_payload on unknown schema name → kUnknownSchema.
TEST(EngineContextSchemas, ValidatePayloadUnknownSchema)
{
    cd::runtime::EngineContext ctx;
    const auto r = ctx.schemas().validate_payload("ghost.schema", {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code,
              static_cast<std::uint32_t>(cd::asset::schema_errors::Code::kUnknownSchema));
}

// Overwrite: second register_schema with the same name replaces the first.
TEST(EngineContextSchemas, OverwriteReplacesSchema)
{
    cd::runtime::EngineContext ctx;
    cd::asset::Schema s1;
    s1.name    = "mat.simple";
    s1.version = 1;
    s1.fields  = { { "color", cd::asset::FieldType::kVec4f, false } };
    ctx.schemas().register_schema(s1);

    cd::asset::Schema s2;
    s2.name    = "mat.simple";
    s2.version = 2;
    s2.fields  = {
        { "color",     cd::asset::FieldType::kVec4f, false },
        { "emissive",  cd::asset::FieldType::kVec3f, true  },
    };
    ctx.schemas().register_schema(std::move(s2));

    EXPECT_EQ(ctx.schemas().size(), 1u);  // still one entry
    const auto* found = ctx.schemas().find("mat.simple");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->version, 2u);
    EXPECT_EQ(found->fields.size(), 2u);
}

// const accessor: schemas visible through const context ref.
TEST(EngineContextSchemas, ConstAccessorVisible)
{
    cd::runtime::EngineContext ctx;
    cd::asset::Schema s;
    s.name    = "cfg.graphics";
    s.version = 1;
    s.fields  = { { "msaa", cd::asset::FieldType::kInt32, false } };
    ctx.schemas().register_schema(std::move(s));

    const cd::runtime::EngineContext& cctx = ctx;
    EXPECT_EQ(cctx.schemas().size(), 1u);
    EXPECT_NE(cctx.schemas().find("cfg.graphics"), nullptr);
}

// --- Cross-service integration -----------------------------------------------

// Load an asset via registry, acquire a ref-count, register dep edge, then
// notify hot-reload.  All four new services interact through one context.
TEST(EngineContextIntegration, RefCountDepGraphHotReloadSchema)
{
    cd::runtime::EngineContext ctx;

    // VFS + loader setup.
    auto mem = std::make_shared<cd::vfs::MemorySource>("mem");
    mem->put_text("mat/pbr.json", "{}");
    mem->put_text("tex/albedo.cdtex", "data");
    ctx.vfs().mount_back(mem);
    ctx.assets().register_loader(std::make_unique<cd::asset::TextAssetLoader>());

    // Load two assets.
    const auto mat_id = ctx.assets().load("text", "mat/pbr.json");
    const auto tex_id = ctx.assets().load("text", "tex/albedo.cdtex");
    ASSERT_TRUE(mat_id.has_value());
    ASSERT_TRUE(tex_id.has_value());

    // Ref-count: hold both.
    ctx.ref_count().acquire(*mat_id);
    ctx.ref_count().acquire(*tex_id);
    EXPECT_EQ(ctx.ref_count().count_of(*mat_id), 1);
    EXPECT_EQ(ctx.ref_count().count_of(*tex_id), 1);

    // Dependency: mat depends on tex.
    ctx.dep_graph().depend(*mat_id, *tex_id);
    ASSERT_EQ(ctx.dep_graph().dependents_of(*tex_id).size(), 1u);

    // Hot-reload: tex changes; notify, verify mat is a dependent.
    ctx.hot_reload().notify(*tex_id);
    const auto affected = ctx.dep_graph().dependents_of(*tex_id);
    EXPECT_EQ(affected[0], *mat_id);

    // Drain the queue.
    int drained = 0;
    ctx.hot_reload().drain([&drained](cd::asset::AssetId) { ++drained; });
    EXPECT_EQ(drained, 1);

    // Release refs.
    ctx.ref_count().release(*mat_id);
    ctx.ref_count().release(*tex_id);
    EXPECT_EQ(ctx.ref_count().tracked_count(), 0u);

    // Schema: register and validate a payload.
    cd::asset::Schema s;
    s.name    = "mat.pbr";
    s.version = 1;
    s.fields  = { { "albedo", cd::asset::FieldType::kVec4f, false } };
    ctx.schemas().register_schema(std::move(s));
    const std::vector<std::string> fields { "albedo" };
    EXPECT_TRUE(ctx.schemas().validate_payload("mat.pbr", fields).has_value());
}

// Shutdown clears no user data in new services (they are value members, not
// owned pointers; shutdown touches only the thread pool and logger).
TEST(EngineContextIntegration, ShutdownDoesNotCorruptNewServices)
{
    cd::runtime::EngineContext ctx;
    const auto id = cd::asset::AssetId::from_path("post/shutdown.cdtex");
    ctx.ref_count().acquire(id);
    ctx.dep_graph().depend(id, cd::asset::AssetId::from_path("dep.cdtex"));
    ctx.hot_reload().notify(id);

    ctx.shutdown();

    // Services remain usable after shutdown (they are plain value members).
    EXPECT_EQ(ctx.ref_count().count_of(id), 1);
    EXPECT_EQ(ctx.dep_graph().edge_node_count(), 1u);
    EXPECT_TRUE(ctx.hot_reload().is_pending(id));
}

TEST(EngineLoopWave6, All12ShowcasesStepThroughEngineLoopAndPieRestoresSnapshot)
{
    cd::runtime::EngineContextConfig cfg;
    cfg.eager_thread_pool = false;
    cd::runtime::EngineContext ctx(cfg);
    cd::runtime::EngineLoop loop(ctx, 1.0F / 60.0F);

    cd::rhi::NullDevice dev;
    cd::rhi::TextureDesc back_desc {};
    back_desc.extent = { 640U, 360U, 1U };
    back_desc.format = cd::rhi::Format::kRGBA8Unorm;
    back_desc.usage = cd::rhi::TextureUsage::kColorAttachment;
    auto back_tex = dev.create_texture(back_desc);
    ASSERT_TRUE(back_tex.has_value());

    for (std::size_t i = 0; i < cd::gameplay_director::kShowcaseCount; ++i)
    {
        const auto id = static_cast<cd::gameplay_director::ShowcaseId>(i);
        loop.set_stage(cd::gameplay_director::build_showcase_stage(id));
        const auto rep = loop.step_frame(1.0F / 60.0F, &dev, *back_tex);
        EXPECT_EQ(rep.fixed_steps_executed, 1U);
        EXPECT_GT(rep.framegraph_passes_compiled, 10U);
    }

    // Play-In-Editor (PIE) snapshot & restore verification on Showcase 05 (Avatar Locomotion)
    loop.set_stage(cd::gameplay_director::build_showcase_stage(
        cd::gameplay_director::ShowcaseId::kAvatarLocomotionAnimIk));
    loop.begin_pie_session();
    EXPECT_TRUE(loop.is_pie_active());
    EXPECT_FALSE(loop.pie_snapshot_cdstage().empty());

    auto avatars = loop.stage().find_all<cd::gameplay_vessel::Avatar>();
    ASSERT_EQ(avatars.size(), 1U);
    const float initial_x = avatars[0]->world_position()[0];
    avatars[0]->apply_movement_input({ 1.0F, 0.0F, 0.0F });

    (void)loop.step_frame(0.1F, &dev, *back_tex);
    EXPECT_GT(avatars[0]->world_position()[0], initial_x);

    loop.stop_pie_session();
    EXPECT_FALSE(loop.is_pie_active());
    auto restored_avatars = loop.stage().find_all<cd::gameplay_vessel::Avatar>();
    ASSERT_EQ(restored_avatars.size(), 1U);
    EXPECT_FLOAT_EQ(restored_avatars[0]->world_position()[0], initial_x);
}

}  // namespace
