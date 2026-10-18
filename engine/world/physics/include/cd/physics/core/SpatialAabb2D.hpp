// =============================================================================
// CHROMODYNAMIC — cd/physics/core/SpatialAabb2D.hpp
// 2D axis-aligned bounding box with perimeter heuristic, fat-margin expansion,
// containment/overlap queries, and slab raycast.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace cd::physics::core
{

struct SpatialAabb2D
{
    cd::math::Vec2f min { 0.0F, 0.0F };
    cd::math::Vec2f max { 0.0F, 0.0F };

    [[nodiscard]] constexpr cd::math::Vec2f center() const noexcept
    {
        return (min + max) * 0.5F;
    }

    [[nodiscard]] constexpr cd::math::Vec2f extents() const noexcept
    {
        return (max - min) * 0.5F;
    }

    [[nodiscard]] constexpr cd::math::Vec2f size() const noexcept
    {
        return max - min;
    }

    [[nodiscard]] constexpr float perimeter() const noexcept
    {
        const float wx = std::max(0.0F, max.x - min.x);
        const float wy = std::max(0.0F, max.y - min.y);
        return 2.0F * (wx + wy);
    }

    [[nodiscard]] constexpr float area() const noexcept
    {
        const float wx = std::max(0.0F, max.x - min.x);
        const float wy = std::max(0.0F, max.y - min.y);
        return wx * wy;
    }

    [[nodiscard]] constexpr bool overlaps(const SpatialAabb2D& o) const noexcept
    {
        return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y;
    }

    [[nodiscard]] constexpr bool contains(const SpatialAabb2D& o) const noexcept
    {
        return min.x <= o.min.x && min.y <= o.min.y && o.max.x <= max.x && o.max.y <= max.y;
    }

    [[nodiscard]] constexpr bool contains(cd::math::Vec2f p) const noexcept
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }

    [[nodiscard]] constexpr SpatialAabb2D fattened(float margin) const noexcept
    {
        const cd::math::Vec2f pad { margin, margin };
        return SpatialAabb2D { min - pad, max + pad };
    }

    [[nodiscard]] static constexpr SpatialAabb2D merge(const SpatialAabb2D& a, const SpatialAabb2D& b) noexcept
    {
        return SpatialAabb2D {
            cd::math::min(a.min, b.min),
            cd::math::max(a.max, b.max),
        };
    }

    [[nodiscard]] bool raycast(cd::math::Vec2f origin,
                               cd::math::Vec2f dir,
                               float           max_fraction,
                               float&          out_tmin) const noexcept
    {
        if (max_fraction < 0.0F)
        {
            return false;
        }
        float tmin = 0.0F;
        float tmax = max_fraction;

        for (std::size_t i = 0; i < 2; ++i)
        {
            if (std::abs(dir[i]) < 1e-8F)
            {
                if (origin[i] < min[i] || origin[i] > max[i])
                {
                    return false;
                }
            }
            else
            {
                const float inv_d = 1.0F / dir[i];
                float       t1    = (min[i] - origin[i]) * inv_d;
                float       t2    = (max[i] - origin[i]) * inv_d;
                if (t1 > t2)
                {
                    std::swap(t1, t2);
                }
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax)
                {
                    return false;
                }
            }
        }

        out_tmin = tmin;
        return true;
    }

    [[nodiscard]] bool raycast(cd::math::Vec2f origin, cd::math::Vec2f dir, float max_fraction = 1.0F) const noexcept
    {
        float tmin = 0.0F;
        return raycast(origin, dir, max_fraction, tmin);
    }

    friend constexpr bool operator==(const SpatialAabb2D&, const SpatialAabb2D&) noexcept = default;
};

[[nodiscard]] constexpr SpatialAabb2D merge(const SpatialAabb2D& a, const SpatialAabb2D& b) noexcept
{
    return SpatialAabb2D::merge(a, b);
}

}  // namespace cd::physics::core
