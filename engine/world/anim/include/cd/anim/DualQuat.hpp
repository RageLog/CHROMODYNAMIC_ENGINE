// =============================================================================
// CHROMODYNAMIC — cd/anim/DualQuat.hpp
// Phase D-F10 — Kavan 2008 Dual-Quaternion Skinning support types.
//
// Kavan et al. 2008 "Geometric Skinning with Approximate Dual Quaternion
// Blending" (ACM TOG 27:4, DOI: 10.1145/1409625.1409627) §4.1, Eq. 7.
//
// A unit dual quaternion encodes a rigid-body motion (rotation + translation)
// exactly. Each skinning joint is represented as a DualQuat; they are blended
// linearly in the 8-dimensional DQ space and then normalised (one rsqrt vs the
// LBS 4×4 matrix accumulation). This eliminates the "candy-wrapper" collapse
// artefact that linear matrix blending produces at large joint twists.
//
// Convention
// ----------
//   real  : unit quaternion encoding the rotation (qr)
//   dual  : pure-quaternion part encoding translation (qd = 0.5 * t * qr
//            where t = Quat{tx, ty, tz, 0})
//
// The "approximate" in the paper title refers to the blend being an
// approximation in the space of rigid motions — it is geodesically close but
// not exact for heterogeneous weight distributions. For character skinning the
// approximation is visually indistinguishable from exact methods.
//
// API surface
// -----------
//   DualQuat                — storage struct (no virtual, no heap)
//   from_rigid(R, t)        — build from rotation quaternion + translation
//   from_mat4(M)            — extract rigid part from a skinning matrix
//   to_mat4(dq)             — recompose a column-major 4×4 skin matrix
//   blend(weights, dqs)     — antipodality-corrected linear blend + normalise
//
// All functions are header-inline; the library adds no new .cpp.
// =============================================================================
#pragma once

#include <cd/math/Matrix.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>

namespace cd::anim
{

// ---------------------------------------------------------------------------
// DualQuat<T>
// ---------------------------------------------------------------------------
template <class T>
struct DualQuat
{
    cd::math::Quat<T> real {};  ///< rotation part (unit quaternion)
    cd::math::Quat<T> dual {};  ///< translation-encoded dual part (0.5*t*real)

    /// Identity: no rotation, no translation.
    [[nodiscard]] static constexpr DualQuat identity() noexcept
    {
        DualQuat dq;
        dq.real = cd::math::Quat<T>::identity();
        dq.dual = cd::math::Quat<T> { T { 0 }, T { 0 }, T { 0 }, T { 0 } };
        return dq;
    }
};

using DualQuatf = DualQuat<float>;

// ---------------------------------------------------------------------------
// from_rigid — build from a rotation quaternion + translation vector
// ---------------------------------------------------------------------------
// Kavan 2008 §2, Eq. 1: qd = (1/2) * [tx, ty, tz, 0] * qr
template <class T>
[[nodiscard]] inline DualQuat<T> from_rigid(const cd::math::Quat<T>& rot,
                                             const cd::math::Vec<T, 3>& trans) noexcept
{
    DualQuat<T> dq;
    dq.real = rot;
    // pure quaternion encoding the translation: t = {tx, ty, tz, 0}
    // dual = 0.5 * t * rot
    const T half = T { 0.5 };
    dq.dual.x = half * ( trans.x * rot.w + trans.y * rot.z - trans.z * rot.y);
    dq.dual.y = half * (-trans.x * rot.z + trans.y * rot.w + trans.z * rot.x);
    dq.dual.z = half * ( trans.x * rot.y - trans.y * rot.x + trans.z * rot.w);
    dq.dual.w = half * (-trans.x * rot.x - trans.y * rot.y - trans.z * rot.z);
    return dq;
}

// ---------------------------------------------------------------------------
// from_mat4 — extract rigid-body dual quaternion from a column-major Mat4
// ---------------------------------------------------------------------------
// The skinning matrix must be a rigid transform (rotation + translation, no
// scale beyond what is already baked into the inverse-bind). This is the
// typical output of compute_skinning_matrices(). If scale is present it is
// ignored (rotation is extracted via the upper-left 3×3, normalised to unit
// quaternion; scale component is thus dropped).
//
// Column-major storage: m[c][r] → element at column c, row r.
template <class T>
[[nodiscard]] inline DualQuat<T> from_mat4(const cd::math::Mat<T, 4>& m) noexcept
{
    // Extract the 3×3 rotation sub-matrix (upper-left block) and convert to
    // quaternion (Shepperd method).
    const T m00 = m[0][0], m10 = m[1][0], m20 = m[2][0];
    const T m01 = m[0][1], m11 = m[1][1], m21 = m[2][1];
    const T m02 = m[0][2], m12 = m[1][2], m22 = m[2][2];

    const T trace = m00 + m11 + m22;
    cd::math::Quat<T> rot;

    // Variable naming: mCR where C=column index, R=row index (column-major
    // storage m[col][row]). Shepperd antisymmetric differences expressed in
    // standard row-major R[row][col]: (R[2][1]-R[1][2]) = m[1][2]-m[2][1]
    // = m12-m21.  Symmetric sums are invariant to index transposition.
    if (trace > T { 0 })
    {
        const T s = std::sqrt(trace + T { 1 }) * T { 2 };  // s = 4*qw
        const T inv_s = T { 1 } / s;
        rot.w = T { 0.25 } * s;
        rot.x = (m12 - m21) * inv_s;  // (R[2][1]-R[1][2])
        rot.y = (m20 - m02) * inv_s;  // (R[0][2]-R[2][0])
        rot.z = (m01 - m10) * inv_s;  // (R[1][0]-R[0][1])
    }
    else if ((m00 > m11) && (m00 > m22))
    {
        const T s = std::sqrt(T { 1 } + m00 - m11 - m22) * T { 2 };  // s = 4*qx
        const T inv_s = T { 1 } / s;
        rot.w = (m12 - m21) * inv_s;
        rot.x = T { 0.25 } * s;
        rot.y = (m01 + m10) * inv_s;  // (R[1][0]+R[0][1])
        rot.z = (m02 + m20) * inv_s;  // (R[2][0]+R[0][2])
    }
    else if (m11 > m22)
    {
        const T s = std::sqrt(T { 1 } + m11 - m00 - m22) * T { 2 };  // s = 4*qy
        const T inv_s = T { 1 } / s;
        rot.w = (m20 - m02) * inv_s;
        rot.x = (m01 + m10) * inv_s;
        rot.y = T { 0.25 } * s;
        rot.z = (m12 + m21) * inv_s;  // (R[2][1]+R[1][2])
    }
    else
    {
        const T s = std::sqrt(T { 1 } + m22 - m00 - m11) * T { 2 };  // s = 4*qz
        const T inv_s = T { 1 } / s;
        rot.w = (m01 - m10) * inv_s;
        rot.x = (m02 + m20) * inv_s;
        rot.y = (m12 + m21) * inv_s;
        rot.z = T { 0.25 } * s;
    }
    rot = normalize(rot);

    // Translation: column 3, rows 0..2 (column-major: m[3][0], m[3][1], m[3][2]).
    const cd::math::Vec<T, 3> trans { m[3][0], m[3][1], m[3][2] };
    return from_rigid(rot, trans);
}

// ---------------------------------------------------------------------------
// to_mat4 — reconstruct a column-major 4×4 skinning matrix from a DualQuat
// ---------------------------------------------------------------------------
// Kavan 2008 §4.2, Eq. 10 (decompose translation from the normalised DQ,
// then convert rotation to matrix).
template <class T>
[[nodiscard]] inline cd::math::Mat<T, 4> to_mat4(const DualQuat<T>& dq) noexcept
{
    // Recover rotation matrix from the real part.
    const T qx = dq.real.x, qy = dq.real.y, qz = dq.real.z, qw = dq.real.w;
    const T xx = qx * qx, yy = qy * qy, zz = qz * qz;
    const T xy = qx * qy, xz = qx * qz, yz = qy * qz;
    const T wx = qw * qx, wy = qw * qy, wz = qw * qz;

    // Recover translation: t = 2 * qd * conjugate(qr)
    // conjugate(qr) = {-qx, -qy, -qz, qw}.
    // Kavan 2008 §4.2, Eq. 10 — only the vector part carries the translation.
    const T dqx = dq.dual.x, dqy = dq.dual.y, dqz = dq.dual.z, dqw = dq.dual.w;
    const T t2x = T { 2 } * (-dqw * qx + dqx * qw - dqy * qz + dqz * qy);
    const T t2y = T { 2 } * (-dqw * qy + dqx * qz + dqy * qw - dqz * qx);
    const T t2z = T { 2 } * (-dqw * qz - dqx * qy + dqy * qx + dqz * qw);

    cd::math::Mat<T, 4> m {};
    // Column 0
    m[0][0] = T { 1 } - T { 2 } * (yy + zz);
    m[0][1] = T { 2 } * (xy + wz);
    m[0][2] = T { 2 } * (xz - wy);
    m[0][3] = T { 0 };
    // Column 1
    m[1][0] = T { 2 } * (xy - wz);
    m[1][1] = T { 1 } - T { 2 } * (xx + zz);
    m[1][2] = T { 2 } * (yz + wx);
    m[1][3] = T { 0 };
    // Column 2
    m[2][0] = T { 2 } * (xz + wy);
    m[2][1] = T { 2 } * (yz - wx);
    m[2][2] = T { 1 } - T { 2 } * (xx + yy);
    m[2][3] = T { 0 };
    // Column 3 — translation
    m[3][0] = t2x;
    m[3][1] = t2y;
    m[3][2] = t2z;
    m[3][3] = T { 1 };
    return m;
}

// ---------------------------------------------------------------------------
// blend — DQS weighted blend with antipodality correction
// ---------------------------------------------------------------------------
// Kavan 2008 §4.1, Eq. 7, p. 105:8.
//
// The key insight: quaternions q and -q represent the same rotation, but
// their dual-quaternion counterparts point in opposite hemispheres of the
// 8D space. Naively blending them cancels to near-zero. The fix: flip any
// DQ whose real part has a negative dot product with the first DQ in the
// blend (antipodality flip). Then accumulate weighted sums, normalise the
// result dual quaternion by the magnitude of its real part.
//
// weights and dqs must have the same length; zero-weighted entries are
// skipped. Returns the identity DQ if all weights are zero.
template <class T>
[[nodiscard]] inline DualQuat<T> blend(std::span<const T> weights,
                                        std::span<const DualQuat<T>> dqs) noexcept
{
    assert(weights.size() == dqs.size());

    DualQuat<T> accum;
    accum.real = cd::math::Quat<T> { T { 0 }, T { 0 }, T { 0 }, T { 0 } };
    accum.dual = cd::math::Quat<T> { T { 0 }, T { 0 }, T { 0 }, T { 0 } };

    // Determine the reference DQ (first non-zero-weight joint) for the
    // antipodality flip.
    const cd::math::Quat<T>* pivot = nullptr;
    for (std::size_t i = 0; i < weights.size(); ++i)
    {
        if (weights[i] > T { 0 })
        {
            pivot = &dqs[i].real;
            break;
        }
    }
    if (pivot == nullptr)
        return DualQuat<T>::identity();

    for (std::size_t i = 0; i < weights.size(); ++i)
    {
        const T w = weights[i];
        if (w <= T { 0 })
            continue;

        const DualQuat<T>& src = dqs[i];
        // Antipodality: if dot(pivot, src.real) < 0, flip the DQ.
        const T d = pivot->x * src.real.x + pivot->y * src.real.y
                  + pivot->z * src.real.z + pivot->w * src.real.w;
        const T sign = d < T { 0 } ? T { -1 } : T { 1 };

        accum.real.x += sign * w * src.real.x;
        accum.real.y += sign * w * src.real.y;
        accum.real.z += sign * w * src.real.z;
        accum.real.w += sign * w * src.real.w;

        accum.dual.x += sign * w * src.dual.x;
        accum.dual.y += sign * w * src.dual.y;
        accum.dual.z += sign * w * src.dual.z;
        accum.dual.w += sign * w * src.dual.w;
    }

    // Normalise by the magnitude of the real part (Kavan 2008 §4.1 norm).
    const T len_sq = accum.real.x * accum.real.x + accum.real.y * accum.real.y
                   + accum.real.z * accum.real.z + accum.real.w * accum.real.w;
    if (len_sq < T { 1e-12F })
        return DualQuat<T>::identity();
    const T inv_len = T { 1 } / std::sqrt(len_sq);
    accum.real.x *= inv_len;
    accum.real.y *= inv_len;
    accum.real.z *= inv_len;
    accum.real.w *= inv_len;
    accum.dual.x *= inv_len;
    accum.dual.y *= inv_len;
    accum.dual.z *= inv_len;
    accum.dual.w *= inv_len;
    return accum;
}

}  // namespace cd::anim
