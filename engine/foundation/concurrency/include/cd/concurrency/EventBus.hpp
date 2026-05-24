// =============================================================================
// CHROMODYNAMIC — cd/concurrency/EventBus.hpp
// Phase 58.B / Wave 226 — typed multicast event bus.
//
// Publish-subscribe over arbitrary event types T:
//   * `subscribe<T>(fn)` — register callback fired by `publish<T>(e)`.
//   * `publish<T>(e)`    — fires every callback registered for T.
//   * `unsubscribe(handle)` — drop a previous subscription.
//
// The bus is **single-threaded** (no internal locking) — caller
// coordinates if publish/subscribe race. For cross-thread messaging
// use `cd::concurrency::Channel` (Phase 57). EventBus is for one-thread
// loose coupling between game systems.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace cd::concurrency
{

class EventBus
{
public:
    using HandleId = std::uint64_t;

    template <class T>
    HandleId subscribe(std::function<void(const T&)> fn)
    {
        const HandleId id = ++next_id_;
        auto& slots = handlers_[std::type_index { typeid(T) }];
        slots.push_back(Slot {
            id,
            [cb = std::move(fn)](const void* p) { cb(*static_cast<const T*>(p)); },
        });
        return id;
    }

    template <class T>
    void publish(const T& event) const
    {
        auto it = handlers_.find(std::type_index { typeid(T) });
        if (it == handlers_.end()) return;
        for (const auto& s : it->second) s.fn(&event);
    }

    bool unsubscribe(HandleId id)
    {
        for (auto& [ti, slots] : handlers_)
        {
            for (auto it = slots.begin(); it != slots.end(); ++it)
            {
                if (it->id == id)
                {
                    slots.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    void clear() noexcept { handlers_.clear(); }

    [[nodiscard]] std::size_t handler_count() const noexcept
    {
        std::size_t total = 0;
        for (const auto& [_, slots] : handlers_) total += slots.size();
        return total;
    }

private:
    struct Slot
    {
        HandleId id;
        std::function<void(const void*)> fn;
    };
    std::unordered_map<std::type_index, std::vector<Slot>> handlers_;
    HandleId next_id_ { 0 };
};

}  // namespace cd::concurrency
