// =============================================================================
// CHROMODYNAMIC — cd/concurrency/JobToken.hpp
// Phase 63.B / Wave 231 — cooperative cancellation token.
//
// `JobToken` is a shared atomic flag pair (cancel, complete) that a
// long-running task polls between work chunks:
//
//   while (!token.is_cancelled()) {
//       work();
//   }
//
// Caller signals cancellation via `token.cancel()`; the worker's next
// `is_cancelled()` check returns true and the worker drops out
// gracefully.
//
// `complete()` signals normal completion (separate from cancellation
// so caller can distinguish). `wait()` blocks until either fires.
//
// Shared via std::shared_ptr<JobTokenImpl>; copies of `JobToken` are
// cheap reference handles.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace cd::concurrency
{

class JobToken
{
public:
    JobToken() : state_ { std::make_shared<State>() } {}

    void cancel() const noexcept
    {
        {
            std::lock_guard<std::mutex> lock { state_->mu };
            state_->cancelled.store(true, std::memory_order_release);
        }
        state_->cv.notify_all();
    }

    void complete() const noexcept
    {
        {
            std::lock_guard<std::mutex> lock { state_->mu };
            state_->completed.store(true, std::memory_order_release);
        }
        state_->cv.notify_all();
    }

    [[nodiscard]] bool is_cancelled() const noexcept
    {
        return state_->cancelled.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_complete() const noexcept
    {
        return state_->completed.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_settled() const noexcept
    {
        return is_cancelled() || is_complete();
    }

    void wait() const
    {
        std::unique_lock<std::mutex> lock { state_->mu };
        state_->cv.wait(lock, [this]
        {
            return state_->cancelled.load(std::memory_order_acquire)
                || state_->completed.load(std::memory_order_acquire);
        });
    }

private:
    struct State
    {
        std::atomic<bool>       cancelled { false };
        std::atomic<bool>       completed { false };
        std::mutex              mu;
        std::condition_variable cv;
    };
    std::shared_ptr<State> state_;
};

}  // namespace cd::concurrency
