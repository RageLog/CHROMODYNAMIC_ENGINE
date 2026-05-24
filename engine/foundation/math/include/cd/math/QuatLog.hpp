// =============================================================================
// CHROMODYNAMIC — cd/math/QuatLog.hpp
// Phase 67.A / Wave 235 — quaternion log / exp / power.
//
// For a unit quaternion q = (cos θ, sin θ · n):
//   log(q)   = (0, θ · n)              — pure vector imaginary
//   exp(v)   = (cos|v|, sin|v|·v/|v|)  — unit quaternion (re-normalized)
//   pow(q,t) = exp(t · log(q))         — fractional rotation
//
// `pow(q, t)` is the canonical "scale this rotation by t" — pairs with
// `additive_blend` (Phase 31) for weighted-additive layers.
//
// All operations clamp through small-angle thresholds to avoid
// divide-by-zero near identity quaternion (q.w ≈ 1).
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Quaternion.hpp>

#include <cmath>

namespace cd::math
{

[[nodiscard]] inline Quatf quat_log(const Quatf& q) noexcept
{
    const float v_len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (v_len < 1e-6F)
        return Quatf { 0.0F, 0.0F, 0.0F, 0.0F };
    const float theta = std::atan2(v_len, q.w);
    const float scale = theta / v_len;
    return Quatf { q.x * scale, q.y * scale, q.z * scale, 0.0F };
}

[[nodiscard]] inline Quatf quat_exp(const Quatf& v) noexcept
{
    const float v_len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (v_len < 1e-6F)
        return Quatf { 0.0F, 0.0F, 0.0F, 1.0F };
    const float s = std::sin(v_len) / v_len;
    return Quatf { v.x * s, v.y * s, v.z * s, std::cos(v_len) };
}

/// `q^t` — fractional rotation. `t = 0` → identity, `t = 1` → q,
/// `t = 0.5` → half-arc rotation (geodesic on the 3-sphere).
[[nodiscard]] inline Quatf quat_pow(const Quatf& q, float t) noexcept
{
    const auto lg = quat_log(q);
    return quat_exp(Quatf { lg.x * t, lg.y * t, lg.z * t, 0.0F });
}

}  // namespace cd::math
