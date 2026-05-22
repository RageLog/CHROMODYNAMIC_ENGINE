// =============================================================================
// CHROMODYNAMIC — cd/runtime/EngineContext.cpp
// =============================================================================
#include <cd/runtime/EngineContext.hpp>

namespace cd::runtime
{

EngineContext::EngineContext(EngineContextConfig cfg)
    : cfg_ { cfg }
    , assets_ { vfs_ }
{
    if (cfg_.eager_thread_pool)
    {
        pool_ = std::make_unique<cd::concurrency::WorkStealingThreadPool>(cfg_.worker_threads);
    }
}

EngineContext::~EngineContext()
{
    shutdown();
}

void EngineContext::shutdown() noexcept
{
    if (shut_down_)
        return;
    shut_down_ = true;
    // Stop accepting new jobs first so subsequent flush sees a stable state.
    if (pool_)
    {
        pool_->shutdown();
        pool_.reset();
    }
    if (logger_)
    {
        logger_->flush();
    }
}

cd::concurrency::WorkStealingThreadPool& EngineContext::thread_pool()
{
    if (!pool_)
    {
        pool_ = std::make_unique<cd::concurrency::WorkStealingThreadPool>(cfg_.worker_threads);
    }
    return *pool_;
}

std::size_t EngineContext::worker_count() const noexcept
{
    return pool_ ? pool_->thread_count() : 0u;
}

}  // namespace cd::runtime
