// =============================================================================
// CHROMODYNAMIC — cd/math/Onb.hpp
// Orthonormal Basis (ONB) construction from a single unit normal.
//
// Implements the branchless algorithm from:
//   Duff et al. 2017 "Building an Orthonormal Basis, Revisited"
//   Journal of Computer Graphics Techniques (JCGT) Vol. 6, No. 1, pp. 1–8.
//   https://jcgt.org/published/0006/01/01/
//
// Supersedes Frisvad 2012 with ~4 orders of magnitude better numerical
// precision near n.z = –1 (south pole) at identical arithmetic cost.
// The Frisvad formula suffers catastrophic cancellation in (1 + n.z) → 0;
// Duff replaces the denominator with (s + n.z) where s = copysign(1, n.z),
// keeping the denominator away from zero at both poles.
//
// GLSL equivalent is provided in the file comment below for copy-paste into
// shader sources. See §4.1, p. 5–6 of the paper.
//
//   void duff_onb(vec3 n, out vec3 b1, out vec3 b2) {
//     float s = (n.z >= 0.0) ? 1.0 : -1.0;  // copysign
//     float a = -1.0 / (s + n.z);
//     float b = n.x * n.y * a;
//     b1 = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
//     b2 = vec3(b, s + n.y * n.y * a, -n.y);
//   }
// =============================================================================
#pragma once

#include <cd/math/Vector.hpp>

#include <cmath>
#include <utility>

namespace cd::math
{

/// Build an orthonormal tangent frame from a unit normal using the algorithm
/// of Duff et al. 2017 (JCGT 6:1).
///
/// @param n  Unit normal (caller is responsible for normalisation).
/// @return   {b1, b2} — two unit vectors orthogonal to n and to each other,
///           forming a right-handed frame (b1, b2, n).
///
/// Precision: RMS orthogonality error ≈ 1e-7 everywhere, including the
/// problematic south-pole region (n.z ≈ –1) where Frisvad 2012 reaches 1e-3.
///
/// Reference: Duff et al. 2017 "Building an Orthonormal Basis, Revisited"
///            JCGT Vol. 6, No. 1.  https://jcgt.org/published/0006/01/01/
[[nodiscard]] inline auto duff_branchless_onb(Vec3f n) noexcept
    -> std::pair<Vec3f, Vec3f>
{
    // s = copysign(1, n.z): +1 in the upper hemisphere, –1 in the lower.
    // a = –1 / (s + n.z): avoids cancellation present in the Frisvad
    //     denominator (1 + n.z) near the south pole.
    const float s = std::copysign(1.0F, n.z);
    const float a = -1.0F / (s + n.z);
    const float b = n.x * n.y * a;
    const Vec3f b1 { 1.0F + s * n.x * n.x * a,  s * b,        -s * n.x };
    const Vec3f b2 { b,                            s + n.y * n.y * a, -n.y };
    return { b1, b2 };
}

/// @deprecated  Use duff_branchless_onb — it is numerically superior at
///              identical cost. Frisvad 2012 loses ~4 decimal digits of
///              precision near n.z = –1 due to catastrophic cancellation.
///
/// Kept as a thin wrapper so any caller compiled against the old name still
/// links without changes.  Will be removed in a future cleanup pass.
[[nodiscard]] [[deprecated("Use duff_branchless_onb (Duff 2017) for "
                           "numerically robust ONB; Frisvad 2012 is "
                           "inaccurate near n.z = -1.")]]
inline auto frisvad_branchless_onb(Vec3f n) noexcept
    -> std::pair<Vec3f, Vec3f>
{
    return duff_branchless_onb(n);
}

}  // namespace cd::math
