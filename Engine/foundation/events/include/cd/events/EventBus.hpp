// =============================================================================
// CHROMODYNAMIC — cd/events/EventBus.hpp
// ADR-017 P1 (DfH asynceventbus.hpp 651-line salvage, distilled & singleton-free)
//
// v1 surface (Sprint S2.2):
//   - Typed pub/sub via subscribe<EventT>(handler) → ScopedConnection
//   - Synchronous publish<EventT>(event) — handler runs on caller thread
//   - Thread-safe (shared_mutex for subscriber registry)
//   - No std::any, no ConfigValue payload, no IPlugin coupling
//
// v2 surface (Wave 56 — Phase 6 Sprint 3):
//   - Deferred publish: queue_publish<EventT>(event) is producer-thread
//     safe; drain() runs every queued handler on the calling thread.
//   - Drain integrates with the engine's main loop: worker threads
//     enqueue, the main tick drains, no shared-state races inside the
//     handler bodies.
//
// Async delivery / priority / oneShot / maxConcurrency / weak-token aliveness
// from the DfH original return in Sprint S2.3+ once the ThreadPool / coroutine
// scheduler land.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/events/ScopedConnection.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace cd::events
{

class EventBus
{
public:
    EventBus() noexcept = default;
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    /// Subscribe to events of type `EventT`. Handler signature: `void(const EventT&)`.
    /// Returns a movable RAII handle; destruction auto-unsubscribes.
    template <class EventT, class Handler>
    [[nodiscard]] ScopedConnection subscribe(Handler&& handler)
    {
        static_assert(std::is_invocable_v<Handler, const EventT&>, "Handler must be callable as void(const EventT&)");
        auto id = next_id_.fetch_add(1, std::memory_order_relaxed) + 1;
        auto entry = std::make_shared<TypedEntry<EventT>>();
        entry->id = id;
        entry->fn = std::forward<Handler>(handler);

        std::unique_lock guard { mutex_ };
        bucket_for<EventT>().push_back(std::move(entry));
        return ScopedConnection { this, id };
    }

    /// Publish synchronously to all live subscribers of `EventT`. Returns the
    /// number of handlers invoked.
    template <class EventT>
    std::size_t publish(const EventT& event)
    {
        // Copy out a snapshot of subscribers under shared lock so that handlers
        // may freely subscribe/unsubscribe without deadlocking.
        std::vector<std::shared_ptr<TypedEntry<EventT>>> snapshot;
        {
            std::shared_lock guard { mutex_ };
            auto it = subscribers_.find(std::type_index { typeid(EventT) });
            if (it == subscribers_.end())
            {
                return 0;
            }
            auto& base_vec = it->second;
            snapshot.reserve(base_vec.size());
            for (auto& base : base_vec)
            {
                snapshot.push_back(std::static_pointer_cast<TypedEntry<EventT>>(base));
            }
        }
        std::size_t invoked = 0;
        for (auto& entry : snapshot)
        {
            if (entry->fn)
            {
                entry->fn(event);
                ++invoked;
            }
        }
        return invoked;
    }

    /// Queue an event for deferred delivery. Thread-safe — safe to
    /// call from worker threads. The event payload is COPIED into a
    /// type-erased deferred slot; the originals can go out of scope
    /// before `drain()` runs.
    template <class EventT>
    void queue_publish(EventT event)
    {
        // We capture by value into a callable; drain() invokes them.
        // Wrapping in std::function lets us erase the EventT type at
        // the queue level while still routing through publish<EventT>
        // for the actual subscriber dispatch.
        auto fn = [this, ev = std::move(event)]() mutable {
            (void)publish<EventT>(ev);
        };
        std::lock_guard guard { queue_mutex_ };
        queue_.push_back(std::move(fn));
    }

    /// Deliver every queued event on the calling thread. Returns the
    /// number of QUEUE ENTRIES processed (NOT the number of handlers
    /// invoked — for that, sum the per-call publish() return values
    /// via a custom handler if you need it). Safe to call concurrently
    /// with queue_publish — drained entries are removed atomically.
    std::size_t drain()
    {
        std::vector<std::function<void()>> local;
        {
            std::lock_guard guard { queue_mutex_ };
            std::swap(local, queue_);
        }
        for (auto& f : local)
            f();
        return local.size();
    }

    /// Snapshot of the queue depth. Diagnostic only — concurrent
    /// queue_publish calls can change this between read and use.
    [[nodiscard]] std::size_t queued_count() const noexcept
    {
        std::lock_guard guard { queue_mutex_ };
        return queue_.size();
    }

    /// Number of active subscribers across every type. Diagnostic only.
    [[nodiscard]] std::size_t subscriber_count() const noexcept
    {
        std::shared_lock guard { mutex_ };
        std::size_t n = 0;
        for (const auto& [_, vec] : subscribers_)
            n += vec.size();
        return n;
    }

    /// Number of active subscribers for `EventT`.
    template <class EventT>
    [[nodiscard]] std::size_t subscriber_count_for() const noexcept
    {
        std::shared_lock guard { mutex_ };
        auto it = subscribers_.find(std::type_index { typeid(EventT) });
        return it == subscribers_.end() ? 0 : it->second.size();
    }

    /// Called by ScopedConnection destructor. Visible publicly so plug-in code
    /// can also detach explicitly without owning the connection.
    void unsubscribe(ScopedConnection::IdType id) noexcept
    {
        if (id == 0)
            return;
        std::unique_lock guard { mutex_ };
        for (auto& [_, vec] : subscribers_)
        {
            auto before = vec.size();
            std::erase_if(
                vec,
                [id](const std::shared_ptr<EntryBase>& e)
                {
                    return e->id == id;
                }
            );
            if (vec.size() != before)
                return;
        }
    }

private:
    struct EntryBase
    {
        ScopedConnection::IdType id { 0 };
        virtual ~EntryBase() = default;
    };

    template <class EventT>
    struct TypedEntry : EntryBase
    {
        std::function<void(const EventT&)> fn;
    };

    template <class EventT>
    std::vector<std::shared_ptr<EntryBase>>& bucket_for()
    {
        return subscribers_[std::type_index { typeid(EventT) }];
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<EntryBase>>> subscribers_;
    std::atomic<ScopedConnection::IdType> next_id_ { 0 };
    mutable std::mutex queue_mutex_;
    std::vector<std::function<void()>> queue_;
};

}  // namespace cd::events

// Definition of ScopedConnection::release() — placed after EventBus so the
// inline body can invoke its members. Keeps the cyclic dep header-only.
namespace cd::events
{

inline void ScopedConnection::release() noexcept
{
    if (bus_ != nullptr && id_ != 0)
    {
        bus_->unsubscribe(id_);
    }
    bus_ = nullptr;
    id_ = 0;
}

}  // namespace cd::events
