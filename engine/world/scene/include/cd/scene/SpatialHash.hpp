// =============================================================================
// CHROMODYNAMIC — cd/scene/SpatialHash.hpp
// Phase 22.A / Wave 186 — uniform-grid spatial hash.
//
// A header-only neighbour-query accelerator. Insert (T, world position),
// then query a sphere or AABB region to enumerate candidates. Useful
// for broad-phase culling, AOI queries, and "find entities within
// radius" gameplay code before a full BVH lands.
//
// Cell size at construction. Cells are addressed by integer
// coordinates (floor(world / cell_size)). Each cell holds a
// std::vector<T>; queries union the lists across overlapped cells.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cd::scene
{

namespace detail
{

struct CellKey
{
    std::int32_t x { 0 }, y { 0 }, z { 0 };
    bool operator==(const CellKey& o) const noexcept { return x == o.x && y == o.y && z == o.z; }
};

struct CellKeyHash
{
    std::size_t operator()(const CellKey& k) const noexcept
    {
        // Simple mix; good enough for a 3D integer key.
        std::size_t h = static_cast<std::size_t>(k.x) * 73856093u;
        h ^= static_cast<std::size_t>(k.y) * 19349663u;
        h ^= static_cast<std::size_t>(k.z) * 83492791u;
        return h;
    }
};

}  // namespace detail

template <class T>
class SpatialHash
{
public:
    explicit SpatialHash(float cell_size) noexcept
        : cell_size_ { cell_size > 0.0F ? cell_size : 1.0F }
    {
    }

    void insert(const T& item, const cd::math::Vec3f& pos)
    {
        cells_[cell_of(pos)].push_back(item);
    }

    void clear() noexcept { cells_.clear(); }

    /// Append candidates within `radius` of `center` to `out`. May
    /// over-report (candidates outside the radius but in the touched
    /// cells); caller filters by actual distance.
    void query_sphere(const cd::math::Vec3f& center, float radius,
                      std::vector<T>& out) const
    {
        const float r = radius < 0.0F ? 0.0F : radius;
        const auto lo = cell_of({ center.x - r, center.y - r, center.z - r });
        const auto hi = cell_of({ center.x + r, center.y + r, center.z + r });
        for (std::int32_t z = lo.z; z <= hi.z; ++z)
            for (std::int32_t y = lo.y; y <= hi.y; ++y)
                for (std::int32_t x = lo.x; x <= hi.x; ++x)
                {
                    auto it = cells_.find(detail::CellKey { x, y, z });
                    if (it != cells_.end())
                        for (const auto& v : it->second) out.push_back(v);
                }
    }

    [[nodiscard]] float cell_size() const noexcept { return cell_size_; }
    [[nodiscard]] std::size_t cell_count() const noexcept { return cells_.size(); }

private:
    [[nodiscard]] detail::CellKey cell_of(const cd::math::Vec3f& p) const noexcept
    {
        return detail::CellKey {
            static_cast<std::int32_t>(std::floor(p.x / cell_size_)),
            static_cast<std::int32_t>(std::floor(p.y / cell_size_)),
            static_cast<std::int32_t>(std::floor(p.z / cell_size_)),
        };
    }

    float cell_size_ { 1.0F };
    std::unordered_map<detail::CellKey, std::vector<T>,
                       detail::CellKeyHash> cells_;
};

}  // namespace cd::scene
