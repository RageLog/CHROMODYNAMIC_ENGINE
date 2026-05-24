// =============================================================================
// CHROMODYNAMIC — cd/math/Range.hpp
// Phase 46.A / Wave 214 — half-open numeric range [min, max].
//
// Pair of (min, max) numerics with the helpers every call site
// re-implements: contains, clamp, lerp (inverse + forward), expand,
// length. Template on T (float / double / int / uint32_t).
//
// Convention: `min` <= `max`. Constructor does NOT enforce — caller
// guarantees. `normalize(r)` swaps if reversed.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <algorithm>

namespace cd::math
{

template <class T>
struct Range
{
    T min {};
    T max {};

    friend constexpr bool operator==(Range, Range) noexcept = default;
};

template <class T>
[[nodiscard]] constexpr T length(const Range<T>& r) noexcept
{
    return r.max - r.min;
}

template <class T>
[[nodiscard]] constexpr bool contains(const Range<T>& r, T v) noexcept
{
    return v >= r.min && v <= r.max;
}

template <class T>
[[nodiscard]] constexpr T clamp(const Range<T>& r, T v) noexcept
{
    return std::clamp(v, r.min, r.max);
}

template <class T>
[[nodiscard]] constexpr T lerp(const Range<T>& r, T u) noexcept
{
    return r.min + (r.max - r.min) * u;
}

/// Returns the normalized coordinate of `v` inside `r` (0 at min, 1 at max).
/// Caller must ensure `length(r) != 0`.
template <class T>
[[nodiscard]] constexpr T inverse_lerp(const Range<T>& r, T v) noexcept
{
    return (v - r.min) / (r.max - r.min);
}

template <class T>
[[nodiscard]] constexpr Range<T> expand(const Range<T>& r, T amount) noexcept
{
    return Range<T> { r.min - amount, r.max + amount };
}

template <class T>
[[nodiscard]] constexpr Range<T> normalize(Range<T> r) noexcept
{
    if (r.min > r.max) std::swap(r.min, r.max);
    return r;
}

}  // namespace cd::math
