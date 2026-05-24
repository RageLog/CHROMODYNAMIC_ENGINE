// =============================================================================
// CHROMODYNAMIC — cd/math/GammaSpace.hpp
// Phase 90.B / Wave 258 — Vec3 sRGB ↔ linear helpers + approximate fast forms.
//
// `cd::math::srgb_to_linear` and `linear_to_srgb` (per-channel scalar)
// already exist in Color.hpp (Phase 23). This header adds Vec3
// overloads and a cheaper `*_approx` family (γ=2.2 simple pow) for
// editor preview where IEC 61966 accuracy isn't required.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Color.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::math
{

[[nodiscard]] inline Vec3f srgb_to_linear(const Vec3f& c) noexcept
{
    return Vec3f { srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z) };
}

[[nodiscard]] inline Vec3f linear_to_srgb(const Vec3f& c) noexcept
{
    return Vec3f { linear_to_srgb(c.x), linear_to_srgb(c.y), linear_to_srgb(c.z) };
}

/// Fast γ=2.2 approximation. Off by ~1-2% in midrange vs IEC 61966.
[[nodiscard]] inline float srgb_to_linear_approx(float c) noexcept
{
    return std::pow(c, 2.2F);
}

[[nodiscard]] inline float linear_to_srgb_approx(float c) noexcept
{
    return std::pow(c, 1.0F / 2.2F);
}

}  // namespace cd::math
