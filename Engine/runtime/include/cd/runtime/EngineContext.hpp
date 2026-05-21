// =============================================================================
// CHROMODYNAMIC — cd/runtime/EngineContext.hpp
// ADR-005 + ADR-017 (Sprint S2.10) — foundation services composition root.
//
// EngineContext is the engine's "kernel" — a single object that game code
// uses to reach engine-provided services. It bundles:
//
//   * CVarRegistry            (runtime-tunable parameters)
//   * VirtualFileSystem       (asset / config lookup with overlays)
//   * WorkStealingThreadPool  (job dispatch; lazy-initialised)
//   * DeadlineMonitor         (subsystem heartbeat watchdog)
//   * ILogger                 (engine-wide logger, injectable)
//
// EngineContext is *not* a singleton: multiple may coexist (in-editor preview
// world + main world, headless tests, embedded use). Subsystems should keep
// a non-owning pointer to the context they were initialised against.
//
// Lifetime: services that need shutdown ordering (thread pool, then logger
// flush) are torn down in reverse mount order by `shutdown()` and the dtor.
// =============================================================================
#pragma once

#include <cd/asset/AssetRegistry.hpp>
#include <cd/concurrency/WorkStealingThreadPool.hpp>
#include <cd/core/CVar.hpp>
#include <cd/diag/DeadlineMonitor.hpp>
#include <cd/log/ILogger.hpp>
#include <cd/vfs/VirtualFileSystem.hpp>

#include <cstddef>
#include <memory>

namespace cd::runtime
{

struct EngineContextConfig
{
    /// Worker thread count for the engine's pool. 0 = hardware_concurrency - 1.
    std::size_t worker_threads = 0;

    /// If true, construct the thread pool eagerly. Otherwise it's spun up on
    /// first use (saves cost in headless / test scenarios).
    bool eager_thread_pool = true;
};

class EngineContext
{
public:
    explicit EngineContext(EngineContextConfig cfg = {});
    ~EngineContext();

    EngineContext(const EngineContext&) = delete;
    EngineContext& operator=(const EngineContext&) = delete;
    EngineContext(EngineContext&&) = delete;
    EngineContext& operator=(EngineContext&&) = delete;

    /// Tear down dependent services in reverse mount order. Idempotent.
    void shutdown() noexcept;

    // --- Service accessors ---------------------------------------------------

    [[nodiscard]] cd::core::CVarRegistry& cvars() noexcept
    {
        return cvars_;
    }

    [[nodiscard]] const cd::core::CVarRegistry& cvars() const noexcept
    {
        return cvars_;
    }

    [[nodiscard]] cd::vfs::VirtualFileSystem& vfs() noexcept
    {
        return vfs_;
    }

    [[nodiscard]] const cd::vfs::VirtualFileSystem& vfs() const noexcept
    {
        return vfs_;
    }

    [[nodiscard]] cd::asset::AssetRegistry& assets() noexcept
    {
        return assets_;
    }

    [[nodiscard]] const cd::asset::AssetRegistry& assets() const noexcept
    {
        return assets_;
    }

    [[nodiscard]] cd::diag::DeadlineMonitor& watchdog() noexcept
    {
        return watchdog_;
    }

    [[nodiscard]] cd::concurrency::WorkStealingThreadPool& thread_pool();

    /// Bind a logger. Pass nullptr to clear.
    void set_logger(std::shared_ptr<cd::log::ILogger> logger) noexcept
    {
        logger_ = std::move(logger);
    }

    [[nodiscard]] cd::log::ILogger* logger() const noexcept
    {
        return logger_.get();
    }

    [[nodiscard]] bool has_thread_pool() const noexcept
    {
        return static_cast<bool>(pool_);
    }

    [[nodiscard]] std::size_t worker_count() const noexcept;

private:
    EngineContextConfig cfg_;
    cd::core::CVarRegistry cvars_;
    cd::vfs::VirtualFileSystem vfs_;
    cd::asset::AssetRegistry assets_;
    cd::diag::DeadlineMonitor watchdog_;
    std::unique_ptr<cd::concurrency::WorkStealingThreadPool> pool_;
    std::shared_ptr<cd::log::ILogger> logger_;
    bool shut_down_ { false };
};

}  // namespace cd::runtime
