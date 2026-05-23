// =============================================================================
// CHROMODYNAMIC — cd/events/ScopedConnection.hpp
// ADR-017 P1 (DfH common/event/asynceventbus.hpp::Subscription salvage)
//
// Move-only RAII handle returned by EventBus::subscribe(). On destruction the
// subscription is automatically detached from the bus. Idiomatic usage:
//
//   cd::events::EventBus bus;
//   cd::events::ScopedConnection conn = bus.subscribe<MyEvent>(
//       [](const MyEvent& e) { ... });
//   // conn goes out of scope -> handler removed.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstdint>
#include <utility>

namespace cd::events
{

class EventBus;  // fwd

class ScopedConnection
{
public:
    using IdType = std::uint64_t;

    ScopedConnection() noexcept = default;

    ScopedConnection(EventBus* bus, IdType id) noexcept
        : bus_ { bus }
        , id_ { id }
    {
    }

    ~ScopedConnection()
    {
        release();
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ScopedConnection(ScopedConnection&& other) noexcept
        : bus_ { other.bus_ }
        , id_ { other.id_ }
    {
        other.bus_ = nullptr;
        other.id_ = 0;
    }

    ScopedConnection& operator=(ScopedConnection&& other) noexcept
    {
        if (this != &other)
        {
            release();
            bus_ = other.bus_;
            id_ = other.id_;
            other.bus_ = nullptr;
            other.id_ = 0;
        }
        return *this;
    }

    /// Detach early; subsequent destruction is a no-op.
    void release() noexcept;

    [[nodiscard]] bool active() const noexcept
    {
        return bus_ != nullptr && id_ != 0;
    }

    [[nodiscard]] IdType id() const noexcept
    {
        return id_;
    }

private:
    EventBus* bus_ { nullptr };
    IdType id_ { 0 };
};

}  // namespace cd::events
