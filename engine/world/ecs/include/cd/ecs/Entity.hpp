// =============================================================================
// CHROMODYNAMIC — cd/ecs/Entity.hpp
// Phase 4 / Sprint S4.1 — Entity handle + generational allocator.
//
// An `Entity` is a 64-bit opaque value: 32-bit slot id + 32-bit generation.
// The generation is bumped every time a slot is reused so a stale handle
// from before destroy() can be detected and rejected (the classic ABA fix
// for object pools).
//
// SOTA references: EnTT [Skypjack], Bevy ECS, Flecs — all converge on
// generational integer handles for hot-path entity validation.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cd::ecs
{

struct Entity
{
    std::uint32_t id { 0 };          ///< Slot index inside the world's storage.
    std::uint32_t generation { 0 };  ///< 0 = invalid; bumped on every reuse.

    [[nodiscard]] bool is_valid() const noexcept
    {
        return generation != 0;
    }

    [[nodiscard]] friend bool operator==(Entity a, Entity b) noexcept
    {
        return a.id == b.id && a.generation == b.generation;
    }

    [[nodiscard]] friend bool operator!=(Entity a, Entity b) noexcept
    {
        return !(a == b);
    }
};

/// Allocator that hands out generational slot ids. The destroy() / is_alive()
/// implementation is the classic "free list of indices, generation counter
/// per slot" pattern: O(1) create, destroy, and validity check.
class EntityManager
{
public:
    EntityManager() noexcept = default;

    [[nodiscard]] Entity create();
    void destroy(Entity e);
    [[nodiscard]] bool is_alive(Entity e) const noexcept;

    /// Number of entities currently alive (created and not destroyed).
    [[nodiscard]] std::size_t alive_count() const noexcept
    {
        return alive_;
    }

    /// Number of slots ever allocated (capacity floor, includes recycled).
    [[nodiscard]] std::size_t slot_count() const noexcept
    {
        return generations_.size();
    }

    /// Visit every live entity. `fn(Entity)` callable. Stable order matches
    /// slot index for tests; do not rely on it for behavior.
    template <class Fn>
    void for_each(Fn&& fn) const
    {
        for (std::uint32_t i = 0; i < generations_.size(); ++i)
        {
            const auto gen = generations_[i];
            if ((gen & 1U) != 0U)
            {
                // Odd generation == alive. Reuse bumps generation by 1 so each
                // (id, generation) pair is unique across the entity's lifetime.
                fn(Entity { i, gen });
            }
        }
    }

private:
    /// Per-slot generation. Odd → alive, even → free. Starting value is 0
    /// (free) so the very first generation handed out is 1.
    std::vector<std::uint32_t> generations_ {};
    /// Free-slot index stack — LIFO so the hot cache stays warm on rapid
    /// create/destroy cycles.
    std::vector<std::uint32_t> free_slots_ {};
    std::size_t alive_ { 0 };
};

}  // namespace cd::ecs
