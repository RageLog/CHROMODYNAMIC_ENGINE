// =============================================================================
// CHROMODYNAMIC — cd/concurrency/ThreadPool.hpp
// ADR-015 + ADR-017 P1 (DfH common/threading/threadpool.hpp distillation,
//                       singleton-free, mutex-based queue v1).
//
// v1 (Sprint S2.3): priority queue + N worker threads + std::future return.
// v2 (Sprint S2.5): Chase-Lev work-stealing deque per worker + hazard pointers
//                   + coroutine ResumeOnAwaiter / SleepForAwaiter wiring.
// =============================================================================
#pragma once

#include <cd/concurrency/CoroTask.hpp>
#include <cd/concurrency/Task.hpp>
#include <cd/concurrency/TaskPriority.hpp>
#include <cd/core/Defines.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace cd::concurrency
{

struct ThreadPoolStats
{
    std::atomic<std::uint64_t> tasks_submitted { 0 };
    std::atomic<std::uint64_t> tasks_completed { 0 };
    std::atomic<std::uint64_t> failures { 0 };
};

class ThreadPool
{
public:
    /// `thread_count == 0` selects `std::thread::hardware_concurrency() - 1`
    /// (reserve one core for the main thread / OS scheduling).
    explicit ThreadPool(std::size_t thread_count = 0)
    {
        if (thread_count == 0)
        {
            auto n = std::thread::hardware_concurrency();
            thread_count = n > 1 ? n - 1 : 1;
        }
        running_.store(true, std::memory_order_release);
        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i)
        {
            workers_.emplace_back(
                [this](std::stop_token st)
                {
                    this->worker_loop(st);
                }
            );
        }
    }

    ~ThreadPool()
    {
        shutdown();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /// Stop accepting tasks, drain in-flight, join workers.
    void shutdown() noexcept
    {
        bool was_running = running_.exchange(false, std::memory_order_acq_rel);
        if (!was_running)
        {
            return;
        }
        {
            std::lock_guard guard { mutex_ };
            // Workers see running_=false; nothing else to flip.
        }
        condition_.notify_all();
        for (auto& w : workers_)
        {
            w.request_stop();
        }
        condition_.notify_all();
        for (auto& w : workers_)
        {
            if (w.joinable())
            {
                w.join();
            }
        }
        workers_.clear();
    }

    /// Submit a callable; returns std::future<R>.
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
        enqueue(
            Job { [pt]
                  {
                      (*pt)();
                  },
                  priority }
        );
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
        enqueue(
            Job { [f = std::forward<F>(fn), ... a = std::forward<Args>(args)]() mutable
                  {
                      std::invoke(std::move(f), std::move(a)...);
                  },
                  priority }
        );
    }

    /// Spawn a detached CoroTask (void-returning) on the pool. Pool drives the
    /// coroutine by resuming the handle as a regular task.
    bool spawn_detached(CoroTask task, TaskPriority priority = TaskPriority::Normal) noexcept
    {
        if (!task)
        {
            return false;
        }
        auto handle = task.release();
        active_coroutines_.fetch_add(1, std::memory_order_relaxed);
        handle.promise().on_complete = [this](std::exception_ptr) noexcept
        {
            active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
        };
        enqueue(
            Job { [handle]
                  {
                      if (!handle.done())
                          handle.resume();
                  },
                  priority }
        );
        return true;
    }

    /// Block until queue empties and all in-flight tasks / coroutines complete.
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

    [[nodiscard]] const ThreadPoolStats& stats() const noexcept
    {
        return stats_;
    }

private:
    struct Entry
    {
        Job job;
        std::uint64_t sequence { 0 };
    };

    struct Compare
    {
        bool operator()(const Entry& a, const Entry& b) const noexcept
        {
            const auto pa = static_cast<std::uint8_t>(a.job.priority());
            const auto pb = static_cast<std::uint8_t>(b.job.priority());
            if (pa == pb)
                return a.sequence > b.sequence;
            return pa < pb;  // higher priority pops first
        }
    };

    void enqueue(Job task)
    {
        if (!running_.load(std::memory_order_acquire))
        {
            return;
        }
        {
            std::lock_guard guard { mutex_ };
            Entry e;
            e.job = std::move(task);
            e.sequence = ++next_sequence_;
            queue_.push(std::move(e));
            queued_.fetch_add(1, std::memory_order_release);
        }
        stats_.tasks_submitted.fetch_add(1, std::memory_order_relaxed);
        condition_.notify_one();
    }

    void worker_loop(std::stop_token st)
    {
        while (true)
        {
            Job t;
            {
                std::unique_lock guard { mutex_ };
                condition_.wait(
                    guard,
                    [&]
                    {
                        return !running_.load(std::memory_order_acquire) || st.stop_requested() || !queue_.empty();
                    }
                );
                if ((!running_.load(std::memory_order_acquire) || st.stop_requested()) && queue_.empty())
                {
                    return;
                }
                if (queue_.empty())
                    continue;
                t = std::move(const_cast<Entry&>(queue_.top()).job);
                queue_.pop();
                queued_.fetch_sub(1, std::memory_order_acq_rel);
                in_flight_.fetch_add(1, std::memory_order_acq_rel);
            }
            try
            {
                t();
                stats_.tasks_completed.fetch_add(1, std::memory_order_relaxed);
            }
            catch (...)
            {
                stats_.failures.fetch_add(1, std::memory_order_relaxed);
            }
            in_flight_.fetch_sub(1, std::memory_order_acq_rel);
            {
                std::lock_guard guard { idle_mutex_ };
                idle_condition_.notify_all();
            }
        }
    }

    std::vector<std::jthread> workers_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::priority_queue<Entry, std::vector<Entry>, Compare> queue_;

    std::mutex idle_mutex_;
    std::condition_variable idle_condition_;

    std::atomic<bool> running_ { false };
    std::atomic<std::size_t> queued_ { 0 };
    std::atomic<std::size_t> in_flight_ { 0 };
    std::atomic<std::size_t> active_coroutines_ { 0 };
    std::uint64_t next_sequence_ { 0 };

    ThreadPoolStats stats_ {};
};

}  // namespace cd::concurrency
