// =============================================================================
// CHROMODYNAMIC — cd/editor/SelectionSet.hpp
// Phase 49.A / Wave 217 — multi-entity selection state.
//
// Editor inspector/viewport need a shared "what's selected right now"
// container. SelectionSet is a sorted-vector wrapper (small N: typical
// selection counts ≤ ~256) supporting:
//   * `add(e)` / `remove(e)` — toggle membership.
//   * `contains(e)` — O(log n) binary search.
//   * `clear()` — drop all.
//   * `primary()` — the most-recently-added entity, used by inspectors
//                   that show ONE entity's properties even when multi-
//                   selected.
//
// Sorted-vector wins over std::unordered_set for small N due to cache
// locality + zero per-entry allocation. Sorted-vector wins over
// std::set due to the same locality argument.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cd::editor
{

class SelectionSet
{
public:
    void add(cd::ecs::Entity e)
    {
        auto it = std::lower_bound(entries_.begin(), entries_.end(), e.id,
            [](cd::ecs::Entity a, std::uint32_t v) { return a.id < v; });
        if (it == entries_.end() || it->id != e.id)
            entries_.insert(it, e);
        primary_ = e;
    }

    void remove(cd::ecs::Entity e)
    {
        auto it = std::lower_bound(entries_.begin(), entries_.end(), e.id,
            [](cd::ecs::Entity a, std::uint32_t v) { return a.id < v; });
        if (it != entries_.end() && it->id == e.id)
        {
            entries_.erase(it);
            if (primary_ == e)
                primary_ = entries_.empty() ? cd::ecs::Entity {} : entries_.back();
        }
    }

    [[nodiscard]] bool contains(cd::ecs::Entity e) const noexcept
    {
        auto it = std::lower_bound(entries_.begin(), entries_.end(), e.id,
            [](cd::ecs::Entity a, std::uint32_t v) { return a.id < v; });
        return it != entries_.end() && it->id == e.id;
    }

    void clear() noexcept
    {
        entries_.clear();
        primary_ = cd::ecs::Entity {};
    }

    [[nodiscard]] cd::ecs::Entity primary() const noexcept { return primary_; }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    [[nodiscard]] const std::vector<cd::ecs::Entity>& entries() const noexcept { return entries_; }

private:
    std::vector<cd::ecs::Entity> entries_;
    cd::ecs::Entity              primary_ {};
};

}  // namespace cd::editor
