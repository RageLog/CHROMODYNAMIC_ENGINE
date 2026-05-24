// =============================================================================
// CHROMODYNAMIC — cd/math/Aabb2.hpp
// Phase 71.B / Wave 239 — 2D axis-aligned bounding box.
//
// Float-based 2D AABB for screen-space UI and 2D scenes. Mirrors
// `cd::physics::Aabb` (Phase 20) for the 3D case.
//
//   * `contains(box, point)` / `overlaps(a, b)` / `merge(a, b)`.
//   * `expand(box, amount)` — symmetric pad.
//   * `area(box)` — width * height (negative if reversed).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>

namespace cd::math
{

struct Aabb2
{
    float min_x { 0.0F };
    float min_y { 0.0F };
    float max_x { 0.0F };
    float max_y { 0.0F };
};

[[nodiscard]] inline bool contains(const Aabb2& b, float x, float y) noexcept
{
    return x >= b.min_x && x <= b.max_x && y >= b.min_y && y <= b.max_y;
}

[[nodiscard]] inline bool overlaps(const Aabb2& a, const Aabb2& b) noexcept
{
    return a.min_x <= b.max_x && a.max_x >= b.min_x
        && a.min_y <= b.max_y && a.max_y >= b.min_y;
}

[[nodiscard]] inline Aabb2 merge(const Aabb2& a, const Aabb2& b) noexcept
{
    return Aabb2 {
        std::min(a.min_x, b.min_x),
        std::min(a.min_y, b.min_y),
        std::max(a.max_x, b.max_x),
        std::max(a.max_y, b.max_y),
    };
}

[[nodiscard]] inline Aabb2 expand(const Aabb2& b, float amount) noexcept
{
    return Aabb2 {
        b.min_x - amount, b.min_y - amount,
        b.max_x + amount, b.max_y + amount,
    };
}

[[nodiscard]] inline float area(const Aabb2& b) noexcept
{
    return (b.max_x - b.min_x) * (b.max_y - b.min_y);
}

}  // namespace cd::math
