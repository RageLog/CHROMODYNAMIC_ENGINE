// =============================================================================
// CHROMODYNAMIC — cd/concurrency/Task.hpp
// ADR-017 P1 (DfH common/threading/threadpool.hpp::Task salvage, renamed Job
// to avoid clash with the coroutine Task<T> template in CoroTask.hpp)
// =============================================================================
#pragma once

#include <cd/concurrency/TaskPriority.hpp>
#include <cd/core/Defines.hpp>

#include <functional>
#include <type_traits>
#include <utility>

namespace cd::concurrency
{

/// Lightweight type-erased callable + priority tag (formerly "Task" in DfH).
/// Used as the storage unit inside ThreadPool's queue.
class Job
{
public:
    Job() noexcept = default;

    template <class F>
        requires std::is_invocable_r_v<void, std::decay_t<F>>
    explicit Job(F&& fn, TaskPriority priority = TaskPriority::Normal)
        : fn_ { std::forward<F>(fn) }
        , priority_ { priority }
    {
    }

    void operator()()
    {
        if (fn_)
            fn_();
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(fn_);
    }

    [[nodiscard]] TaskPriority priority() const noexcept
    {
        return priority_;
    }

private:
    std::function<void()> fn_ {};
    TaskPriority priority_ { TaskPriority::Normal };
};

}  // namespace cd::concurrency
