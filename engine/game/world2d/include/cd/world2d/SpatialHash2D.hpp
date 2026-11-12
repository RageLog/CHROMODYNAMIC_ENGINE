// =============================================================================
// CHROMODYNAMIC — cd/world2d/SpatialHash2D.hpp
// Milestone 3 — Uniform Grid 2D Spatial Hash Broadphase for Deduplicated
// AABB, Circle, and Point Collision Queries with Span Limit Clamping.
// =============================================================================
#pragma once

#include <cd/math/Aabb2.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cd::world2d
{

struct Circle2D
{
    cd::math::Vec2f center { 0.0F, 0.0F };
    float           radius { 1.0F };
};

struct Aabb2D
{
    cd::math::Vec2f min { 0.0F, 0.0F };
    cd::math::Vec2f max { 0.0F, 0.0F };

    constexpr Aabb2D() = default;
    constexpr Aabb2D(cd::math::Vec2f min_pt, cd::math::Vec2f max_pt) noexcept
        : min(min_pt), max(max_pt)
    {
    }

    constexpr Aabb2D(const cd::math::Aabb2& b) noexcept
        : min { b.min_x, b.min_y }, max { b.max_x, b.max_y }
    {
    }

    [[nodiscard]] constexpr cd::math::Aabb2 to_math_aabb() const noexcept
    {
        return cd::math::Aabb2 { min.x, min.y, max.x, max.y };
    }
};

/// Internal registration metadata for an entity inserted into SpatialHash2D.
struct SpatialEntry2D
{
    Aabb2D                bounds {};
    mutable std::uint64_t query_stamp { 0U };
    bool                  is_oversized { false };
};

class SpatialHash2D
{
public:
    /// Maximum cell span allowed along either X or Y dimension for grid indexing.
    /// Entities exceeding this span are handled safely via the oversized list.
    static constexpr std::int32_t kMaxCellSpan = 128;

    explicit SpatialHash2D(float cell_size = 2.0F, std::size_t bucket_reserve = 512U);

    [[nodiscard]] float cell_size() const noexcept { return cell_size_; }
    void set_cell_size(float cell_size);

    void insert(std::uint32_t id, const Aabb2D& bounds);
    void insert(std::uint32_t id, const cd::math::Aabb2& bounds)
    {
        insert(id, Aabb2D(bounds));
    }

    bool remove(std::uint32_t id);

    void update(std::uint32_t id, const Aabb2D& new_bounds);
    void update(std::uint32_t id, const cd::math::Aabb2& new_bounds)
    {
        update(id, Aabb2D(new_bounds));
    }

    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t entity_count() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] bool contains(std::uint32_t id) const noexcept;
    [[nodiscard]] const Aabb2D* find_bounds(std::uint32_t id) const noexcept;

    void query_aabb(
        const Aabb2D&               query_box,
        std::vector<std::uint32_t>& out_results) const;

    void query_aabb(
        const cd::math::Aabb2&      query_box,
        std::vector<std::uint32_t>& out_results) const
    {
        query_aabb(Aabb2D(query_box), out_results);
    }

    void query_circle(
        cd::math::Vec2f             center,
        float                       radius,
        std::vector<std::uint32_t>& out_results) const;

    void query_circle(
        const Circle2D&             circle,
        std::vector<std::uint32_t>& out_results) const
    {
        query_circle(circle.center, circle.radius, out_results);
    }

    void query_point(
        cd::math::Vec2f             point,
        std::vector<std::uint32_t>& out_results) const;

    [[nodiscard]] std::vector<std::uint32_t> query_aabb(const Aabb2D& query_box) const;
    [[nodiscard]] std::vector<std::uint32_t> query_aabb(const cd::math::Aabb2& query_box) const
    {
        return query_aabb(Aabb2D(query_box));
    }

    [[nodiscard]] std::vector<std::uint32_t> query_circle(cd::math::Vec2f center, float radius) const;
    [[nodiscard]] std::vector<std::uint32_t> query_circle(const Circle2D& circle) const
    {
        return query_circle(circle.center, circle.radius);
    }

    [[nodiscard]] std::vector<std::uint32_t> query_point(cd::math::Vec2f point) const;

private:
    [[nodiscard]] constexpr std::int32_t to_cell_coord(float v) const noexcept
    {
        if (std::isnan(v))
        {
            return 0;
        }
        constexpr float kMaxSafeCoord = 1.0e7F;
        const float clamped_v = std::clamp(v, -kMaxSafeCoord, kMaxSafeCoord);
        return static_cast<std::int32_t>(std::floor(clamped_v * inv_cell_size_));
    }

    [[nodiscard]] static constexpr std::uint64_t pack_cell_key(std::int32_t cx, std::int32_t cy) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32U) |
               static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy));
    }

    void reset_query_stamps() const noexcept;

    float cell_size_ { 2.0F };
    float inv_cell_size_ { 0.5F };

    mutable std::uint64_t                                         current_query_stamp_ { 0U };
    std::unordered_map<std::uint32_t, SpatialEntry2D>             entries_ {};
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> grid_ {};
    std::vector<std::uint32_t>                                    oversized_items_ {};
};

}  // namespace cd::world2d
