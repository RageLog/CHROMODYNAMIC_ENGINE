// =============================================================================
// CHROMODYNAMIC — cd/math/AngleUnits.hpp
// Phase 38.B / Wave 206 — degree/radian conversion + UDLs.
//
// All engine math is radians. UI / scene-file / editor input is often
// degrees. This header centralizes the conversion and exposes optional
// user-defined literals so call sites read like physics notation:
//
//   const float yaw = 45.0_deg;        // converted to radians
//   const float roll = 1.5_rad;        // identity
//
// Pure constexpr; zero runtime cost.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Constants.hpp>

namespace cd::math
{

[[nodiscard]] constexpr float to_radians(float deg) noexcept
{
    return deg * (cd::math::pi / 180.0F);
}

[[nodiscard]] constexpr float to_degrees(float rad) noexcept
{
    return rad * (180.0F / cd::math::pi);
}

namespace literals
{

[[nodiscard]] constexpr float operator""_deg(long double v) noexcept
{
    return to_radians(static_cast<float>(v));
}

[[nodiscard]] constexpr float operator""_deg(unsigned long long v) noexcept
{
    return to_radians(static_cast<float>(v));
}

[[nodiscard]] constexpr float operator""_rad(long double v) noexcept
{
    return static_cast<float>(v);
}

[[nodiscard]] constexpr float operator""_rad(unsigned long long v) noexcept
{
    return static_cast<float>(v);
}

}  // namespace literals

}  // namespace cd::math
