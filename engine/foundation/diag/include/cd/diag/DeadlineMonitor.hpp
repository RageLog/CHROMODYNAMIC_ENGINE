// =============================================================================
// CHROMODYNAMIC — cd/diag/DeadlineMonitor.hpp
// ADR-013 + ADR-017 P2 (DfH safety_monitor/watchdog.hpp salvage)
//
// Subsystem heartbeat watchdog. Each subsystem (render thread, audio actor,
// asset loader, etc.) registers a name + max tick interval; if no `heartbeat()`
// arrives within the deadline, the monitor invokes `on_stall(name, last_tick)`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cd::diag
{

class DeadlineMonitor
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using StallCallback = std::function<void(std::string_view name, std::chrono::milliseconds since_last)>;

    DeadlineMonitor() noexcept = default;
    ~DeadlineMonitor() = default;
    DeadlineMonitor(const DeadlineMonitor&) = delete;
    DeadlineMonitor& operator=(const DeadlineMonitor&) = delete;
    DeadlineMonitor(DeadlineMonitor&&) = delete;
    DeadlineMonitor& operator=(DeadlineMonitor&&) = delete;

    void register_subsystem(std::string_view name, std::chrono::milliseconds deadline)
    {
        std::unique_lock guard { mutex_ };
        auto& s = subs_[std::string { name }];
        s.deadline = deadline;
        s.last_heartbeat.store(Clock::now(), std::memory_order_release);
    }

    void unregister(std::string_view name)
    {
        std::unique_lock guard { mutex_ };
        subs_.erase(std::string { name });
    }

    /// Call from the subsystem's tick loop to refresh its deadline window.
    void heartbeat(std::string_view name)
    {
        std::shared_lock guard { mutex_ };
        auto it = subs_.find(std::string { name });
        if (it == subs_.end())
            return;
        it->second.last_heartbeat.store(Clock::now(), std::memory_order_release);
    }

    /// Set the global on-stall callback. Called when a subsystem misses its
    /// deadline. Invocation happens on the thread that calls `evaluate()`.
    void on_stall(StallCallback cb)
    {
        std::unique_lock guard { mutex_ };
        on_stall_cb_ = std::move(cb);
    }

    /// Evaluate all subsystems and report stalls. Returns the count of stalled
    /// subsystems. Typical use: from a periodic timer (TimerQueue) at coarse
    /// granularity (e.g., every 1s).
    std::size_t evaluate()
    {
        const auto now = Clock::now();
        StallCallback cb;
        std::vector<std::tuple<std::string, std::chrono::milliseconds>> stalled;
        {
            std::shared_lock guard { mutex_ };
            cb = on_stall_cb_;
            for (auto& [name, s] : subs_)
            {
                const auto last = s.last_heartbeat.load(std::memory_order_acquire);
                const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
                if (since > s.deadline)
                {
                    stalled.emplace_back(name, since);
                }
            }
        }
        if (cb)
        {
            for (auto& [name, since] : stalled)
            {
                cb(name, since);
            }
        }
        return stalled.size();
    }

    [[nodiscard]] std::size_t subsystem_count() const
    {
        std::shared_lock guard { mutex_ };
        return subs_.size();
    }

private:
    struct Subsystem
    {
        std::chrono::milliseconds deadline { 1000 };
        std::atomic<TimePoint> last_heartbeat { TimePoint::min() };

        Subsystem() noexcept = default;

        Subsystem(const Subsystem& other) noexcept
            : deadline { other.deadline }
            , last_heartbeat { other.last_heartbeat.load() }
        {
        }

        Subsystem& operator=(const Subsystem& other) noexcept
        {
            deadline = other.deadline;
            last_heartbeat.store(other.last_heartbeat.load());
            return *this;
        }
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Subsystem> subs_;
    StallCallback on_stall_cb_;
};

}  // namespace cd::diag
