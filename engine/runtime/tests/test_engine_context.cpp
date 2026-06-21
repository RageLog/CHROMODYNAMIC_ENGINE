// =============================================================================
// CHROMODYNAMIC — cd::runtime::EngineContext tests (Sprint S2.10)
// =============================================================================
#include <cd/asset/IAssetLoader.hpp>
#include <cd/diag/DeadlineMonitor.hpp>
#include <cd/log/ConsoleLogger.hpp>
#include <cd/log/ILogger.hpp>
#include <cd/runtime/EngineContext.hpp>
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

}  // namespace
