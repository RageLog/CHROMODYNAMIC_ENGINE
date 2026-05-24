// =============================================================================
// CHROMODYNAMIC — cd/concurrency/TaskGroup.hpp
// Phase 78.B / Wave 246 — spawn-and-join helper over std::thread.
//
// `TaskGroup` collects a set of `std::thread`s and joins them all in
// the destructor (RAII fork-join). API:
//
//   {
//       TaskGroup g;
//       g.run([] { work_a(); });
//       g.run([] { work_b(); });
//   }   // both joined here
//
// Compared to ThreadPool: TaskGroup is the lightweight ad-hoc
// "spawn N threads, wait for all" pattern when you don't justify a
// long-lived pool. Pairs well with `cd::concurrency::Latch` (Phase 33)
// when you need a barrier between work chunks.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

namespace cd::concurrency
{

class TaskGroup
{
public:
    TaskGroup() = default;

    TaskGroup(const TaskGroup&) = delete;
    TaskGroup& operator=(const TaskGroup&) = delete;

    ~TaskGroup() noexcept
    {
        wait();
    }

    template <class F>
    void run(F&& fn)
    {
        threads_.emplace_back(std::forward<F>(fn));
    }

    /// Block until every running task has finished. Idempotent;
    /// calling twice is safe but the second call is a no-op.
    void wait() noexcept
    {
        for (auto& t : threads_)
        {
            if (t.joinable()) t.join();
        }
        threads_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept { return threads_.size(); }

private:
    std::vector<std::thread> threads_;
};

}  // namespace cd::concurrency
