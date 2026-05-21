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

// --- Convenience aliases ---------------------------------------------------

using Mat3f = Mat<float, 3>;
using Mat4f = Mat<float, 4>;
using Mat3d = Mat<double, 3>;
using Mat4d = Mat<double, 4>;

}  // namespace cd::math
