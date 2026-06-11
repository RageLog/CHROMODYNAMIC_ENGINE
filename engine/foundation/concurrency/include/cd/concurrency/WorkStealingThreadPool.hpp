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
//   - Priority routes each job to its level's per-worker deque; pops and
//     steals scan Critical -> Low (phase1079, X1-FU-D). Within one level:
//     owner LIFO from bottom, thieves FIFO from top.
//   - Job storage: heap-allocated to fit the WSD<T*> requirement (T must be
//     trivially copyable). The pool owns the allocation and frees it after
//     execution.
//   - No global mutex on the hot path; contention is only the WSD's CAS.
//
// phase1080 (X1-FU-C): outgrown WSD buffers are reclaimed through a
// pool-owned HazardDomain<1> instead of being retained until pool
// destruction. Each worker stack-owns a ThreadCache; it doubles as the
// steal-side guard (protect the victim's array pointer for the
// load+CAS window) and the owner-side retire channel (grow()).
// Destruction order is load-bearing: hazard_domain_ is declared FIRST
// so it outlives the deques; worker caches die at thread exit (join
// happens in shutdown(), before any member is destroyed).
//
// phase1079 (X1-FU-D): priority-aware pop AND steal ordering. Each
// worker owns kPriorityLevels Chase-Lev deques (one per TaskPriority);
// the inject drain routes a job to its priority's deque, own pops and
// victim steals both scan Critical -> Low. A Chase-Lev deque cannot
// reorder in place, so per-level deques are the standard way to get
// strict cross-priority ordering while keeping the lock-free
// owner/thief protocol untouched. Ordering INSIDE one level is
// unchanged (owner LIFO / thief FIFO).
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
#include <cd/concurrency/HazardPtr.hpp>
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
        queues_.reserve(thread_count * kPriorityLevels);
        inject_mutexes_.reserve(thread_count);
        inject_buffers_.resize(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i)
        {
            for (std::size_t lvl = 0; lvl < kPriorityLevels; ++lvl)
            {
                auto q = std::make_unique<WorkStealingDeque<Job*>>(64);
                q->set_hazard_domain(&hazard_domain_);
                queues_.emplace_back(std::move(q));
            }
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
        // Drain any orphaned jobs to free memory (all priority levels).
        // phase1085: balance queued_ for every job deleted unrun and
        // notify idle waiters afterwards, so a wait_all() that raced
        // into shutdown() observes consistent counters instead of
        // hanging on jobs that will never run. CONTRACT: detached
        // COROUTINES pending here still leak their frame and leave
        // active_coroutines_ high (the type-erased Job cannot reach the
        // handle) — call wait_all() before shutdown() when
        // spawn_detached was used; see the v1 ThreadPool note.
        std::uint64_t drained = 0;
        for (auto& q : queues_)
        {
            while (auto v = q->pop())
            {
                delete *v;
                ++drained;
            }
        }
        for (std::size_t i = 0; i < inject_buffers_.size(); ++i)
        {
            std::scoped_lock guard { *inject_mutexes_[i] };
            for (auto* j : inject_buffers_[i])
                delete j;
            drained += inject_buffers_[i].size();
            inject_buffers_[i].clear();
        }
        if (drained != 0)
        {
            queued_.fetch_sub(drained, std::memory_order_acq_rel);
            {
                std::scoped_lock guard { idle_mutex_ };
            }
            idle_condition_.notify_all();
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
            // phase1084 (safety F5): same empty-lock pattern as
            // run_job() — without it the decrement+notify can fire
            // between a wait_all() waiter's predicate evaluation and
            // its sleep, losing the wakeup.
            {
                std::scoped_lock guard { idle_mutex_ };
            }
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
                                                 // Frame is suspended at FinalAwaiter once done —
                                                 // safe (and required) to destroy from outside.
                                                 // phase1085: v1 ThreadPool got this in phase1052;
                                                 // the work-stealing pool had been leaking every
                                                 // completed detached frame.
                                                 if (handle.done())
                                                     handle.destroy();
                                             },
                                             priority };
        if (job == nullptr)
        {
            active_coroutines_.fetch_sub(1, std::memory_order_relaxed);
            handle.destroy();  // phase1085: released frame must not leak
            {
                std::scoped_lock guard { idle_mutex_ };  // F5, see above
            }
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
    static constexpr std::size_t kPriorityLevels = 4;  // TaskPriority::Low..Critical

    /// Deque for (worker, priority level). Levels are contiguous per worker.
    [[nodiscard]] WorkStealingDeque<Job*>& wsd(std::size_t worker, std::size_t lvl) noexcept
    {
        return *queues_[worker * kPriorityLevels + lvl];
    }

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
        const auto n = inject_buffers_.size();
        const auto idx = next_inject_.fetch_add(1, std::memory_order_relaxed) % n;
        {
            std::scoped_lock guard { *inject_mutexes_[idx] };
            // phase1084 (safety F4): re-check running_ UNDER the inject
            // mutex. shutdown() drains these buffers under the same
            // mutex after flipping running_, so a submitter that lost
            // the race here would otherwise park a job that nobody
            // ever runs or deletes (leak + a permanently non-zero
            // queued_ hanging the next wait_all()).
            if (!running_.load(std::memory_order_acquire))
            {
                delete job;
                return;
            }
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
            wsd(self, static_cast<std::size_t>(j->priority())).push(j);
        }
        // phase1084 (safety F2): the pushes above just made work
        // STEALABLE — that is a publish, and every publish must bump
        // the wake epoch or a peer that found nothing to steal can
        // sleep until the next external enqueue (the retired 2 ms poll
        // used to paper over exactly this). The bump also provides the
        // release edge that makes these pushes visible to a sleeping
        // peer's relaxed approx_size() re-check.
        if (!local.empty())
            bump_wake_epoch();
        return local.size();
    }

    std::size_t pick_victim(std::size_t self, std::mt19937& rng)
    {
        const auto n = inject_buffers_.size();
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
        constexpr int kStealAttempts = 4;

        // phase1084 (safety F3): make EVERY request_stop() self-waking —
        // a worker blocked in wake_epoch_.wait() is not observable to
        // jthread's dtor otherwise (the ctor-throw unwind path never
        // runs shutdown(), so its request_stop+join would hang forever
        // on a sleeping worker). Also turns shutdown()'s second bump
        // from belt-and-braces into a structural guarantee.
        std::stop_callback wake_on_stop { st, [this] { bump_wake_epoch(); } };

        // X1-FU-C: this worker's hazard cache — steal-side guard AND the
        // owner-side retire channel for MY deques' grow(). Stack RAII:
        // flushes its retire list back to the domain at thread exit.
        HazardDomain<1>::ThreadCache hp_cache { hazard_domain_ };
        for (std::size_t lvl = 0; lvl < kPriorityLevels; ++lvl)
            wsd(self, lvl).set_owner_cache(&hp_cache);

        // Critical -> Low scan over MY levels; returns nullptr when all empty.
        const auto pop_mine = [&]() -> Job*
        {
            for (std::size_t lvl = kPriorityLevels; lvl-- > 0;)
            {
                if (auto v = wsd(self, lvl).pop())
                    return *v;
            }
            return nullptr;
        };

        while (true)
        {
            // 1) Drain pending external submissions onto MY WSD (owner-only push).
            drain_my_inject(self);

            // 2) Own deques first — highest priority level wins (X1-FU-D);
            //    LIFO inside one level.
            if (auto* local = pop_mine())
            {
                run_job(local);
                continue;
            }

            // 3) Try stealing.
            bool found = false;
            for (int attempt = 0; attempt < kStealAttempts; ++attempt)
            {
                const auto victim = pick_victim(self, rng);
                // X1-FU-D: scan the victim's levels Critical -> Low so a
                // thief always relieves the highest-priority backlog first.
                for (std::size_t lvl = kPriorityLevels; lvl-- > 0;)
                {
                    Job* stolen {};
                    const auto status = wsd(victim, lvl).steal(stolen, &hp_cache);
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
                    break;
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
                const auto workers = inject_buffers_.size();
                for (std::size_t i = 0; i < workers && !work_visible; ++i)
                {
                    if (i == self)
                        continue;
                    for (std::size_t lvl = 0; lvl < kPriorityLevels; ++lvl)
                    {
                        if (wsd(i, lvl).approx_size() > 0)
                        {
                            work_visible = true;
                            break;
                        }
                    }
                }
            }
            if (!work_visible)
                wake_epoch_.wait(wake_epoch, std::memory_order_acquire);

            if (!running_.load(std::memory_order_acquire) || st.stop_requested())
            {
                // Final drain before exit: only owner-side ops on my own
                // deques are safe here. Highest level first.
                while (auto* local = pop_mine())
                {
                    run_job(local);
                }
                return;
            }
        }
    }

    /// X1-FU-C reclamation domain. Declared FIRST: members are destroyed
    /// in reverse order, so the domain outlives the deques that retire
    /// buffers into it (its dtor frees anything still pending).
    HazardDomain<1> hazard_domain_;

    /// kPriorityLevels deques PER WORKER, contiguous per worker — see wsd().
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

    /// phase1084 (safety F3): LAST member on purpose. Members are
    /// destroyed in reverse declaration order, so on ANY unwind —
    /// including a ctor throw mid-spawn, where shutdown() never runs —
    /// the jthread dtors (request_stop + join, self-waking via the
    /// stop_callback above) finish BEFORE queues_ / hazard_domain_ /
    /// the mutexes those workers still touch are destroyed.
    std::vector<std::jthread> workers_;
};

}  // namespace cd::concurrency
