// =============================================================================
// CHROMODYNAMIC — cd/diag/DeadlineMonitor.hpp
// ADR-013 + ADR-017 P2 (DfH safety_monitor/watchdog.hpp salvage)
//
// Subsystem heartbeat watchdog. Each subsystem (render thread, audio actor,
// asset loader, etc.) registers a name + max tick interval; if no `heartbeat()`
// arrives within the deadline, the monitor invokes `on_stall(name, last_tick)`.
//
// Injected-clock constructor:
//   For unit tests pass a `NowFn` (any callable returning TimePoint) so the
//   test controls the clock without sleep_for. Production uses the default
//   zero-arg ctor which calls Clock::now().
//
// arm / disarm:
//   Pause or resume deadline checking for an individual subsystem without
//   unregistering it. A disarmed subsystem is skipped by evaluate().
//
// extend_deadline:
//   Lengthen (or shorten) a subsystem's deadline window after registration.
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

    /// Production constructor: uses Clock::now() internally.
    DeadlineMonitor() noexcept = default;

    /// Test constructor: supply an injected clock function so tests can
    /// advance time without sleep_for. The function must be thread-safe.
    explicit DeadlineMonitor(std::function<TimePoint()> now_fn)
        : now_fn_ { std::move(now_fn) }
    {
    }

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
        s.armed = true;
        s.last_heartbeat.store(now(), std::memory_order_release);
    }

    void unregister(std::string_view name)
    {
        std::unique_lock guard { mutex_ };
        subs_.erase(std::string { name });
    }

    /// Suspend deadline checking for the named subsystem without removing it.
    /// A disarmed subsystem is skipped by evaluate() until arm() is called.
    void disarm(std::string_view name)
    {
        std::unique_lock guard { mutex_ };
        auto it = subs_.find(std::string { name });
        if (it != subs_.end())
        {
            it->second.armed = false;
        }
    }

    /// Re-enable deadline checking for a previously disarmed subsystem.
    /// Also refreshes its last_heartbeat timestamp to avoid an immediate stall.
    void arm(std::string_view name)
    {
        std::unique_lock guard { mutex_ };
        auto it = subs_.find(std::string { name });
        if (it != subs_.end())
        {
            it->second.armed = true;
            it->second.last_heartbeat.store(now(), std::memory_order_release);
        }
    }

    /// Replace the deadline window for a registered subsystem.
    /// No-op if the subsystem is not registered.
    void extend_deadline(std::string_view name, std::chrono::milliseconds new_deadline)
    {
        std::unique_lock guard { mutex_ };
        auto it = subs_.find(std::string { name });
        if (it != subs_.end())
        {
            it->second.deadline = new_deadline;
        }
    }

    /// Call from the subsystem's tick loop to refresh its deadline window.
    void heartbeat(std::string_view name)
    {
        std::shared_lock guard { mutex_ };
        auto it = subs_.find(std::string { name });
        if (it == subs_.end())
            return;
        it->second.last_heartbeat.store(now(), std::memory_order_release);
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
    /// granularity (e.g., every 1s). Disarmed subsystems are skipped.
    [[nodiscard]] std::size_t evaluate()
    {
        const auto now_tp = now();
        StallCallback cb;
        std::vector<std::tuple<std::string, std::chrono::milliseconds>> stalled;
        {
            std::shared_lock guard { mutex_ };
            cb = on_stall_cb_;
            for (auto& [name, s] : subs_)
            {
                if (!s.armed)
                    continue;
                const auto last = s.last_heartbeat.load(std::memory_order_acquire);
                const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - last);
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

    /// Return whether the named subsystem is currently armed.
    /// Returns false if the subsystem is not registered.
    [[nodiscard]] bool is_armed(std::string_view name) const
    {
        std::shared_lock guard { mutex_ };
        const auto it = subs_.find(std::string { name });
        return it != subs_.end() && it->second.armed;
    }

private:
    /// Return the current time via the injected clock or Clock::now().
    [[nodiscard]] TimePoint now() const
    {
        if (now_fn_)
            return now_fn_();
        return Clock::now();
    }

    struct Subsystem
    {
        std::chrono::milliseconds deadline { 1000 };
        std::atomic<TimePoint> last_heartbeat { TimePoint::min() };
        bool armed { true };

        Subsystem() noexcept = default;

        Subsystem(const Subsystem& other) noexcept
            : deadline { other.deadline }
            , last_heartbeat { other.last_heartbeat.load() }
            , armed { other.armed }
        {
        }

        Subsystem& operator=(const Subsystem& other) noexcept
        {
            if (this != &other)
            {
                deadline = other.deadline;
                last_heartbeat.store(other.last_heartbeat.load());
                armed = other.armed;
            }
            return *this;
        }
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Subsystem> subs_;
    StallCallback on_stall_cb_;
    std::function<TimePoint()> now_fn_ {};
};

}  // namespace cd::diag
