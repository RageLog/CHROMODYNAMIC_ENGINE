// =============================================================================
// CHROMODYNAMIC — cd/ecs/Generation.hpp
// Phase 92.B / Wave 260 — entity-generation predicate helpers.
//
// Entity stores (id, generation). Stale handles (from before a
// recycle) can be detected by comparing the cached generation to the
// current one in the EntityManager. These free-function helpers
// document the comparison intent + provide nullity probes:
//
//   * `is_null(e)` — generation == 0 (never assigned).
//   * `same_generation(a, b)` — id + generation match.
//   * `stale(handle, current_generation)` — handle.generation differs.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>

#include <cstdint>

namespace cd::ecs
{

[[nodiscard]] constexpr bool is_null(Entity e) noexcept
{
    return e.generation == 0;
}

[[nodiscard]] constexpr bool same_generation(Entity a, Entity b) noexcept
{
    return a.id == b.id && a.generation == b.generation;
}

[[nodiscard]] constexpr bool stale(Entity handle, std::uint32_t current_generation) noexcept
{
    return handle.generation != current_generation;
}

}  // namespace cd::ecs
