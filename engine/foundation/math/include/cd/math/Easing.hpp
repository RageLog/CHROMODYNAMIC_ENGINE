// =============================================================================
// CHROMODYNAMIC — cd/math/Easing.hpp
// Phase 21.E / Wave 184 — standard easing curves.
//
// Every UI / VFX / camera-blend layer needs the same handful of
// scalar interpolation curves. Centralizing them as header-only
// constexpr functions avoids the "each library reimplements
// smoothstep" pattern.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

namespace cd::math
{

[[nodiscard]] constexpr float clamp01(float t) noexcept
{
    return t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
}

[[nodiscard]] constexpr float linear(float t) noexcept
{
    return clamp01(t);
}

[[nodiscard]] constexpr float smoothstep(float t) noexcept
{
    t = clamp01(t);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] constexpr float ease_in_quad(float t) noexcept
{
    t = clamp01(t);
    return t * t;
}

[[nodiscard]] constexpr float ease_out_quad(float t) noexcept
{
    t = clamp01(t);
    return 1.0F - (1.0F - t) * (1.0F - t);
}

[[nodiscard]] constexpr float ease_in_out_cubic(float t) noexcept
{
    t = clamp01(t);
    if (t < 0.5F)
        return 4.0F * t * t * t;
    const float u = -2.0F * t + 2.0F;
    return 1.0F - 0.5F * u * u * u;
}

}  // namespace cd::math
