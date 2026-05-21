// =============================================================================
// CHROMODYNAMIC — cd/math/Transform.hpp
// ADR-017 P4 (Sprint S2.7) — affine TRS transform composition + projections.
//
// `Transform` packs translation + rotation (unit quaternion) + scale. Order of
// application is **S then R then T** (model-space vertex → world: `T·R·S·v`).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Constants.hpp>
#include <cd/math/Functions.hpp>
#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::math
{

template <class T>
struct Transform
{
    Vec<T, 3> position { T { 0 }, T { 0 }, T { 0 } };
    Quat<T> rotation {};  // identity by default
    Vec<T, 3> scale { T { 1 }, T { 1 }, T { 1 } };

    [[nodiscard]] static constexpr Transform identity() noexcept
    {
        return {};
    }

    /// Apply S, then R, then T to a point.
    [[nodiscard]] Vec<T, 3> apply_to_point(const Vec<T, 3>& p) const noexcept
    {
        Vec<T, 3> scaled { p.x * scale.x, p.y * scale.y, p.z * scale.z };
        return rotate(rotation, scaled) + position;
    }

    /// Apply only R to a direction vector (ignores T/S).
    [[nodiscard]] Vec<T, 3> apply_to_direction(const Vec<T, 3>& d) const noexcept
    {
        return rotate(rotation, d);
    }
};

/// Convert a Quat to a 3x3 rotation matrix (column-major).
template <class T>
[[nodiscard]] Mat<T, 3> to_mat3(const Quat<T>& q) noexcept
{
    const T xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const T xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const T wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    return Mat<T, 3> {
        { T { 1 } - T { 2 } * (yy + zz), T { 2 } * (xy + wz),           T { 2 } * (xz - wy)           },
        { T { 2 } * (xy - wz),           T { 1 } - T { 2 } * (xx + zz), T { 2 } * (yz + wx)           },
        { T { 2 } * (xz + wy),           T { 2 } * (yz - wx),           T { 1 } - T { 2 } * (xx + yy) },
    };
}

/// Build a column-major model matrix from a Transform.
template <class T>
[[nodiscard]] Mat<T, 4> to_mat4(const Transform<T>& xf) noexcept
{
    const Mat<T, 3> r3 = to_mat3(xf.rotation);
    Mat<T, 4> m {};
    // columns 0..2 hold rotated-scaled basis vectors; column 3 holds translation.
    m[0] = Vec<T, 4> { r3[0].x * xf.scale.x, r3[0].y * xf.scale.x, r3[0].z * xf.scale.x, T { 0 } };
    m[1] = Vec<T, 4> { r3[1].x * xf.scale.y, r3[1].y * xf.scale.y, r3[1].z * xf.scale.y, T { 0 } };
    m[2] = Vec<T, 4> { r3[2].x * xf.scale.z, r3[2].y * xf.scale.z, r3[2].z * xf.scale.z, T { 0 } };
    m[3] = Vec<T, 4> { xf.position.x, xf.position.y, xf.position.z, T { 1 } };
    return m;
}

/// Right-handed look-at matrix. Camera at `eye` looking at `target`, with
/// `up` defining the camera's vertical axis (typically world-up).
template <class T>
[[nodiscard]] Mat<T, 4> look_at(const Vec<T, 3>& eye, const Vec<T, 3>& target, const Vec<T, 3>& up) noexcept
{
    const Vec<T, 3> f = normalize(target - eye);  // forward (camera looks -z RH)
    const Vec<T, 3> s = normalize(cross(f, up));  // right
    const Vec<T, 3> u = cross(s, f);              // recomputed up

    Mat<T, 4> m = Mat<T, 4>::identity();
    m[0][0] = s.x;
    m[1][0] = s.y;
    m[2][0] = s.z;
    m[0][1] = u.x;
    m[1][1] = u.y;
    m[2][1] = u.z;
    m[0][2] = -f.x;
    m[1][2] = -f.y;
    m[2][2] = -f.z;
    m[3][0] = -dot(s, eye);
    m[3][1] = -dot(u, eye);
    m[3][2] = dot(f, eye);
    return m;
}

/// Right-handed perspective projection, depth in [0, 1] (Vulkan/D3D convention).
/// `fov_y_radians` is the vertical field of view; `aspect` = width/height.
template <class T>
[[nodiscard]] Mat<T, 4> perspective(T fov_y_radians, T aspect, T near_z, T far_z) noexcept
{
    const T tan_half = std::tan(fov_y_radians * T { 0.5 });
    Mat<T, 4> m {};
    m[0][0] = T { 1 } / (aspect * tan_half);
    m[1][1] = T { 1 } / tan_half;
    m[2][2] = far_z / (near_z - far_z);
    m[2][3] = T { -1 };
    m[3][2] = (near_z * far_z) / (near_z - far_z);
    return m;
}

/// Right-handed orthographic projection, depth in [0, 1].
template <class T>
[[nodiscard]] Mat<T, 4> ortho(T left, T right, T bottom, T top, T near_z, T far_z) noexcept
{
    Mat<T, 4> m {};
    m[0][0] = T { 2 } / (right - left);
    m[1][1] = T { 2 } / (top - bottom);
    m[2][2] = T { 1 } / (near_z - far_z);
    m[3][0] = -(right + left) / (right - left);
    m[3][1] = -(top + bottom) / (top - bottom);
    m[3][2] = near_z / (near_z - far_z);
    m[3][3] = T { 1 };
    return m;
}

/// Shortest-arc spherical linear interpolation between unit quaternions.
template <class T>
[[nodiscard]] Quat<T> slerp(Quat<T> a, Quat<T> b, T t) noexcept
{
    T cos_theta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (cos_theta < T { 0 })
    {
        b = Quat<T> { -b.x, -b.y, -b.z, -b.w };
        cos_theta = -cos_theta;
    }
    // If nearly parallel, fall back to linear interpolation + renormalize.
    constexpr T kLinearThreshold = static_cast<T>(0.9995);
    if (cos_theta > kLinearThreshold)
    {
        Quat<T> r { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z), a.w + t * (b.w - a.w) };
        return normalize(r);
    }
    const T theta = std::acos(cos_theta);
    const T sin_theta = std::sin(theta);
    const T s_a = std::sin((T { 1 } - t) * theta) / sin_theta;
    const T s_b = std::sin(t * theta) / sin_theta;
    return Quat<T> { s_a * a.x + s_b * b.x, s_a * a.y + s_b * b.y, s_a * a.z + s_b * b.z, s_a * a.w + s_b * b.w };
}

using Transformf = Transform<float>;
using Transformd = Transform<double>;

}  // namespace cd::math
