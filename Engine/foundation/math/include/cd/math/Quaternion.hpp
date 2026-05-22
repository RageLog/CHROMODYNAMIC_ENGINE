// =============================================================================
// CHROMODYNAMIC — cd/math/Quaternion.hpp
// ADR-017 P4 (Sprint S2.7) — unit quaternion for 3D rotations.
//
// Storage: {x, y, z, w} where (x,y,z) is the imaginary vector part and `w`
// is the real (scalar) part. Identity is {0, 0, 0, 1}. Right-handed; rotation
// q applied to vector v as: v' = q * v * conjugate(q).
//
// All trig-using constructors take radians.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::math
{

template <class T>
struct Quat
{
    using value_type = T;
    T x { T { 0 } }, y { T { 0 } }, z { T { 0 } }, w { T { 1 } };

    constexpr Quat() noexcept = default;

    constexpr Quat(T xx, T yy, T zz, T ww) noexcept
        : x { xx }
        , y { yy }
        , z { zz }
        , w { ww }
    {
    }

    [[nodiscard]] static constexpr Quat identity() noexcept
    {
        return Quat {};
    }

    [[nodiscard]] static Quat from_axis_angle(const Vec<T, 3>& axis, T radians) noexcept
    {
        const Vec<T, 3> n = normalize(axis);
        const T half = radians * T { 0.5 };
        const T s = std::sin(half);
        return Quat { n.x * s, n.y * s, n.z * s, std::cos(half) };
    }

    friend constexpr bool operator==(const Quat&, const Quat&) noexcept = default;
};

// --- Operations -------------------------------------------------------------

template <class T>
[[nodiscard]] constexpr Quat<T> operator*(const Quat<T>& a, const Quat<T>& b) noexcept
{
    return Quat<T> {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

template <class T>
[[nodiscard]] constexpr Quat<T> conjugate(const Quat<T>& q) noexcept
{
    return Quat<T> { -q.x, -q.y, -q.z, q.w };
}

template <class T>
[[nodiscard]] constexpr T length_squared(const Quat<T>& q) noexcept
{
    return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
}

template <class T>
[[nodiscard]] T length(const Quat<T>& q) noexcept
{
    return std::sqrt(length_squared(q));
}

template <class T>
[[nodiscard]] Quat<T> normalize(const Quat<T>& q) noexcept
{
    const T len = length(q);
    if (!(len > epsilon_v<T>))
        return Quat<T>::identity();
    const T inv = T { 1 } / len;
    return Quat<T> { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
}

/// Rotate a 3-vector by a unit quaternion.
template <class T>
[[nodiscard]] constexpr Vec<T, 3> rotate(const Quat<T>& q, const Vec<T, 3>& v) noexcept
{
    // Optimised: v' = v + 2*qw*(qv x v) + 2*qv x (qv x v)
    const Vec<T, 3> qv { q.x, q.y, q.z };
    const Vec<T, 3> t = cross(qv, v) * T { 2 };
    return v + (t * q.w) + cross(qv, t);
}

template <class T>
[[nodiscard]] constexpr bool approx_equal(const Quat<T>& a, const Quat<T>& b, T tol = epsilon_v<T> * T { 16 }) noexcept
{
    return approx_equal(a.x, b.x, tol) && approx_equal(a.y, b.y, tol) && approx_equal(a.z, b.z, tol) &&
           approx_equal(a.w, b.w, tol);
}

using Quatf = Quat<float>;
using Quatd = Quat<double>;

}  // namespace cd::math
