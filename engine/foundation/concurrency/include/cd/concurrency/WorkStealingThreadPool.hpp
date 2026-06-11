// =============================================================================
// CHROMODYNAMIC — cd/concurrency/WorkStealingThreadPool.hpp
// ADR-015 + ADR-017 P3 (Sprint S2.5)
//
// N-worker pool with per-worker Chase-Lev work-stealing deque. Each worker
// owns one WSD; the submit() path round-robins across deques. A worker drains
// its own deque LIFO, then attempts to steal from random victims when empty.
//
// API mirrors cd::concurrency::ThreadPool to ease swap-in:
//   submit / submit_with_priority / submit_detached / spawn_detached / wait_all
//
// Differences vs. v1 ThreadPool:
//   - Priority is recorded but currently advisory (jobs run FIFO inside one
//     worker, LIFO popped from bottom, FIFO stolen from top). Priority-aware
//     stealing is a follow-up.
//   - Job storage: heap-allocated to fit the WSD<T*> requirement (T must be
//     trivially copyable). The pool owns the allocation and frees it after
//     execution.
//   - No global mutex on the hot path; contention is only the WSD's CAS.
//
// Outstanding (Sprint S2.5 follow-ups):
//   - Hazard-pointer based reclamation for retired WSD buffers (currently
//     bounded retention until pool destruction).
//   - Priority-aware steal ordering.
//
// phase1078 (X1-FU-A): the worker WAKE path migrated from
// condition_variable + 2 ms polling wait_for to C++20
// std::atomic::wait/notify_all on a wake epoch counter. Protocol
// (lost-wake-free by construction):
//   worker: epoch = wake_epoch_.load(acquire)
//           re-check shutdown + own inject buffer + peer queues
//           wake_epoch_.wait(epoch)            // returns iff epoch moved
//   waker:  publish work (release)             // enqueue / shutdown
//           wake_epoch_.fetch_add(1, release)
//           wake_epoch_.notify_all()
// A bump between the worker's load and wait() makes wait() return
// immediately; the acquire load orders the re-check AFTER the epoch
// read, so any work published before the bump is visible to it.
// idle_condition_ (wait_all) deliberately stays a cv — cold path,
// and the predicate spans three counters.
// =============================================================================
#pragma once

#include <cd/concurrency/CoroTask.hpp>
#include <cd/concurrency/Task.hpp>
#include <cd/concurrency/TaskPriority.hpp>
#include <cd/concurrency/WorkStealingDeque.hpp>
#include <cd/core/Defines.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cd::concurrency
{

struct WorkStealingPoolStats
{
    std::atomic<std::uint64_t> tasks_submitted { 0 };
    std::atomic<std::uint64_t> tasks_completed { 0 };
    std::atomic<std::uint64_t> steals { 0 };
    std::atomic<std::uint64_t> steal_aborts { 0 };
    /// Detached jobs that exited via an uncaught C++ exception. The pool can't
    /// propagate the exception (no caller waiting), but the count is exposed for
    /// observability so tests / diagnostics can assert it stays zero.
    std::atomic<std::uint64_t> detached_exceptions { 0 };
};

class WorkStealingThreadPool
{
public:
    /// `thread_count == 0` selects hardware_concurrency()-1, min 1.
    explicit WorkStealingThreadPool(std::size_t thread_count = 0)
    {
        if (thread_count == 0)
        {
            auto n = std::thread::hardware_concurrency();
            thread_count = n > 1 ? n - 1 : 1;
        }
        running_.store(true, std::memory_order_release);
        workers_.reserve(thread_count);
        queues_.reserve(thread_count);
        inject_mutexes_.reserve(thread_count);
        inject_buffers_.resize(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i)
        {
            queues_.emplace_back(std::make_unique<WorkStealingDeque<Job*>>(64));
            inject_mutexes_.emplace_back(std::make_unique<std::mutex>());
        }
        for (std::size_t i = 0; i < thread_count; ++i)
        {
            workers_.emplace_back(
                [this, i](std::stop_token st)
                {
                    this->worker_loop(i, st);
                }
            );
        }
    }

    ~WorkStealingThreadPool()
    {
        shutdown();
    }

    WorkStealingThreadPool(const WorkStealingThreadPool&) = delete;
    WorkStealingThreadPool& operator=(const WorkStealingThreadPool&) = delete;
    WorkStealingThreadPool(WorkStealingThreadPool&&) = delete;
    WorkStealingThreadPool& operator=(WorkStealingThreadPool&&) = delete;

    void shutdown() noexcept
    {
        bool was = running_.exchange(false, std::memory_order_acq_rel);
        if (!was)
            return;
        {
            std::scoped_lock guard { idle_mutex_ };
        }
        bump_wake_epoch();
        idle_condition_.notify_all();
        for (auto& w : workers_)
            w.request_stop();
        // Second bump covers a worker that loaded the epoch before
        // request_stop() above (mirrors the old double-notify).
        bump_wake_epoch();
        for (auto& w : workers_)
        {
            if (w.joinable())
                w.join();
        }
        // Drain any orphaned jobs to free memory.
        for (std::size_t i = 0; i < queues_.size(); ++i)
        {
            while (auto v = queues_[i]->pop())
                delete *v;
            std::scoped_lock guard { *inject_mutexes_[i] };
            for (auto* j : inject_buffers_[i])
                delete j;
            inject_buffers_[i].clear();
        }
        workers_.clear();
    }

    template <class F, class... Args>
    auto submit(F&& fn, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        return submit_with_priority(TaskPriority::Normal, std::forward<F>(fn), std::forward<Args>(args)...);
    }

    template <class F, class... Args>
    auto submit_with_priority(TaskPriority priority, F&& fn, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using R = std::invoke_result_t<F, Args...>;
        auto pt = std::make_shared<std::packaged_task<R()>>(
            [f = std::forward<F>(fn), ... a = std::forward<Args>(args)]() mutable -> R
            {
                return std::invoke(std::move(f), std::move(a)...);
            }
        );
        auto fut = pt->get_future();
        enqueue(new Job { [pt]
                          {
                              (*pt)();
                          },
                          priority });
        return fut;
    }

    template <class F, class... Args>
    void submit_detached(F&& fn, Args&&... args)
    {
        submit_detached_with_priority(TaskPriority::Normal, std::forward<F>(fn), std::forward<Args>(args)...);
    }

    template <class F, class... Args>
    void submit_detached_with_priority(TaskPriority priority, F&& fn, Args&&... args)
    {
        enqueue(new Job { [f = std::forward<F>(fn), ... a = std::forward<Args>(args)]() mutable
                          {
                              std::invoke(std::move(f), std::move(a)...);
                          },
                          priority });
    }

    bool spawn_detached(CoroTask task, TaskPriority priority = TaskPriority::Normal) noexcept
    {
        if (!task)
            return false;
        auto handle = task.release();
        active_coroutines_.fetch_add(1, std::memory_order_relaxed);
        handle.promise().on_complete = [this](std::exception_ptr) noexcept
        {
            active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
            idle_condition_.notify_all();
        };
        // bugprone-unhandled-exception-at-new: spawn_detached is noexcept,
        // so a throwing `new Job{}` would call std::terminate. Use
        // nothrow allocation + graceful failure path so the coroutine
        // counter stays balanced if the heap is exhausted.
        auto* job = new (std::nothrow) Job { [handle]
                                             {
                                                 if (!handle.done())
                                                     handle.resume();
                                             },
                                             priority };
        if (job == nullptr)
        {
            active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
            idle_condition_.notify_all();
            return false;
        }
        enqueue(job);
        return true;
    }

    void wait_all()
    {
        std::unique_lock guard { idle_mutex_ };
        idle_condition_.wait(
            guard,
            [this]
            {
                return queued_.load(std::memory_order_acquire) == 0 &&
                       in_flight_.load(std::memory_order_acquire) == 0 &&
                       active_coroutines_.load(std::memory_order_acquire) == 0;
            }
        );
    }

    [[nodiscard]] bool is_running() const noexcept
    {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t thread_count() const noexcept
    {
        return workers_.size();
    }

    [[nodiscard]] std::size_t pending() const noexcept
    {
        return queued_.load(std::memory_order_acquire);
    }

    [[nodiscard]] const WorkStealingPoolStats& stats() const noexcept
    {
        return stats_;
    }

private:
    void bump_wake_epoch() noexcept
    {
        wake_epoch_.fetch_add(1, std::memory_order_release);
        wake_epoch_.notify_all();
    }

    void enqueue(Job* job)
    {
        if (!running_.load(std::memory_order_acquire))
        {
            delete job;
            return;
        }
        // Round-robin the submission across per-worker injection buffers. The
        // chosen worker will drain its own buffer into its WSD; other workers
        // can only access this job through stealing.
        const auto n = queues_.size();
        const auto idx = next_inject_.fetch_add(1, std::memory_order_relaxed) % n;
        {
            std::scoped_lock guard { *inject_mutexes_[idx] };
            inject_buffers_[idx].push_back(job);
        }
        queued_.fetch_add(1, std::memory_order_release);
        stats_.tasks_submitted.fetch_add(1, std::memory_order_relaxed);
        bump_wake_epoch();
    }

    /// Owner-only: drain MY injection buffer into MY WSD. Returns number drained.
    std::size_t drain_my_inject(std::size_t self)
    {
        std::vector<Job*> local;
        {
            std::scoped_lock guard { *inject_mutexes_[self] };
            local.swap(inject_buffers_[self]);
        }
        for (auto* j : local)
        {
            queues_[self]->push(j);
        }
        return local.size();
    }

    std::size_t pick_victim(std::size_t self, std::mt19937& rng)
    {
        const auto n = queues_.size();
        if (n <= 1)
            return self;
        std::uniform_int_distribution<std::size_t> dist { 0, n - 2 };
        auto v = dist(rng);
        if (v >= self)
            ++v;
        return v;
    }

    void run_job(Job* j) noexcept
    {
        in_flight_.fetch_add(1, std::memory_order_acq_rel);
        try
        {
            (*j)();
        }
        catch (...)
        {
            // packaged_task captures the exception_ptr inside the job itself, so
            // futures still surface it via std::future::get(). Detached jobs have
            // no caller to receive the exception; record the event for diagnostics
            // and continue. Letting the exception propagate would invoke
            // std::terminate() (we are inside a noexcept worker loop).
            stats_.detached_exceptions.fetch_add(1, std::memory_order_relaxed);
        }
        delete j;
        stats_.tasks_completed.fetch_add(1, std::memory_order_relaxed);
        in_flight_.fetch_sub(1, std::memory_order_acq_rel);
        queued_.fetch_sub(1, std::memory_order_acq_rel);
        {
            std::scoped_lock guard { idle_mutex_ };
        }
        idle_condition_.notify_all();
    }

    void worker_loop(std::size_t self, std::stop_token st)
    {
        // Knuth multiplicative hash mod 2^32. Compute entirely in uint32_t so the
        // cast to seed is a no-op and no implicit widening of `self` (size_t) to
        // 64-bit math happens — keeps the seed reproducible across 32/64-bit
        // builds and silences -Wimplicit-int-conversion.
        const auto self_u32 = static_cast<std::uint32_t>(self & 0xFFFFFFFFU);
        std::mt19937 rng { self_u32 * 2654435761U + 0x9E3779B9U };
        auto& my_q = *queues_[self];
        constexpr int kStealAttempts = 4;

        while (true)
        {
            // 1) Drain pending external submissions onto MY WSD (owner-only push).
            drain_my_inject(self);

            // 2) Own deque first (LIFO).
            if (auto local = my_q.pop())
            {
                run_job(*local);
                continue;
            }

            // 3) Try stealing.
            bool found = false;
            for (int attempt = 0; attempt < kStealAttempts; ++attempt)
            {
                const auto victim = pick_victim(self, rng);
                Job* stolen {};
                const auto status = queues_[victim]->steal(stolen);
                if (status == StealStatus::Success)
                {
                    stats_.steals.fetch_add(1, std::memory_order_relaxed);
                    run_job(stolen);
                    found = true;
                    break;
                }
                if (status == StealStatus::Abort)
                {
                    stats_.steal_aborts.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                }
            }
            if (found)
                continue;

            // 4) Sleep until new work or shutdown (X1-FU-A: C++20 atomic
            // wait on the wake epoch — event-driven, no 2 ms poll). The
            // epoch is loaded FIRST; the work re-check below is ordered
            // after it by the acquire load, so a producer that published
            // work and bumped the epoch in between either makes the
            // re-check see the work or makes wait() return immediately.
            const auto wake_epoch = wake_epoch_.load(std::memory_order_acquire);
            bool work_visible =
                !running_.load(std::memory_order_acquire) || st.stop_requested();
            if (!work_visible)
            {
                // MY injection buffer has work, or a peer queue is stealable.
                std::scoped_lock sg { *inject_mutexes_[self] };
                work_visible = !inject_buffers_[self].empty();
            }
            if (!work_visible)
            {
                for (std::size_t i = 0; i < queues_.size(); ++i)
                {
                    if (i != self && queues_[i]->approx_size() > 0)
                    {
                        work_visible = true;
                        break;
                    }
                }
            }
            if (!work_visible)
                wake_epoch_.wait(wake_epoch, std::memory_order_acquire);

            if (!running_.load(std::memory_order_acquire) || st.stop_requested())
            {
                // Final drain before exit: only owner-side ops on my_q are safe here.
                while (auto v = my_q.pop())
                {
                    run_job(*v);
                }
                return;
            }
        }
    }

    std::vector<std::jthread> workers_;
    std::vector<std::unique_ptr<WorkStealingDeque<Job*>>> queues_;

    // Per-worker injection lists: external submitters drop new jobs here,
    // round-robin'ed across workers. Each worker pulls from its own list at the
    // top of every loop iteration. This guarantees that submitted work is
    // pre-partitioned, and an under-loaded worker must steal from a busy peer.
    std::vector<std::unique_ptr<std::mutex>> inject_mutexes_;
    std::vector<std::vector<Job*>> inject_buffers_;
    std::atomic<std::size_t> next_inject_ { 0 };

    /// X1-FU-A wake epoch — see the protocol note in the file banner.
    std::atomic<std::uint32_t> wake_epoch_ { 0 };

    std::mutex idle_mutex_;
    std::condition_variable idle_condition_;

    std::atomic<bool> running_ { false };
    std::atomic<std::uint64_t> queued_ { 0 };
    std::atomic<std::uint64_t> in_flight_ { 0 };
    std::atomic<std::uint64_t> active_coroutines_ { 0 };

    WorkStealingPoolStats stats_;
};

}  // namespace cd::concurrency
