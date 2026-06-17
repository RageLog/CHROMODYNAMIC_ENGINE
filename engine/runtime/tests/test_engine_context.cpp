// =============================================================================
// CHROMODYNAMIC — cd::runtime::EngineContext tests (Sprint S2.10)
// =============================================================================
#include <cd/asset/IAssetLoader.hpp>
#include <cd/log/ConsoleLogger.hpp>
#include <cd/log/ILogger.hpp>
#include <cd/runtime/EngineContext.hpp>
#include <cd/vfs/MemorySource.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

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

}  // namespace
