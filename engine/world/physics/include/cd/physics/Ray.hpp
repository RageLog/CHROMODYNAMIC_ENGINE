// =============================================================================
// CHROMODYNAMIC — cd/physics/Ray.hpp
// Phase 22.C / Wave 186 — header-only ray primitive + AABB intersection.
//
// Slab-method ray/AABB intersection. Returns the parametric t at which
// the ray enters the AABB; the hit point is `origin + direction * t`.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>
#include <cd/physics/Aabb.hpp>

#include <algorithm>
#include <optional>

namespace cd::physics
{

struct Ray
{
    cd::math::Vec3f origin {};
    cd::math::Vec3f direction { 0.0F, 0.0F, -1.0F };
};

/// Slab-method ray/AABB intersection. Returns the entry t (>= 0)
/// when the ray hits the box, std::nullopt otherwise. Inside-the-box
/// rays return t=0.
[[nodiscard]] inline std::optional<float>
intersect_ray_aabb(const Ray& r, const Aabb& a) noexcept
{
    float t_min = 0.0F;
    float t_max = 1.0e30F;
    const auto comp = [](const cd::math::Vec3f& v, int i) noexcept
    {
        if (i == 0) return v.x;
        if (i == 1) return v.y;
        return v.z;
    };
    for (int i = 0; i < 3; ++i)
    {
        const float ro = comp(r.origin, i);
        const float rd = comp(r.direction, i);
        const float lo = comp(a.min, i);
        const float hi = comp(a.max, i);
        if (std::abs(rd) < 1e-8F)
        {
            if (ro < lo || ro > hi) return std::nullopt;
        }
        else
        {
            float t1 = (lo - ro) / rd;
            float t2 = (hi - ro) / rd;
            if (t1 > t2) std::swap(t1, t2);
            t_min = std::max(t_min, t1);
            t_max = std::min(t_max, t2);
            if (t_min > t_max) return std::nullopt;
        }
    }
    return t_min;
}

}  // namespace cd::physics
