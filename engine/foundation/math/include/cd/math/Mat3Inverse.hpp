// =============================================================================
// CHROMODYNAMIC — cd/math/Mat3Inverse.hpp
// Phase 32.A / Wave 200 — Mat3 inverse + normal-matrix builder.
//
// The 4×4 inverse already exists in Matrix.hpp (adjugate / det). The
// 3×3 inverse is a separate primitive because it shows up in the
// per-vertex normal-matrix path: shaders multiply object-space normals
// by `transpose(inverse(M3))` where M3 is the top-left 3×3 of the
// model matrix. Reusing the 4×4 path for that means promoting normals
// through a 4-component pipeline for no payoff.
//
//   inverse_3(M) — closed-form cofactor formula. Returns identity if
//                  |det| < ε (predictable fallback for shaders that
//                  don't check the result).
//   normal_matrix(M4) — top-left 3×3 of M4, inverted, transposed.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Matrix.hpp>

namespace cd::math
{

template <class T>
[[nodiscard]] constexpr Mat<T, 3> inverse_3(const Mat<T, 3>& m) noexcept
{
    // column-major access: m[col][row]
    const T a00 = m[0][0], a01 = m[1][0], a02 = m[2][0];
    const T a10 = m[0][1], a11 = m[1][1], a12 = m[2][1];
    const T a20 = m[0][2], a21 = m[1][2], a22 = m[2][2];

    const T c00 = a11 * a22 - a12 * a21;
    const T c01 = a12 * a20 - a10 * a22;
    const T c02 = a10 * a21 - a11 * a20;
    const T det = a00 * c00 + a01 * c01 + a02 * c02;
    constexpr T kEps = T(1e-12);
    if (det < kEps && det > -kEps)
        return Mat<T, 3>::identity();
    const T inv = T(1) / det;

    Mat<T, 3> out {};
    out[0][0] = c00 * inv;
    out[0][1] = c01 * inv;
    out[0][2] = c02 * inv;
    out[1][0] = (a02 * a21 - a01 * a22) * inv;
    out[1][1] = (a00 * a22 - a02 * a20) * inv;
    out[1][2] = (a01 * a20 - a00 * a21) * inv;
    out[2][0] = (a01 * a12 - a02 * a11) * inv;
    out[2][1] = (a02 * a10 - a00 * a12) * inv;
    out[2][2] = (a00 * a11 - a01 * a10) * inv;
    return out;
}

/// Extract the top-left 3×3 of a 4×4 matrix.
template <class T>
[[nodiscard]] constexpr Mat<T, 3> upper_3x3(const Mat<T, 4>& m) noexcept
{
    return Mat<T, 3> {
        Vec<T, 3> { m[0][0], m[0][1], m[0][2] },
        Vec<T, 3> { m[1][0], m[1][1], m[1][2] },
        Vec<T, 3> { m[2][0], m[2][1], m[2][2] },
    };
}

/// Standard normal-matrix: `transpose(inverse(upper_3x3(M)))`.
template <class T>
[[nodiscard]] constexpr Mat<T, 3> normal_matrix(const Mat<T, 4>& m) noexcept
{
    return transpose(inverse_3(upper_3x3(m)));
}

}  // namespace cd::math
