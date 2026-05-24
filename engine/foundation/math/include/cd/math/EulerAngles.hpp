// =============================================================================
// CHROMODYNAMIC — cd/math/EulerAngles.hpp
// Phase 95.B / Wave 263 — Euler XYZ ↔ quaternion conversion (Vec3 wrap).
//
// XYZ-order intrinsic Euler angles (pitch about X, yaw about Y, roll
// about Z) in radians. Conversion to quaternion uses the standard
// half-angle factored formula:
//
//   q = qz * qy * qx   (intrinsic XYZ → multiply right-to-left)
//
// Returns quaternion (x, y, z, w) consistent with Quaternion.hpp
// convention.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>
#include <cd/math/Vector.hpp>

#include <cmath>

namespace cd::math
{

[[nodiscard]] inline Quatf euler_xyz_to_quat(const Vec3f& euler_rad) noexcept
{
    const float hx = euler_rad.x * 0.5F;
    const float hy = euler_rad.y * 0.5F;
    const float hz = euler_rad.z * 0.5F;
    const float cx = std::cos(hx), sx = std::sin(hx);
    const float cy = std::cos(hy), sy = std::sin(hy);
    const float cz = std::cos(hz), sz = std::sin(hz);
    return Quatf {
        sx * cy * cz + cx * sy * sz,   // x
        cx * sy * cz - sx * cy * sz,   // y
        cx * cy * sz + sx * sy * cz,   // z
        cx * cy * cz - sx * sy * sz,   // w
    };
}

[[nodiscard]] inline Vec3f quat_to_euler_xyz(const Quatf& q) noexcept
{
    Vec3f e;
    // Pitch (x)
    const float sinr_cosp = 2.0F * (q.w * q.x + q.y * q.z);
    const float cosr_cosp = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
    e.x = std::atan2(sinr_cosp, cosr_cosp);
    // Yaw (y)
    const float sinp = 2.0F * (q.w * q.y - q.z * q.x);
    if (std::fabs(sinp) >= 1.0F)
        e.y = std::copysign(1.5707963F, sinp);   // gimbal lock at ±90°
    else
        e.y = std::asin(sinp);
    // Roll (z)
    const float siny_cosp = 2.0F * (q.w * q.z + q.x * q.y);
    const float cosy_cosp = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
    e.z = std::atan2(siny_cosp, cosy_cosp);
    return e;
}

}  // namespace cd::math
