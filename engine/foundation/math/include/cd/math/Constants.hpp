// =============================================================================
// CHROMODYNAMIC — cd/math/Constants.hpp
// ADR-017 P4 (Sprint S2.7) — scalar math constants.
//
// Conventions:
//   * Angles are radians throughout cd::math.
//   * Right-handed, Y-up world basis (matches Filament/Sokol; Vulkan clip-space
//     adjustment lives at the projection matrix, not here).
//   * Column-major matrix storage (matrix * column-vector semantics).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <limits>

namespace cd::math
{

template <class T>
inline constexpr T pi_v = T(3.14159265358979323846L);
template <class T>
inline constexpr T tau_v = T(6.28318530717958647692L);
template <class T>
inline constexpr T half_pi_v = T(1.57079632679489661923L);
template <class T>
// NOLINTNEXTLINE(modernize-use-std-numbers) — this header IS the project's
// constants surface; the T(...) long-double literal idiom is kept uniform
// across pi/tau/half_pi/inv_sqrt2, several of which std::numbers cannot
// express (tau, inv_sqrt2 differs from 1/sqrt2 by rounding). The check's
// auto-fix also mangles variable templates (phase1087 incident).
inline constexpr T inv_pi_v = T(0.31830988618379067154L);
template <class T>
// NOLINTNEXTLINE(modernize-use-std-numbers) — see inv_pi_v note above.
inline constexpr T sqrt2_v = T(1.41421356237309504880L);
template <class T>
inline constexpr T inv_sqrt2_v = T(0.70710678118654752440L);
template <class T>
inline constexpr T epsilon_v = std::numeric_limits<T>::epsilon();

inline constexpr float pi = pi_v<float>;
inline constexpr float tau = tau_v<float>;
inline constexpr float half_pi = half_pi_v<float>;
inline constexpr float inv_pi = inv_pi_v<float>;
inline constexpr float sqrt2 = sqrt2_v<float>;
inline constexpr float inv_sqrt2 = inv_sqrt2_v<float>;
inline constexpr float epsilon = epsilon_v<float>;
inline constexpr double pi_d = pi_v<double>;
inline constexpr double epsilon_d = epsilon_v<double>;

}  // namespace cd::math
