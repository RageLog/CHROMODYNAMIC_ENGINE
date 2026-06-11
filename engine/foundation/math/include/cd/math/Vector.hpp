// =============================================================================
// CHROMODYNAMIC — cd/math/Vector.hpp
// ADR-017 P4 (Sprint S2.7) — fixed-size vector primitives.
//
// Vec2<T> / Vec3<T> / Vec4<T> with arithmetic, dot/cross, length/normalize,
// component-wise min/max, and element access. Scalar storage only — SIMD
// specialisation can drop in later behind the same value semantics.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Functions.hpp>

#include <cmath>
#include <cstddef>
#include <type_traits>

namespace cd::math
{

template <class T, std::size_t N>
struct Vec;  // primary

template <class T>
struct Vec<T, 2>
{
    using value_type = T;
    T x {}, y {};

    constexpr Vec() noexcept = default;

    constexpr Vec(T xx, T yy) noexcept
        : x { xx }
        , y { yy }
    {
    }

    constexpr explicit Vec(T s) noexcept
        : x { s }
        , y { s }
    {
    }

    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept
    {
        return i == 0 ? x : y;
    }

    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept
    {
        return i == 0 ? x : y;
    }

    friend constexpr bool operator==(const Vec&, const Vec&) noexcept = default;
};

template <class T>
struct Vec<T, 3>
{
    using value_type = T;
    T x {}, y {}, z {};

    constexpr Vec() noexcept = default;

    constexpr Vec(T xx, T yy, T zz) noexcept
        : x { xx }
        , y { yy }
        , z { zz }
    {
    }

    constexpr explicit Vec(T s) noexcept
        : x { s }
        , y { s }
        , z { s }
    {
    }

    constexpr Vec(Vec<T, 2> xy, T zz) noexcept
        : x { xy.x }
        , y { xy.y }
        , z { zz }
    {
    }

    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept
    {
        if (i == 0) return x;
        if (i == 1) return y;
        return z;
    }

    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept
    {
        if (i == 0) return x;
        if (i == 1) return y;
        return z;
    }

    [[nodiscard]] constexpr Vec<T, 2> xy() const noexcept
    {
        return { x, y };
    }

    friend constexpr bool operator==(const Vec&, const Vec&) noexcept = default;
};

template <class T>
struct Vec<T, 4>
{
    using value_type = T;
    T x {}, y {}, z {}, w {};

    constexpr Vec() noexcept = default;

    constexpr Vec(T xx, T yy, T zz, T ww) noexcept
        : x { xx }
        , y { yy }
        , z { zz }
        , w { ww }
    {
    }

    constexpr explicit Vec(T s) noexcept
        : x { s }
        , y { s }
        , z { s }
        , w { s }
    {
    }

    constexpr Vec(Vec<T, 3> xyz, T ww) noexcept
        : x { xyz.x }
        , y { xyz.y }
        , z { xyz.z }
        , w { ww }
    {
    }

    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept
    {
        if (i == 0) return x;
        if (i == 1) return y;
        if (i == 2) return z;
        return w;
    }

    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept
    {
        if (i == 0) return x;
        if (i == 1) return y;
        if (i == 2) return z;
        return w;
    }

    [[nodiscard]] constexpr Vec<T, 3> xyz() const noexcept
    {
        return { x, y, z };
    }

    friend constexpr bool operator==(const Vec&, const Vec&) noexcept = default;
};

// --- Arithmetic (component-wise) -------------------------------------------

#define CD_MATH_VEC_BINOP(op)                                                               \
    template <class T, std::size_t N>                                                       \
    [[nodiscard]] constexpr Vec<T, N> operator op(Vec<T, N> a, const Vec<T, N>& b) noexcept \
    {                                                                                       \
        for (std::size_t i = 0; i < N; ++i)                                                 \
            a[i] = a[i] op b[i];                                                            \
        return a;                                                                           \
    }                                                                                       \
    template <class T, std::size_t N>                                                       \
    [[nodiscard]] constexpr Vec<T, N> operator op(Vec<T, N> a, T s) noexcept                \
    {                                                                                       \
        for (std::size_t i = 0; i < N; ++i)                                                 \
            a[i] = a[i] op s;                                                               \
        return a;                                                                           \
    }                                                                                       \
    template <class T, std::size_t N>                                                       \
    [[nodiscard]] constexpr Vec<T, N> operator op(T s, Vec<T, N> a) noexcept                \
    {                                                                                       \
        for (std::size_t i = 0; i < N; ++i)                                                 \
            a[i] = s op a[i];                                                               \
        return a;                                                                           \
    }

CD_MATH_VEC_BINOP(+)
CD_MATH_VEC_BINOP(-)
CD_MATH_VEC_BINOP(*)
CD_MATH_VEC_BINOP(/)

#undef CD_MATH_VEC_BINOP

template <class T, std::size_t N>
[[nodiscard]] constexpr Vec<T, N> operator-(Vec<T, N> v) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
        v[i] = -v[i];
    return v;
}

template <class T, std::size_t N>
constexpr Vec<T, N>& operator+=(Vec<T, N>& a, const Vec<T, N>& b) noexcept
{
    a = a + b;
    return a;
}

template <class T, std::size_t N>
constexpr Vec<T, N>& operator-=(Vec<T, N>& a, const Vec<T, N>& b) noexcept
{
    a = a - b;
    return a;
}

template <class T, std::size_t N>
constexpr Vec<T, N>& operator*=(Vec<T, N>& a, T s) noexcept
{
    a = a * s;
    return a;
}

template <class T, std::size_t N>
constexpr Vec<T, N>& operator/=(Vec<T, N>& a, T s) noexcept
{
    a = a / s;
    return a;
}

// --- Free functions --------------------------------------------------------

template <class T, std::size_t N>
[[nodiscard]] constexpr T dot(const Vec<T, N>& a, const Vec<T, N>& b) noexcept
{
    T sum {};
    for (std::size_t i = 0; i < N; ++i)
        sum = sum + a[i] * b[i];
    return sum;
}

template <class T>
[[nodiscard]] constexpr Vec<T, 3> cross(const Vec<T, 3>& a, const Vec<T, 3>& b) noexcept
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

template <class T, std::size_t N>
[[nodiscard]] constexpr T length_squared(const Vec<T, N>& v) noexcept
{
    return dot(v, v);
}

template <class T, std::size_t N>
[[nodiscard]] T length(const Vec<T, N>& v) noexcept
{
    return std::sqrt(length_squared(v));
}

template <class T, std::size_t N>
[[nodiscard]] Vec<T, N> normalize(const Vec<T, N>& v) noexcept
{
    const T len = length(v);
    return len > epsilon_v<T> ? v / len : v;
}

template <class T, std::size_t N>
[[nodiscard]] constexpr Vec<T, N> lerp(const Vec<T, N>& a, const Vec<T, N>& b, T t) noexcept
{
    return a + (b - a) * t;
}

template <class T, std::size_t N>
[[nodiscard]] constexpr Vec<T, N> min(const Vec<T, N>& a, const Vec<T, N>& b) noexcept
{
    Vec<T, N> r;
    for (std::size_t i = 0; i < N; ++i)
        r[i] = a[i] < b[i] ? a[i] : b[i];
    return r;
}

template <class T, std::size_t N>
[[nodiscard]] constexpr Vec<T, N> max(const Vec<T, N>& a, const Vec<T, N>& b) noexcept
{
    Vec<T, N> r;
    for (std::size_t i = 0; i < N; ++i)
        r[i] = a[i] > b[i] ? a[i] : b[i];
    return r;
}

template <class T, std::size_t N>
[[nodiscard]] bool approx_equal(const Vec<T, N>& a, const Vec<T, N>& b, T tol = epsilon_v<T> * T { 16 }) noexcept
{
    static_assert(std::is_floating_point_v<T>);
    for (std::size_t i = 0; i < N; ++i)
    {
        if (!approx_equal(a[i], b[i], tol))
            return false;
    }
    return true;
}

// --- Convenience aliases ---------------------------------------------------

using Vec2f = Vec<float, 2>;
using Vec3f = Vec<float, 3>;
using Vec4f = Vec<float, 4>;
using Vec2d = Vec<double, 2>;
using Vec3d = Vec<double, 3>;
using Vec4d = Vec<double, 4>;
using Vec2i = Vec<int, 2>;
using Vec3i = Vec<int, 3>;
using Vec4i = Vec<int, 4>;

}  // namespace cd::math
