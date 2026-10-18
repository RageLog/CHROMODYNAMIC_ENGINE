// =============================================================================
// CHROMODYNAMIC — cd/physics/core/SpatialAabb3D.hpp
// 3D axis-aligned bounding box with surface-area heuristic (SAH), fat-margin
// expansion, containment/overlap queries, and slab raycast.
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

struct SpatialAabb3D
{
    cd::math::Vec3f min { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f max { 0.0F, 0.0F, 0.0F };

    [[nodiscard]] constexpr cd::math::Vec3f center() const noexcept
    {
        return (min + max) * 0.5F;
    }

    [[nodiscard]] constexpr cd::math::Vec3f extents() const noexcept
    {
        return (max - min) * 0.5F;
    }

    [[nodiscard]] constexpr cd::math::Vec3f size() const noexcept
    {
        return max - min;
    }

    [[nodiscard]] constexpr float surface_area() const noexcept
    {
        const float dx = std::max(0.0F, max.x - min.x);
        const float dy = std::max(0.0F, max.y - min.y);
        const float dz = std::max(0.0F, max.z - min.z);
        return 2.0F * (dx * dy + dy * dz + dz * dx);
    }

    [[nodiscard]] constexpr float volume() const noexcept
    {
        const float dx = std::max(0.0F, max.x - min.x);
        const float dy = std::max(0.0F, max.y - min.y);
        const float dz = std::max(0.0F, max.z - min.z);
        return dx * dy * dz;
    }

    [[nodiscard]] constexpr bool overlaps(const SpatialAabb3D& o) const noexcept
    {
        return min.x <= o.max.x && max.x >= o.min.x &&
               min.y <= o.max.y && max.y >= o.min.y &&
               min.z <= o.max.z && max.z >= o.min.z;
    }

    [[nodiscard]] constexpr bool contains(const SpatialAabb3D& o) const noexcept
    {
        return min.x <= o.min.x && min.y <= o.min.y && min.z <= o.min.z &&
               o.max.x <= max.x && o.max.y <= max.y && o.max.z <= max.z;
    }

    [[nodiscard]] constexpr bool contains(cd::math::Vec3f p) const noexcept
    {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }

    [[nodiscard]] constexpr SpatialAabb3D fattened(float margin) const noexcept
    {
        const cd::math::Vec3f pad { margin, margin, margin };
        return SpatialAabb3D { min - pad, max + pad };
    }

    [[nodiscard]] static constexpr SpatialAabb3D merge(const SpatialAabb3D& a, const SpatialAabb3D& b) noexcept
    {
        return SpatialAabb3D {
            cd::math::min(a.min, b.min),
            cd::math::max(a.max, b.max),
        };
    }

    [[nodiscard]] bool raycast(cd::math::Vec3f origin,
                               cd::math::Vec3f dir,
                               float           max_fraction,
                               float&          out_tmin) const noexcept
    {
        if (max_fraction < 0.0F)
        {
            return false;
        }
        float tmin = 0.0F;
        float tmax = max_fraction;

        for (std::size_t i = 0; i < 3; ++i)
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

    [[nodiscard]] bool raycast(cd::math::Vec3f origin, cd::math::Vec3f dir, float max_fraction = 1.0F) const noexcept
    {
        float tmin = 0.0F;
        return raycast(origin, dir, max_fraction, tmin);
    }

    friend constexpr bool operator==(const SpatialAabb3D&, const SpatialAabb3D&) noexcept = default;
};

[[nodiscard]] constexpr SpatialAabb3D merge(const SpatialAabb3D& a, const SpatialAabb3D& b) noexcept
{
    return SpatialAabb3D::merge(a, b);
}

}  // namespace cd::physics::core
