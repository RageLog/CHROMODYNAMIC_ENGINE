// =============================================================================
// CHROMODYNAMIC — cd/ecs/Lifecycle.hpp
// Phase 50.B / Wave 218 — entity create/destroy callback registry.
//
// Editor + gameplay systems often want a hook when an entity is created
// or destroyed (highlight new entities, free GPU resources tied to an
// entity, audit logs). LifecycleRegistry is a multicast registry:
//   * `on_created(fn)` — registers a callback fired by `notify_created(e)`.
//   * `on_destroyed(fn)` — same for destruction.
//   * Registration returns a `HandleId` for later removal.
//
// The registry does NOT integrate with World — caller manually invokes
// `notify_*` at create / destroy points. This keeps World lean and
// makes the registry usable beyond the ECS proper (scene-level
// "created node" notifications, etc.).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace cd::ecs
{

class LifecycleRegistry
{
public:
    using HandleId = std::uint32_t;
    using Callback = std::function<void(Entity)>;

    HandleId on_created(Callback fn)
    {
        const auto id = next_id_++;
        created_.push_back({ id, std::move(fn) });
        return id;
    }

    HandleId on_destroyed(Callback fn)
    {
        const auto id = next_id_++;
        destroyed_.push_back({ id, std::move(fn) });
        return id;
    }

    bool remove(HandleId id) noexcept
    {
        auto erase_from = [id](std::vector<Slot>& v)
        {
            for (auto it = v.begin(); it != v.end(); ++it)
            {
                if (it->id == id) { v.erase(it); return true; }
            }
            return false;
        };
        return erase_from(created_) || erase_from(destroyed_);
    }

    void notify_created(Entity e) const
    {
        for (const auto& s : created_) s.fn(e);
    }

    void notify_destroyed(Entity e) const
    {
        for (const auto& s : destroyed_) s.fn(e);
    }

    [[nodiscard]] std::size_t created_handler_count() const noexcept { return created_.size(); }
    [[nodiscard]] std::size_t destroyed_handler_count() const noexcept { return destroyed_.size(); }

private:
    struct Slot
    {
        HandleId id;
        Callback fn;
    };
    std::vector<Slot> created_;
    std::vector<Slot> destroyed_;
    HandleId          next_id_ { 1 };
};

}  // namespace cd::ecs
