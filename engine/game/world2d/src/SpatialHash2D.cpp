// =============================================================================
// CHROMODYNAMIC — cd/world2d/SpatialHash2D.cpp
// Milestone 3 — Uniform Grid 2D Spatial Hash Broadphase Implementation.
// =============================================================================
#include <cd/world2d/SpatialHash2D.hpp>

#include <algorithm>

namespace cd::world2d
{

SpatialHash2D::SpatialHash2D(float cell_size, std::size_t bucket_reserve)
    : cell_size_(std::max(0.1F, cell_size))
    , inv_cell_size_(1.0F / cell_size_)
{
    entries_.reserve(bucket_reserve);
    grid_.reserve(bucket_reserve * 2U);
}

void SpatialHash2D::set_cell_size(float cell_size)
{
    const float new_size = std::max(0.1F, cell_size);
    if (std::abs(new_size - cell_size_) < 1.0e-5F)
    {
        return;
    }
    cell_size_     = new_size;
    inv_cell_size_ = 1.0F / cell_size_;

    // Rebuild grid and oversized list with new cell dimensions
    grid_.clear();
    oversized_items_.clear();

    for (auto& [id, entry] : entries_)
    {
        const auto& bounds = entry.bounds;
        if (bounds.min.x > bounds.max.x || bounds.min.y > bounds.max.y)
        {
            entry.is_oversized = false;
            continue;
        }

        const std::int32_t min_cx = to_cell_coord(bounds.min.x);
        const std::int32_t max_cx = to_cell_coord(bounds.max.x);
        const std::int32_t min_cy = to_cell_coord(bounds.min.y);
        const std::int32_t max_cy = to_cell_coord(bounds.max.y);

        const std::int64_t span_x = static_cast<std::int64_t>(max_cx) - min_cx + 1;
        const std::int64_t span_y = static_cast<std::int64_t>(max_cy) - min_cy + 1;

        if (span_x > kMaxCellSpan || span_y > kMaxCellSpan)
        {
            entry.is_oversized = true;
            oversized_items_.push_back(id);
        }
        else
        {
            entry.is_oversized = false;
            for (std::int32_t cy = min_cy; cy <= max_cy; ++cy)
            {
                for (std::int32_t cx = min_cx; cx <= max_cx; ++cx)
                {
                    grid_[pack_cell_key(cx, cy)].push_back(id);
                }
            }
        }
    }
}

void SpatialHash2D::insert(std::uint32_t id, const Aabb2D& bounds)
{
    if (entries_.contains(id))
    {
        update(id, bounds);
        return;
    }

    SpatialEntry2D entry {};
    entry.bounds       = bounds;
    entry.query_stamp  = 0U;
    entry.is_oversized = false;

    if (bounds.min.x > bounds.max.x || bounds.min.y > bounds.max.y)
    {
        // Inverted/degenerate bounds: track entry but do not index in grid
        entries_[id] = entry;
        return;
    }

    const std::int32_t min_cx = to_cell_coord(bounds.min.x);
    const std::int32_t max_cx = to_cell_coord(bounds.max.x);
    const std::int32_t min_cy = to_cell_coord(bounds.min.y);
    const std::int32_t max_cy = to_cell_coord(bounds.max.y);

    const std::int64_t span_x = static_cast<std::int64_t>(max_cx) - min_cx + 1;
    const std::int64_t span_y = static_cast<std::int64_t>(max_cy) - min_cy + 1;

    if (span_x > kMaxCellSpan || span_y > kMaxCellSpan)
    {
        entry.is_oversized = true;
        oversized_items_.push_back(id);
    }
    else
    {
        for (std::int32_t cy = min_cy; cy <= max_cy; ++cy)
        {
            for (std::int32_t cx = min_cx; cx <= max_cx; ++cx)
            {
                grid_[pack_cell_key(cx, cy)].push_back(id);
            }
        }
    }

    entries_[id] = entry;
}

bool SpatialHash2D::remove(std::uint32_t id)
{
    const auto it = entries_.find(id);
    if (it == entries_.end())
    {
        return false;
    }

    const auto& entry = it->second;
    if (entry.is_oversized)
    {
        std::erase(oversized_items_, id);
    }
    else
    {
        const Aabb2D bounds = entry.bounds;
        if (bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y)
        {
            const std::int32_t min_cx = to_cell_coord(bounds.min.x);
            const std::int32_t max_cx = to_cell_coord(bounds.max.x);
            const std::int32_t min_cy = to_cell_coord(bounds.min.y);
            const std::int32_t max_cy = to_cell_coord(bounds.max.y);

            for (std::int32_t cy = min_cy; cy <= max_cy; ++cy)
            {
                for (std::int32_t cx = min_cx; cx <= max_cx; ++cx)
                {
                    const auto git = grid_.find(pack_cell_key(cx, cy));
                    if (git != grid_.end())
                    {
                        std::erase(git->second, id);
                        if (git->second.empty())
                        {
                            grid_.erase(git);
                        }
                    }
                }
            }
        }
    }

    entries_.erase(it);
    return true;
}

void SpatialHash2D::update(std::uint32_t id, const Aabb2D& new_bounds)
{
    remove(id);
    insert(id, new_bounds);
}

void SpatialHash2D::clear() noexcept
{
    entries_.clear();
    grid_.clear();
    oversized_items_.clear();
    current_query_stamp_ = 0U;
}

bool SpatialHash2D::contains(std::uint32_t id) const noexcept
{
    return entries_.contains(id);
}

const Aabb2D* SpatialHash2D::find_bounds(std::uint32_t id) const noexcept
{
    const auto it = entries_.find(id);
    return (it != entries_.end()) ? &it->second.bounds : nullptr;
}

void SpatialHash2D::reset_query_stamps() const noexcept
{
    for (auto& [id, entry] : entries_)
    {
        entry.query_stamp = 0U;
    }
    current_query_stamp_ = 1U;
}

void SpatialHash2D::query_aabb(
    const Aabb2D&               query_box,
    std::vector<std::uint32_t>& out_results) const
{
    out_results.clear();
    if (entries_.empty())
    {
        return;
    }

    // Inverted bounds early-out
    if (query_box.min.x > query_box.max.x || query_box.min.y > query_box.max.y)
    {
        return;
    }

    const std::int32_t min_cx = to_cell_coord(query_box.min.x);
    const std::int32_t max_cx = to_cell_coord(query_box.max.x);
    const std::int32_t min_cy = to_cell_coord(query_box.min.y);
    const std::int32_t max_cy = to_cell_coord(query_box.max.y);

    const std::int64_t span_x = static_cast<std::int64_t>(max_cx) - min_cx + 1;
    const std::int64_t span_y = static_cast<std::int64_t>(max_cy) - min_cy + 1;
    if (span_x <= 0 || span_y <= 0)
    {
        return;
    }

    const cd::math::Aabb2 q_math = query_box.to_math_aabb();

    // If query box exceeds cell span limit, fallback to linear scan of entries_ directly.
    // This avoids iterating tens/hundreds of thousands of grid cells while guaranteeing 100% recall.
    if (span_x > kMaxCellSpan || span_y > kMaxCellSpan)
    {
        for (const auto& [id, entry] : entries_)
        {
            if (entry.bounds.min.x <= entry.bounds.max.x &&
                entry.bounds.min.y <= entry.bounds.max.y &&
                cd::math::overlaps(entry.bounds.to_math_aabb(), q_math))
            {
                out_results.push_back(id);
            }
        }
        return;
    }

    std::uint64_t stamp = ++current_query_stamp_;
    if (stamp == 0U) [[unlikely]]
    {
        reset_query_stamps();
        stamp = current_query_stamp_;
    }

    // 1. Grid cell traversal with O(1) query stamp deduplication
    for (std::int32_t cy = min_cy; cy <= max_cy; ++cy)
    {
        for (std::int32_t cx = min_cx; cx <= max_cx; ++cx)
        {
            const auto git = grid_.find(pack_cell_key(cx, cy));
            if (git == grid_.end())
            {
                continue;
            }

            for (std::uint32_t id : git->second)
            {
                const auto eit = entries_.find(id);
                if (eit == entries_.end())
                {
                    continue;
                }
                auto& entry = eit->second;
                if (entry.query_stamp == stamp)
                {
                    continue; // Already processed during this query!
                }
                entry.query_stamp = stamp;

                if (cd::math::overlaps(entry.bounds.to_math_aabb(), q_math))
                {
                    out_results.push_back(id);
                }
            }
        }
    }

    // 2. Oversized items check
    for (std::uint32_t id : oversized_items_)
    {
        const auto eit = entries_.find(id);
        if (eit == entries_.end())
        {
            continue;
        }
        auto& entry = eit->second;
        if (entry.query_stamp == stamp)
        {
            continue;
        }
        entry.query_stamp = stamp;

        if (cd::math::overlaps(entry.bounds.to_math_aabb(), q_math))
        {
            out_results.push_back(id);
        }
    }
}

void SpatialHash2D::query_circle(
    cd::math::Vec2f             center,
    float                       radius,
    std::vector<std::uint32_t>& out_results) const
{
    const float r = std::max(0.0F, radius);
    const Aabb2D box {
        { center.x - r, center.y - r },
        { center.x + r, center.y + r }
    };
    query_aabb(box, out_results);

    const float r_sq = r * r;
    std::erase_if(out_results, [this, center, r_sq](std::uint32_t id) noexcept {
        const auto it = entries_.find(id);
        if (it == entries_.end())
        {
            return true;
        }
        const auto& b = it->second.bounds;
        const float closest_x = std::clamp(center.x, b.min.x, b.max.x);
        const float closest_y = std::clamp(center.y, b.min.y, b.max.y);
        const float dx = center.x - closest_x;
        const float dy = center.y - closest_y;
        return (dx * dx + dy * dy) > r_sq;
    });
}

void SpatialHash2D::query_point(
    cd::math::Vec2f             point,
    std::vector<std::uint32_t>& out_results) const
{
    out_results.clear();
    if (entries_.empty())
    {
        return;
    }

    std::uint64_t stamp = ++current_query_stamp_;
    if (stamp == 0U) [[unlikely]]
    {
        reset_query_stamps();
        stamp = current_query_stamp_;
    }

    // 1. Grid cell lookup
    const auto git = grid_.find(pack_cell_key(to_cell_coord(point.x), to_cell_coord(point.y)));
    if (git != grid_.end())
    {
        for (std::uint32_t id : git->second)
        {
            const auto eit = entries_.find(id);
            if (eit == entries_.end())
            {
                continue;
            }
            auto& entry = eit->second;
            if (entry.query_stamp == stamp)
            {
                continue;
            }
            entry.query_stamp = stamp;

            const auto& b = entry.bounds;
            if (point.x >= b.min.x && point.x <= b.max.x &&
                point.y >= b.min.y && point.y <= b.max.y)
            {
                out_results.push_back(id);
            }
        }
    }

    // 2. Oversized items check
    for (std::uint32_t id : oversized_items_)
    {
        const auto eit = entries_.find(id);
        if (eit == entries_.end())
        {
            continue;
        }
        auto& entry = eit->second;
        if (entry.query_stamp == stamp)
        {
            continue;
        }
        entry.query_stamp = stamp;

        const auto& b = entry.bounds;
        if (point.x >= b.min.x && point.x <= b.max.x &&
            point.y >= b.min.y && point.y <= b.max.y)
        {
            out_results.push_back(id);
        }
    }
}

std::vector<std::uint32_t> SpatialHash2D::query_aabb(const Aabb2D& query_box) const
{
    std::vector<std::uint32_t> results;
    query_aabb(query_box, results);
    return results;
}

std::vector<std::uint32_t> SpatialHash2D::query_circle(cd::math::Vec2f center, float radius) const
{
    std::vector<std::uint32_t> results;
    query_circle(center, radius, results);
    return results;
}

std::vector<std::uint32_t> SpatialHash2D::query_point(cd::math::Vec2f point) const
{
    std::vector<std::uint32_t> results;
    query_point(point, results);
    return results;
}

}  // namespace cd::world2d
