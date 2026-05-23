// =============================================================================
// CHROMODYNAMIC — cd/ecs/TagHelpers.hpp
// Phase 35.A / Wave 203 — sugar for tag (empty) components.
//
// A tag component is an empty struct that marks an entity:
//
//   struct Selected {};
//   struct Disabled {};
//
// Without sugar, marking and querying tags reads:
//   world.emplace<Selected>(e);
//   if (world.get<Selected>(e) != nullptr) { ... }
//
// With sugar:
//   tag<Selected>(world, e);
//   if (has_tag<Selected>(world, e)) { ... }
//
// The sugar is name-only — no extra storage, no extra registry. Same
// `emplace`/`get`/`remove` path under the hood, but the names match
// the intent. Zero-size component (EBO) means ComponentStorage uses
// only its sparse-set bookkeeping, no payload bytes per entity.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>
#include <cd/ecs/World.hpp>

#include <type_traits>

namespace cd::ecs
{

template <class Tag>
inline void tag(World& w, Entity e)
{
    static_assert(std::is_empty_v<Tag> || std::is_default_constructible_v<Tag>,
                  "Tag<T> expects T to be empty or default-constructible");
    w.emplace<Tag>(e);
}

template <class Tag>
[[nodiscard]] inline bool has_tag(const World& w, Entity e) noexcept
{
    return w.get<Tag>(e) != nullptr;
}

template <class Tag>
inline void untag(World& w, Entity e)
{
    w.remove<Tag>(e);
}

}  // namespace cd::ecs
