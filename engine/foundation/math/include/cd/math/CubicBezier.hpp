// =============================================================================
// CHROMODYNAMIC — cd/math/CubicBezier.hpp
// Phase 22.D / Wave 186 — header-only cubic Bezier curve evaluator.
//
// 4 control points (P0..P3); `at(t)` returns the point at parameter t
// in [0, 1]. `arc_length(samples)` approximates the curve length by
// summing N straight-line segments — adequate for UI motion paths and
// animation pre-bake.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::math
{

struct CubicBezier
{
    Vec3f p0 {}, p1 {}, p2 {}, p3 {};

    [[nodiscard]] Vec3f at(float t) const noexcept
    {
        if (t < 0.0F) t = 0.0F;
        if (t > 1.0F) t = 1.0F;
        const float u = 1.0F - t;
        const float w0 = u * u * u;
        const float w1 = 3.0F * u * u * t;
        const float w2 = 3.0F * u * t * t;
        const float w3 = t * t * t;
        return Vec3f {
            w0 * p0.x + w1 * p1.x + w2 * p2.x + w3 * p3.x,
            w0 * p0.y + w1 * p1.y + w2 * p2.y + w3 * p3.y,
            w0 * p0.z + w1 * p1.z + w2 * p2.z + w3 * p3.z,
        };
    }

    [[nodiscard]] float arc_length(int samples = 32) const noexcept
    {
        if (samples < 2) samples = 2;
        float len = 0.0F;
        Vec3f prev = at(0.0F);
        for (int i = 1; i <= samples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(samples);
            const Vec3f cur = at(t);
            const float dx = cur.x - prev.x;
            const float dy = cur.y - prev.y;
            const float dz = cur.z - prev.z;
            len += std::sqrt(dx * dx + dy * dy + dz * dz);
            prev = cur;
        }
        return len;
    }
};

}  // namespace cd::math
