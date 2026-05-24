// =============================================================================
// CHROMODYNAMIC — cd/ecs/EntityRange.hpp
// Phase 46.B / Wave 214 — paginated query helpers.
//
// Editor / inspector lists need to iterate a possibly-large entity
// query in fixed-size pages (e.g., "show 20 selected entities, prev /
// next page"). `EntityPage` is the lightweight value-type result of
// `paginate(query, page_size, page_index)`:
//
//   * `entities` — the slice for this page (std::span into caller storage).
//   * `page_index` — same as passed in.
//   * `page_count` — total pages.
//   * `total` — the unpaged entity count.
//
// The helper does NOT iterate the World — it operates on a caller-
// provided `std::span<const Entity>` snapshot. Build that snapshot
// once per frame from your Query.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/ecs/Entity.hpp>

#include <algorithm>
#include <cstddef>
#include <span>

namespace cd::ecs
{

struct EntityPage
{
    std::span<const Entity> entities;
    std::size_t             page_index { 0 };
    std::size_t             page_count { 0 };
    std::size_t             total { 0 };
};

[[nodiscard]] inline EntityPage paginate(std::span<const Entity> all,
                                         std::size_t page_size,
                                         std::size_t page_index) noexcept
{
    EntityPage out;
    out.total = all.size();
    out.page_count = (page_size == 0)
        ? 0
        : (all.size() + page_size - 1) / page_size;
    out.page_index = std::min(page_index,
                              out.page_count > 0 ? out.page_count - 1 : std::size_t { 0 });
    if (page_size == 0 || all.empty())
    {
        out.entities = std::span<const Entity> {};
        return out;
    }
    const std::size_t start = out.page_index * page_size;
    const std::size_t end   = std::min(start + page_size, all.size());
    out.entities = all.subspan(start, end - start);
    return out;
}

}  // namespace cd::ecs
