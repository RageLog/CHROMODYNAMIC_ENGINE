// =============================================================================
// CHROMODYNAMIC — cd/concurrency/DeterministicExecutor.hpp
// ADR-015 + ADR-017 P3 (replay determinism + test infra)
//
// A single-thread, in-order executor that owns no worker threads. Tasks are
// queued in submission order and only run when the caller invokes step()
// (one task), drain() (all currently queued), or pump_until(predicate).
//
// Use cases:
//   * Deterministic unit tests for higher-level systems that submit jobs.
//   * Replay/golden-trace harnesses where wall-clock thread scheduling would
//     introduce non-determinism.
//   * Single-frame batched compute on the main thread.
// =============================================================================
#pragma once

#include <cd/concurrency/Task.hpp>
#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace cd::concurrency
{

class DeterministicExecutor
{
public:
    DeterministicExecutor() noexcept = default;
    ~DeterministicExecutor() = default;

    DeterministicExecutor(const DeterministicExecutor&) = delete;
    DeterministicExecutor& operator=(const DeterministicExecutor&) = delete;
    DeterministicExecutor(DeterministicExecutor&&) = delete;
    DeterministicExecutor& operator=(DeterministicExecutor&&) = delete;

    /// Submit a void callable. Returns the sequence id assigned to the task.
    /// Tasks run in increasing sequence-id order — FIFO by submission.
    template <class F>
        requires std::is_invocable_r_v<void, std::decay_t<F>>
    std::uint64_t submit(F&& fn)
    {
        std::lock_guard guard { mutex_ };
        const auto id = ++next_id_;
        queue_.emplace_back(Entry { id, Job { std::forward<F>(fn) } });
        return id;
    }

    /// Submit a callable returning T, return a future to its result.
    /// Exceptions thrown by `fn` propagate through `future.get()`.
    template <class F>
    auto submit_future(F&& fn) -> std::future<std::invoke_result_t<std::decay_t<F>>>
    {
        using R = std::invoke_result_t<std::decay_t<F>>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        auto fut = task->get_future();
        submit(
            [task]()
            {
                (*task)();
            }
        );
        return fut;
    }

    /// Run exactly one queued task if any. Returns true if a task ran.
    bool step()
    {
        Entry e;
        {
            std::lock_guard guard { mutex_ };
            if (queue_.empty())
                return false;
            e = std::move(queue_.front());
            queue_.pop_front();
        }
        ++ran_;
        e.job();
        return true;
    }

    /// Run every task currently queued. Tasks added by those tasks are NOT run
    /// — call drain() again or use pump_until() if you need transitive draining.
    /// Returns the number of tasks executed.
    std::size_t drain()
    {
        std::deque<Entry> work;
        {
            std::lock_guard guard { mutex_ };
            work.swap(queue_);
        }
        for (auto& e : work)
        {
            ++ran_;
            e.job();
        }
        return work.size();
    }

    /// Repeatedly drain() until queue is empty or `pred()` returns false.
    /// Guards against infinite loops with `max_iterations` (default 1024).
    template <class Pred>
    std::size_t pump_until(Pred pred, std::size_t max_iterations = 1024)
    {
        std::size_t total = 0;
        for (std::size_t i = 0; i < max_iterations; ++i)
        {
            const auto n = drain();
            total += n;
            if (n == 0)
                break;
            if (!pred())
                break;
        }
        return total;
    }

    [[nodiscard]] std::size_t pending() const
    {
        std::lock_guard guard { mutex_ };
        return queue_.size();
    }

    [[nodiscard]] std::uint64_t executed_count() const noexcept
    {
        return ran_;
    }

    [[nodiscard]] std::uint64_t last_assigned_id() const noexcept
    {
        return next_id_;
    }

    void clear()
    {
        std::lock_guard guard { mutex_ };
        queue_.clear();
    }

private:
    struct Entry
    {
        std::uint64_t id { 0 };
        Job job;
    };

    mutable std::mutex mutex_;
    std::deque<Entry> queue_;
    std::uint64_t next_id_ { 0 };
    std::uint64_t ran_ { 0 };
};

}  // namespace cd::concurrency
