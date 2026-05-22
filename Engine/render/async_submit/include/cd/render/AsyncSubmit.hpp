// =============================================================================
// CHROMODYNAMIC — cd/render/AsyncSubmit.hpp
// Phase 4 / Sprint S4.4 — minimal render thread primitive.
//
// One worker thread + one job slot. The owning thread calls
//   wait_idle();                  // drain previous frame
//   auto frame = renderer.begin_frame();
//   // ... record into frame.command_buffer ...
//   async.enqueue([&]{ renderer.end_frame(); });
//
// This is the "one frame deep" producer/consumer pattern. It does NOT
// pipeline beyond a single frame — that requires N-deep ring + per-
// frame fences (Phase 5). What we get from this minimal version:
//
//   * Game logic for frame N+1 overlaps with GPU submit/present of N.
//   * Single-slot semantics → no races between game and submit on the
//     same Frame resources.
//   * Predictable shutdown: destructor joins the worker after draining.
//
// Concurrency invariants:
//   * enqueue() must be called from the SAME thread that owns the
//     surrounding renderer (single-producer). The class does not gate
//     multi-producer; that's a programming error.
//   * The job function runs on the worker thread. Anything it touches
//     must either be thread-safe or owned exclusively by the worker
//     between enqueue() and wait_idle().
//   * wait_idle() blocks until the last-enqueued job has completed.
//   * Destructor calls wait_idle() then joins the thread cleanly.
//
// Phase 4 closure ADR pre-conditions for the "real" render thread are
// captured separately (ADR-20260522-wave20-render-thread-deferral).
// This primitive is the building block that those conditions, once
// satisfied, would build on top of.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace cd::render
{

class AsyncSubmit
{
public:
    using Job = std::function<void()>;

    AsyncSubmit()
        : thread_ { [this] { worker_loop_(); } }
    {
    }

    ~AsyncSubmit()
    {
        // Drain any pending job, then signal stop and join.
        wait_idle();
        {
            std::lock_guard guard { mu_ };
            stop_ = true;
        }
        have_job_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }

    AsyncSubmit(const AsyncSubmit&) = delete;
    AsyncSubmit& operator=(const AsyncSubmit&) = delete;
    AsyncSubmit(AsyncSubmit&&) = delete;
    AsyncSubmit& operator=(AsyncSubmit&&) = delete;

    /// Enqueue a job. If a previous job is still running, this blocks
    /// until it completes (single-slot pipeline). On return, the new
    /// job has been handed to the worker; it may not have started yet.
    void enqueue(Job job)
    {
        std::unique_lock guard { mu_ };
        // Wait for any currently-running job to finish.
        job_done_.wait(guard, [this] { return !busy_; });
        pending_ = std::move(job);
        busy_ = true;
        ++enqueue_count_;
        guard.unlock();
        have_job_.notify_one();
    }

    /// Block until the worker is idle (no pending or running job).
    void wait_idle()
    {
        std::unique_lock guard { mu_ };
        job_done_.wait(guard, [this] { return !busy_; });
    }

    /// True when the worker is currently running a job (or has one
    /// queued that has not yet been picked up). Cheap; suitable for
    /// busy-waiting in tests but use `wait_idle()` in production.
    [[nodiscard]] bool is_busy() const
    {
        std::lock_guard guard { mu_ };
        return busy_;
    }

    /// Total enqueue calls since construction. Test introspection.
    [[nodiscard]] std::uint64_t enqueue_count() const noexcept
    {
        return enqueue_count_;
    }

    /// Total jobs completed by the worker. Test introspection.
    [[nodiscard]] std::uint64_t completion_count() const noexcept
    {
        return completion_count_.load(std::memory_order_acquire);
    }

private:
    void worker_loop_()
    {
        while (true)
        {
            Job job;
            {
                std::unique_lock guard { mu_ };
                have_job_.wait(guard, [this] { return busy_ || stop_; });
                if (stop_ && !busy_)
                    return;
                job = std::move(pending_);
                pending_ = nullptr;
            }
            if (job)
                job();  // run outside the lock — must not access shared state.
            {
                std::lock_guard guard { mu_ };
                busy_ = false;
                completion_count_.fetch_add(1, std::memory_order_release);
            }
            job_done_.notify_all();
        }
    }

    mutable std::mutex mu_;
    std::condition_variable have_job_;
    std::condition_variable job_done_;
    Job pending_ {};
    bool busy_ { false };
    bool stop_ { false };
    std::uint64_t enqueue_count_ { 0 };
    std::atomic<std::uint64_t> completion_count_ { 0 };
    std::thread thread_;
};

}  // namespace cd::render
