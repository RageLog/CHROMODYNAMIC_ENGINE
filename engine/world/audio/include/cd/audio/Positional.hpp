// =============================================================================
// CHROMODYNAMIC — cd/audio/Positional.hpp
// Phase 7 / Wave 77 — 3D positional audio math (ITD + ILD + panning).
//
// Pure-math primitives that take a listener pose + a source position
// and produce the cues a downstream mixer needs to position the sound
// in space:
//
//   * compute_itd_seconds   — Woodworth (1938) interaural time delay.
//                              Positive value = right ear delayed.
//   * compute_ild_db        — simplified head-shadow ILD in decibels.
//                              Positive = right ear louder.
//   * compute_stereo_gains  — final (left, right) gains in [0, 1]
//                              including inverse-distance attenuation
//                              and a constant-power pan law.
//
// All math is on cd::math::Vec3f; no audio backend dependency. The
// audio render thread (WASAPI / CoreAudio / ALSA) is expected to call
// these per voice per chunk and apply the result to its mix.
//
// Reference values:
//   * Speed of sound (15 °C, 0 m altitude)  : 343.0 m/s
//   * Average human head radius              : 8.75 cm = 0.0875 m
//   * ITD range for source at ±90° azimuth   : ±~640 µs
//
// Header-only. Depends on cd::core + cd::math + standard library.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/math/Vector.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cd::audio
{

inline constexpr float kSpeedOfSoundMps { 343.0F };
inline constexpr float kDefaultHeadRadiusM { 0.0875F };

/// Listener orientation in world space. `forward` is the gaze axis;
/// `up` is the cranial-vertical axis. Both must be unit-length; the
/// math helpers below assume orthonormality without re-checking.
struct ListenerPose
{
    cd::math::Vec3f position { 0.0F, 0.0F, 0.0F };
    cd::math::Vec3f forward { 0.0F, 0.0F, -1.0F };
    cd::math::Vec3f up { 0.0F, 1.0F, 0.0F };
};

struct StereoGains
{
    float left { 1.0F };
    float right { 1.0F };
};

namespace positional_detail
{

/// Build the listener's local right vector (forward × up).
[[nodiscard]] inline cd::math::Vec3f right_of(const ListenerPose& l) noexcept
{
    return cd::math::cross(l.forward, l.up);
}

/// Return the source position in the listener's local space — x = right,
/// y = up, z = forward — without the position offset.
[[nodiscard]] inline cd::math::Vec3f
to_local_direction(const ListenerPose& l, const cd::math::Vec3f& src) noexcept
{
    const auto rel = cd::math::Vec3f { src.x - l.position.x,
                                       src.y - l.position.y,
                                       src.z - l.position.z };
    const auto right = right_of(l);
    return cd::math::Vec3f { cd::math::dot(rel, right),
                             cd::math::dot(rel, l.up),
                             cd::math::dot(rel, l.forward) };
}

}  // namespace positional_detail

/// Woodworth ITD: d = (r / c) · (sin θ + θ) where θ is the azimuth
/// angle relative to the median plane. Positive ITD = the right ear
/// hears the sound later (source on the listener's left).
[[nodiscard]] inline float compute_itd_seconds(
    const ListenerPose& listener,
    const cd::math::Vec3f& source_pos,
    float head_radius_m = kDefaultHeadRadiusM) noexcept
{
    const auto local = positional_detail::to_local_direction(listener, source_pos);
    const float horizontal = std::sqrt(local.x * local.x + local.z * local.z);
    if (horizontal < 1.0e-6F)
        return 0.0F;
    // Azimuth: 0 in front, +π/2 to the right, -π/2 to the left.
    const float theta = std::atan2(local.x, std::max(local.z, 1.0e-6F));
    const float clamped = std::clamp(theta, -1.5707963F, 1.5707963F);
    const float d = (head_radius_m / kSpeedOfSoundMps)
                  * (std::sin(clamped) + clamped);
    // Right ear delayed when source is on the LEFT (negative azimuth);
    // we flip the sign so positive output = right ear delayed.
    return -d;
}

/// Simplified head-shadow ILD. Real ILD is strongly frequency-dependent
/// (smaller below ~1.5 kHz, up to ±20 dB above); the lite model uses
/// a single 6-dB-per-radian-sin curve which is good enough for "the
/// source is roughly to one side" panning. Positive = right ear louder.
[[nodiscard]] inline float compute_ild_db(
    const ListenerPose& listener, const cd::math::Vec3f& source_pos) noexcept
{
    const auto local = positional_detail::to_local_direction(listener, source_pos);
    const float horizontal = std::sqrt(local.x * local.x + local.z * local.z);
    if (horizontal < 1.0e-6F)
        return 0.0F;
    const float theta = std::atan2(local.x, std::max(local.z, 1.0e-6F));
    constexpr float kIldPerRadianSin = 6.0F;  // dB
    return kIldPerRadianSin * std::sin(theta);
}

/// (left, right) stereo gains including inverse-distance attenuation
/// (1 / max(distance/ref, rolloff)) and a constant-power pan
/// derived from the azimuth angle. Each output gain is clamped to
/// [0, 1]; clipping is the mixer's responsibility on the final sum.
///
/// `ref_distance_m` = distance at which attenuation is 1.0; closer
/// than ref doesn't get louder (caller-clipped).
[[nodiscard]] inline StereoGains compute_stereo_gains(
    const ListenerPose& listener,
    const cd::math::Vec3f& source_pos,
    float ref_distance_m = 1.0F,
    float rolloff = 1.0F) noexcept
{
    const auto local = positional_detail::to_local_direction(listener, source_pos);
    const float dist = std::sqrt(local.x * local.x + local.y * local.y + local.z * local.z);
    if (dist < 1.0e-6F)
        return { 1.0F, 1.0F };

    const float attenuation = std::min(
        1.0F, ref_distance_m / std::max(dist * rolloff, 1.0e-6F));

    // Azimuth-based constant-power pan: at θ=0 both ears = sqrt(0.5);
    // at θ=+π/2 (right) left=0, right=1.
    const float theta = std::atan2(local.x, std::max(local.z, 1.0e-6F));
    constexpr float kPiOver2 = 1.5707963F;
    const float pan = std::clamp(theta / kPiOver2, -1.0F, 1.0F);  // -1 left, +1 right
    const float angle = (pan + 1.0F) * 0.25F * std::numbers::pi_v<float>;  // 0 → π/2
    const float L = std::cos(angle);
    const float R = std::sin(angle);
    return { std::clamp(attenuation * L, 0.0F, 1.0F),
             std::clamp(attenuation * R, 0.0F, 1.0F) };
}

}  // namespace cd::audio
