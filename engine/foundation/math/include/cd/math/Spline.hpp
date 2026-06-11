// =============================================================================
// CHROMODYNAMIC — cd/math/Spline.hpp
// Phase 26.B / Wave 194 — Catmull-Rom spline (header-only).
//
// Given a list of N control points (N >= 4), at(t in [0, 1]) returns
// the point on the curve. Catmull-Rom passes through every interior
// control point — useful for path-following animation that should
// "hit the waypoints" without manual tangent tuning.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace cd::math
{

class CatmullRomSpline
{
public:
    explicit CatmullRomSpline(std::vector<Vec3f> control) noexcept
        : control_ { std::move(control) }
    {
    }

    [[nodiscard]] std::size_t size() const noexcept { return control_.size(); }

    /// Sample at global parameter t in [0, 1] across the whole curve.
    /// Returns control_[0] when N < 4 or when control_ is empty.
    [[nodiscard]] Vec3f at(float t) const noexcept
    {
        const std::size_t n = control_.size();
        if (n == 0) return Vec3f {};
        if (n < 4) return control_[0];
        t = std::max(t, 0.0F);
        t = std::min(t, 1.0F);
        const std::size_t segments = n - 3;
        const float scaled = t * static_cast<float>(segments);
        std::size_t seg = static_cast<std::size_t>(scaled);
        if (seg >= segments) seg = segments - 1;
        const float local = scaled - static_cast<float>(seg);
        return at_segment_(seg, local);
    }

private:
    [[nodiscard]] Vec3f at_segment_(std::size_t seg, float u) const noexcept
    {
        const Vec3f& p0 = control_[seg + 0];
        const Vec3f& p1 = control_[seg + 1];
        const Vec3f& p2 = control_[seg + 2];
        const Vec3f& p3 = control_[seg + 3];
        const float u2 = u * u;
        const float u3 = u2 * u;
        const float a = -0.5F * u3 +       u2 - 0.5F * u;
        const float b =  1.5F * u3 - 2.5F * u2          + 1.0F;
        const float c = -1.5F * u3 + 2.0F * u2 + 0.5F * u;
        const float d =  0.5F * u3 - 0.5F * u2;
        return Vec3f {
            a * p0.x + b * p1.x + c * p2.x + d * p3.x,
            a * p0.y + b * p1.y + c * p2.y + d * p3.y,
            a * p0.z + b * p1.z + c * p2.z + d * p3.z,
        };
    }

    std::vector<Vec3f> control_;
};

}  // namespace cd::math
