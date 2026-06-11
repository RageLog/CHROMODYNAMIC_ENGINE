// =============================================================================
// CHROMODYNAMIC — cd/math/Vec2Ops.hpp
// Phase 28.A / Wave 197 — Vec2 helper functions.
//
// Vec2 (2-component) ops parallel to the Vec3 set in Vector.hpp. Used
// by 2D UI math, screen-space picking, and texture-coord operations.
// =============================================================================
#pragma once

#include <algorithm>
#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::math
{

[[nodiscard]] inline float length_v2(const Vec2f& v) noexcept
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

[[nodiscard]] inline float length_sq_v2(const Vec2f& v) noexcept
{
    return v.x * v.x + v.y * v.y;
}

[[nodiscard]] inline Vec2f normalize_v2(const Vec2f& v) noexcept
{
    const float len = length_v2(v);
    if (len <= 0.0F) return Vec2f { 0, 0 };
    return Vec2f { v.x / len, v.y / len };
}

[[nodiscard]] inline Vec2f lerp_v2(const Vec2f& a, const Vec2f& b, float t) noexcept
{
    t = std::max(t, 0.0F);
    t = std::min(t, 1.0F);
    return Vec2f { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
}

[[nodiscard]] inline float dot_v2(const Vec2f& a, const Vec2f& b) noexcept
{
    return a.x * b.x + a.y * b.y;
}

}  // namespace cd::math
