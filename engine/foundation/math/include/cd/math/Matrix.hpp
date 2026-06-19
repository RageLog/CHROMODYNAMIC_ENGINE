// =============================================================================
// CHROMODYNAMIC — cd/math/Matrix.hpp
// ADR-017 P4 (Sprint S2.7) — column-major fixed-size matrices.
//
// Storage layout: column-major. `m[c][r]` accesses element at column `c`,
// row `r`. This matches GLSL / SPIR-V / Metal / Filament conventions. The
// convention `M * v` treats `v` as a column vector — common in graphics math.
//
// Currently implements Mat<T,3> (3x3) and Mat<T,4> (4x4) which are the
// dominant cases for rendering transforms. Generic Mat<T,R,C> can come later.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <cstddef>
#include <optional>

namespace cd::math
{

template <class T, std::size_t N>
struct Mat;  // primary

template <class T>
struct Mat<T, 3>
{
    using value_type = T;
    Vec<T, 3> cols[3] {};

    constexpr Mat() noexcept = default;

    constexpr Mat(Vec<T, 3> c0, Vec<T, 3> c1, Vec<T, 3> c2) noexcept
        : cols { c0, c1, c2 }
    {
    }

    [[nodiscard]] static constexpr Mat identity() noexcept
    {
        return Mat {
            { T { 1 }, T { 0 }, T { 0 } },
            { T { 0 }, T { 1 }, T { 0 } },
            { T { 0 }, T { 0 }, T { 1 } }
        };
    }

    [[nodiscard]] constexpr Vec<T, 3>& operator[](std::size_t c) noexcept
    {
        return cols[c];
    }

    [[nodiscard]] constexpr const Vec<T, 3>& operator[](std::size_t c) const noexcept
    {
        return cols[c];
    }

    friend constexpr bool operator==(const Mat&, const Mat&) noexcept = default;
};

template <class T>
struct Mat<T, 4>
{
    using value_type = T;
    Vec<T, 4> cols[4] {};

    constexpr Mat() noexcept = default;

    constexpr Mat(Vec<T, 4> c0, Vec<T, 4> c1, Vec<T, 4> c2, Vec<T, 4> c3) noexcept
        : cols { c0, c1, c2, c3 }
    {
    }

    [[nodiscard]] static constexpr Mat identity() noexcept
    {
        return Mat {
            { T { 1 }, T { 0 }, T { 0 }, T { 0 } },
            { T { 0 }, T { 1 }, T { 0 }, T { 0 } },
            { T { 0 }, T { 0 }, T { 1 }, T { 0 } },
            { T { 0 }, T { 0 }, T { 0 }, T { 1 } }
        };
    }

    [[nodiscard]] constexpr Vec<T, 4>& operator[](std::size_t c) noexcept
    {
        return cols[c];
    }

    [[nodiscard]] constexpr const Vec<T, 4>& operator[](std::size_t c) const noexcept
    {
        return cols[c];
    }

    friend constexpr bool operator==(const Mat&, const Mat&) noexcept = default;
};

// --- Free functions --------------------------------------------------------

template <class T, std::size_t N>
[[nodiscard]] constexpr Mat<T, N> transpose(const Mat<T, N>& m) noexcept
{
    Mat<T, N> r;
    for (std::size_t c = 0; c < N; ++c)
    {
        for (std::size_t row = 0; row < N; ++row)
        {
            r[c][row] = m[row][c];
        }
    }
    return r;
}

template <class T, std::size_t N>
[[nodiscard]] constexpr Mat<T, N> operator*(const Mat<T, N>& a, const Mat<T, N>& b) noexcept
{
    Mat<T, N> r;
    for (std::size_t c = 0; c < N; ++c)
    {
        for (std::size_t row = 0; row < N; ++row)
        {
            T sum {};
            for (std::size_t k = 0; k < N; ++k)
            {
                sum = sum + a[k][row] * b[c][k];
            }
            r[c][row] = sum;
        }
    }
    return r;
}

/// Mat * Vec — treats `v` as a column vector.
template <class T, std::size_t N>
[[nodiscard]] constexpr Vec<T, N> operator*(const Mat<T, N>& m, const Vec<T, N>& v) noexcept
{
    Vec<T, N> r {};
    for (std::size_t row = 0; row < N; ++row)
    {
        T sum {};
        for (std::size_t c = 0; c < N; ++c)
        {
            sum = sum + m[c][row] * v[c];
        }
        r[row] = sum;
    }
    return r;
}

// --- Affine builders (Mat4) -----------------------------------------------

template <class T>
[[nodiscard]] constexpr Mat<T, 4> translation(const Vec<T, 3>& t) noexcept
{
    auto m = Mat<T, 4>::identity();
    m[3] = Vec<T, 4> { t.x, t.y, t.z, T { 1 } };
    return m;
}

template <class T>
[[nodiscard]] constexpr Mat<T, 4> scaling(const Vec<T, 3>& s) noexcept
{
    Mat<T, 4> m {};
    m[0][0] = s.x;
    m[1][1] = s.y;
    m[2][2] = s.z;
    m[3][3] = T { 1 };
    return m;
}

// --- General 4x4 inverse ----------------------------------------------------
//
// Classic adjugate / determinant method — closed form, no LU/Gauss. Cost
// 16 cofactors + 1 determinant; fine for camera matrices (called O(1)
// per frame, not per fragment). Returns the identity if the matrix is
// numerically singular (|det| < ε); callers wanting strict error
// reporting should use `try_inverse()` below.
template <class T>
[[nodiscard]] constexpr Mat<T, 4> inverse(const Mat<T, 4>& m) noexcept
{
    // Build a flat 16-element view in column-major so the cofactor
    // formulas mirror the canonical references (column[c][row]).
    const T a00 = m[0][0];
    const T a01 = m[1][0];
    const T a02 = m[2][0];
    const T a03 = m[3][0];
    const T a10 = m[0][1];
    const T a11 = m[1][1];
    const T a12 = m[2][1];
    const T a13 = m[3][1];
    const T a20 = m[0][2];
    const T a21 = m[1][2];
    const T a22 = m[2][2];
    const T a23 = m[3][2];
    const T a30 = m[0][3];
    const T a31 = m[1][3];
    const T a32 = m[2][3];
    const T a33 = m[3][3];

    const T b00 = a00 * a11 - a01 * a10;
    const T b01 = a00 * a12 - a02 * a10;
    const T b02 = a00 * a13 - a03 * a10;
    const T b03 = a01 * a12 - a02 * a11;
    const T b04 = a01 * a13 - a03 * a11;
    const T b05 = a02 * a13 - a03 * a12;
    const T b06 = a20 * a31 - a21 * a30;
    const T b07 = a20 * a32 - a22 * a30;
    const T b08 = a20 * a33 - a23 * a30;
    const T b09 = a21 * a32 - a22 * a31;
    const T b10 = a21 * a33 - a23 * a31;
    const T b11 = a22 * a33 - a23 * a32;

    const T det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    // Singular → return identity. Predictable fallback for shaders that
    // do not check the result; callers needing strict semantics should
    // use the Result-returning variant.
    constexpr T kEps = T(1e-12);
    if (det < kEps && det > -kEps)
        return Mat<T, 4>::identity();
    const T inv_det = T(1) / det;

    Mat<T, 4> out {};
    out[0][0] = (a11 * b11 - a12 * b10 + a13 * b09) * inv_det;
    out[0][1] = (a12 * b08 - a10 * b11 - a13 * b07) * inv_det;
    out[0][2] = (a10 * b10 - a11 * b08 + a13 * b06) * inv_det;
    out[0][3] = (a11 * b07 - a10 * b09 - a12 * b06) * inv_det;

    out[1][0] = (a02 * b10 - a01 * b11 - a03 * b09) * inv_det;
    out[1][1] = (a00 * b11 - a02 * b08 + a03 * b07) * inv_det;
    out[1][2] = (a01 * b08 - a00 * b10 - a03 * b06) * inv_det;
    out[1][3] = (a00 * b09 - a01 * b07 + a02 * b06) * inv_det;

    out[2][0] = (a31 * b05 - a32 * b04 + a33 * b03) * inv_det;
    out[2][1] = (a32 * b02 - a30 * b05 - a33 * b01) * inv_det;
    out[2][2] = (a30 * b04 - a31 * b02 + a33 * b00) * inv_det;
    out[2][3] = (a31 * b01 - a30 * b03 - a32 * b00) * inv_det;

    out[3][0] = (a22 * b04 - a21 * b05 - a23 * b03) * inv_det;
    out[3][1] = (a20 * b05 - a22 * b02 + a23 * b01) * inv_det;
    out[3][2] = (a21 * b02 - a20 * b04 - a23 * b00) * inv_det;
    out[3][3] = (a20 * b03 - a21 * b01 + a22 * b00) * inv_det;
    return out;
}

// --- Strict 4x4 inverse -----------------------------------------------------
//
// Same adjugate / determinant arithmetic as `inverse()` above, but reports
// singularity explicitly: returns `std::nullopt` instead of silently folding
// to the identity. Use this when a caller must distinguish "M was singular"
// from "M happened to be the identity" (e.g. asset validation, unit tests).
// The non-singular numerical result is byte-identical to `inverse()`.
template <class T>
[[nodiscard]] constexpr std::optional<Mat<T, 4>> try_inverse(const Mat<T, 4>& m) noexcept
{
    const T a00 = m[0][0];
    const T a01 = m[1][0];
    const T a02 = m[2][0];
    const T a03 = m[3][0];
    const T a10 = m[0][1];
    const T a11 = m[1][1];
    const T a12 = m[2][1];
    const T a13 = m[3][1];
    const T a20 = m[0][2];
    const T a21 = m[1][2];
    const T a22 = m[2][2];
    const T a23 = m[3][2];
    const T a30 = m[0][3];
    const T a31 = m[1][3];
    const T a32 = m[2][3];
    const T a33 = m[3][3];

    const T b00 = a00 * a11 - a01 * a10;
    const T b01 = a00 * a12 - a02 * a10;
    const T b02 = a00 * a13 - a03 * a10;
    const T b03 = a01 * a12 - a02 * a11;
    const T b04 = a01 * a13 - a03 * a11;
    const T b05 = a02 * a13 - a03 * a12;
    const T b06 = a20 * a31 - a21 * a30;
    const T b07 = a20 * a32 - a22 * a30;
    const T b08 = a20 * a33 - a23 * a30;
    const T b09 = a21 * a32 - a22 * a31;
    const T b10 = a21 * a33 - a23 * a31;
    const T b11 = a22 * a33 - a23 * a32;

    const T det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    constexpr T kEps = T(1e-12);
    if (det < kEps && det > -kEps)
        return std::nullopt;
    const T inv_det = T(1) / det;

    Mat<T, 4> out {};
    out[0][0] = (a11 * b11 - a12 * b10 + a13 * b09) * inv_det;
    out[0][1] = (a12 * b08 - a10 * b11 - a13 * b07) * inv_det;
    out[0][2] = (a10 * b10 - a11 * b08 + a13 * b06) * inv_det;
    out[0][3] = (a11 * b07 - a10 * b09 - a12 * b06) * inv_det;

    out[1][0] = (a02 * b10 - a01 * b11 - a03 * b09) * inv_det;
    out[1][1] = (a00 * b11 - a02 * b08 + a03 * b07) * inv_det;
    out[1][2] = (a01 * b08 - a00 * b10 - a03 * b06) * inv_det;
    out[1][3] = (a00 * b09 - a01 * b07 + a02 * b06) * inv_det;

    out[2][0] = (a31 * b05 - a32 * b04 + a33 * b03) * inv_det;
    out[2][1] = (a32 * b02 - a30 * b05 - a33 * b01) * inv_det;
    out[2][2] = (a30 * b04 - a31 * b02 + a33 * b00) * inv_det;
    out[2][3] = (a31 * b01 - a30 * b03 - a32 * b00) * inv_det;

    out[3][0] = (a22 * b04 - a21 * b05 - a23 * b03) * inv_det;
    out[3][1] = (a20 * b05 - a22 * b02 + a23 * b01) * inv_det;
    out[3][2] = (a21 * b02 - a20 * b04 - a23 * b00) * inv_det;
    out[3][3] = (a20 * b03 - a21 * b01 + a22 * b00) * inv_det;
    return out;
}

// --- Convenience aliases ---------------------------------------------------

using Mat3f = Mat<float, 3>;
using Mat4f = Mat<float, 4>;
using Mat3d = Mat<double, 3>;
using Mat4d = Mat<double, 4>;

}  // namespace cd::math
