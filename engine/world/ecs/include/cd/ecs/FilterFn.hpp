// =============================================================================
// CHROMODYNAMIC — cd/ecs/FilterFn.hpp
// Phase 55.A / Wave 223 — predicate filter over an entity range.
//
// `filter_entities(span, predicate)` returns a fresh vector of the
// subset of entities for which `predicate(entity)` returns true.
// `count_entities(span, predicate)` is the count-only variant (no
// allocation).
//
// Use case: editor selection refine ("among the 50 selected, show me
// only the ones tagged 'static'"), debug overlays (highlight entities
// matching some condition), tooling queries.
//
// O(n); caller provides the snapshot. The header avoids depending on
// World — the predicate captures whatever context it needs.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace cd::ecs
{

template <class Pred>
[[nodiscard]] inline std::vector<Entity> filter_entities(
    std::span<const Entity> all, Pred predicate)
{
    std::vector<Entity> out;
    out.reserve(all.size() / 2);  // conservative bound
    for (const auto e : all)
        if (predicate(e)) out.push_back(e);
    return out;
}

template <class Pred>
[[nodiscard]] inline std::size_t count_entities(
    std::span<const Entity> all, Pred predicate)
{
    std::size_t n = 0;
    for (const auto e : all)
        if (predicate(e)) ++n;
    return n;
}

}  // namespace cd::ecs
