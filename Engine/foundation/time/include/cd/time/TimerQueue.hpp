// =============================================================================
// CHROMODYNAMIC — cd/time/TimerQueue.hpp
// ADR-017 P1 (DfH common/timing/timerqueue.hpp salvage)
//
// Single-thread priority-queue timer service. start()/stop()/scheduleAfter(...)
// + per-timer std::stop_token for cancellation. The callback is invoked with
// `bool cancelled` so users can distinguish ordinary timeouts from cancels.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/time/Types.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <vector>

namespace cd::time
{

class TimerQueue
{
public:
    using Callback = std::function<void(bool cancelled)>;

    TimerQueue() noexcept = default;

    ~TimerQueue()
    {
        stop();
    }

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;
    TimerQueue(TimerQueue&&) = delete;
    TimerQueue& operator=(TimerQueue&&) = delete;

    /// Start the timer thread. No-op if already running.
    void start()
    {
        std::unique_lock guard { mutex_ };
        if (running_)
            return;
        running_ = true;
        thread_ = std::jthread { [this](std::stop_token st)
                                 {
                                     this->loop(st);
                                 } };
    }

    /// Stop the thread, cancelling every queued timer (callbacks invoked with
    /// cancelled=true).
    void stop()
    {
        {
            std::unique_lock guard { mutex_ };
            if (!running_)
                return;
            running_ = false;
        }
        condition_.notify_all();
        if (thread_.joinable())
        {
            thread_.request_stop();
            thread_.join();
        }
        drain_cancelled();
    }

    [[nodiscard]] bool is_running() const noexcept
    {
        std::lock_guard guard { mutex_ };
        return running_;
    }

    /// Schedule `cb` to fire after `delay`. Returns a monotonically-increasing
    /// sequence id; pass an external stop_token to allow caller-side cancel.
    std::uint64_t schedule_after(std::chrono::milliseconds delay, Callback cb, std::stop_token token = {})
    {
        std::unique_lock guard { mutex_ };
        Entry e;
        e.deadline = Clock::now() + delay;
        e.sequence = next_sequence_++;
        e.callback = std::move(cb);
        e.token = std::move(token);
        entries_.push(std::move(e));
        condition_.notify_one();
        return entries_.top().sequence;  // not necessarily what we just pushed
    }

private:
    struct Entry
    {
        TimePoint deadline {};
        std::uint64_t sequence { 0 };
        Callback callback {};
        std::stop_token token {};
    };

    struct Compare
    {
        bool operator()(const Entry& a, const Entry& b) const noexcept
        {
            if (a.deadline == b.deadline)
                return a.sequence > b.sequence;
            return a.deadline > b.deadline;
        }
    };

    void loop(std::stop_token st)
    {
        while (!st.stop_requested())
        {
            std::unique_lock guard { mutex_ };
            if (entries_.empty())
            {
                condition_.wait(
                    guard,
                    [&]
                    {
                        return !running_ || !entries_.empty() || st.stop_requested();
                    }
                );
                if (st.stop_requested() || !running_)
                    return;
                continue;
            }
            const auto next = entries_.top().deadline;
            if (condition_.wait_until(
                    guard,
                    next,
                    [&]
                    {
                        return !running_ || st.stop_requested() || entries_.empty() || entries_.top().deadline < next;
                    }
                ))
            {
                continue;  // condition changed, re-evaluate
            }
            if (st.stop_requested() || !running_)
                return;
            Entry e = std::move(const_cast<Entry&>(entries_.top()));
            entries_.pop();
            guard.unlock();
            const bool cancelled = e.token.stop_possible() && e.token.stop_requested();
            if (e.callback)
            {
                e.callback(cancelled);
            }
        }
    }

    void drain_cancelled()
    {
        std::priority_queue<Entry, std::vector<Entry>, Compare> drained;
        {
            std::unique_lock guard { mutex_ };
            drained.swap(entries_);
        }
        while (!drained.empty())
        {
            Entry e = std::move(const_cast<Entry&>(drained.top()));
            drained.pop();
            if (e.callback)
                e.callback(true);  // cancelled
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::priority_queue<Entry, std::vector<Entry>, Compare> entries_;
    std::jthread thread_;
    bool running_ { false };
    std::uint64_t next_sequence_ { 1 };
};

}  // namespace cd::time
