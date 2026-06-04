// =============================================================================
// CHROMODYNAMIC — cd/audio/spatial/AudioSpatial.cpp
// Phase 693 — cd::audio::spatial Sprint-1 implementation.
//
// ITD model:
//   The simplified Woodworth-Schlosberg formula:
//     tau = (r / c) * (theta + sin(theta))
//   where r = head radius, c = speed of sound, theta = azimuth angle
//   (0 = front, pi/2 = right, pi = rear, -pi/2 = left).
//   We don't actually apply a sample delay here — we encode the ITD as a
//   pitch-coherent gain imbalance (ILD) rather than a time shift, because the
//   mixer output is a gain struct, not a signal.  Full ITD as a delay line
//   belongs in Sprint-2's HRTF convolution path.
//
// ILD model:
//   Simple head-shadow: attenuation of the contralateral channel.
//     right_gain = cos(theta/2) * 0.5 + 0.5   (range [0.5, 1.0])
//     left_gain  = cos((pi - theta)/2) * ...   (mirror)
//   More precisely we use:
//     azimuth_01 = (theta + pi) / (2*pi)        → [0, 1]  (0=left, 0.5=front, 1=right)
//     right_gain = clamp(0.5 + azimuth_01, 0,1)
//     left_gain  = clamp(1.5 - azimuth_01, 0,1)
//   This gives unity on both sides when source is directly in front (azimuth=0),
//   and max right / min left when the source is to the far right.
//
// Distance attenuation (1/r):
//   d = distance(listener, source)
//   if d <= inner: attenuation = 1.0
//   if d >= outer: attenuation = 0.0
//   else:          attenuation = inner / d    (inverse linear, not inverse-square,
//                                              matches OpenAL LINEAR_DISTANCE_CLAMPED
//                                              feel for game audio)
//
// Doppler:
//   Classical approximation using projected velocities along listener-source axis:
//     pitch_shift = (c + v_listener_projected) / (c + v_source_projected)
//   where "projected" means the component along the direction FROM source TO listener.
//   Result clamped to [0.1, 4.0] to avoid extreme shifts.
// =============================================================================

#include <cd/audio/spatial/AudioSpatial.hpp>

#include <algorithm>
#include <cmath>

namespace cd::audio::spatial
{

// =============================================================================
// SpatialMixer — public interface
// =============================================================================

void SpatialMixer::set_listener(const Listener& listener) noexcept
{
    listener_ = listener;
}

void SpatialMixer::add_source(const SoundSource& source)
{
    sources_[source.id] = source;
}

void SpatialMixer::remove_source(uint64_t id)
{
    sources_.erase(id);
}

void SpatialMixer::update_source_position(uint64_t id, std::array<float, 3> pos) noexcept
{
    const auto it = sources_.find(id);
    if (it != sources_.end())
        it->second.position = pos;
}

std::size_t SpatialMixer::source_count() const noexcept
{
    return sources_.size();
}

// =============================================================================
// SpatialMixer::compute_mix — core 3D spatialization
// =============================================================================

SourceMix SpatialMixer::compute_mix(uint64_t source_id) const noexcept
{
    // Default: unity stereo, no pitch shift, no attenuation
    SourceMix mix{};

    const auto it = sources_.find(source_id);
    if (it == sources_.end())
        return mix;

    const SoundSource& src = it->second;

    // -------------------------------------------------------------------------
    // 1. World-space direction from listener to source
    // -------------------------------------------------------------------------
    const float dist = distance(listener_.position, src.position);

    // -------------------------------------------------------------------------
    // 2. Distance attenuation (inverse linear, clamped)
    // -------------------------------------------------------------------------
    const float inner = (src.radius_inner > 0.0F) ? src.radius_inner : 0.001F;
    const float outer = (src.radius_outer > inner) ? src.radius_outer : inner + 0.001F;

    float atten = 1.0F;
    if (dist >= outer)
    {
        atten = 0.0F;
    }
    else if (dist > inner)
    {
        atten = inner / dist;  // 1/r falloff in [inner, outer]
        atten = std::clamp(atten, 0.0F, 1.0F);
    }

    mix.distance_attenuation = atten;

    // -------------------------------------------------------------------------
    // 3. Listener-local coordinate system from orientation quaternion
    //
    //    We extract the right-ear direction from the quaternion so we can
    //    project the source direction onto it and get an azimuth angle.
    //    Listener faces -Z (right-handed convention); right ear = +X.
    //
    //    right_world = rotate_by_quat({1,0,0}, orientation_quat)
    // -------------------------------------------------------------------------
    const std::array<float, 3> right_world =
        rotate_by_quat({ 1.0F, 0.0F, 0.0F }, listener_.orientation_quat);

    // Direction from listener to source (unit vector)
    std::array<float, 3> to_src { 0.0F, 0.0F, -1.0F };  // default: directly in front
    if (dist > 1e-5F)
        to_src = direction(listener_.position, src.position);

    // Azimuth: dot of to_src with right ear direction
    // +1 → fully to the right, -1 → fully to the left, 0 → front/back
    const float azimuth_dot = dot(to_src, right_world);  // in [-1, +1]

    // -------------------------------------------------------------------------
    // 4. ILD (Interaural Level Difference) — linear head-shadow model
    //
    //    Map azimuth_dot [-1,+1] to panning in [0,1]
    //      panning = (azimuth_dot + 1) / 2   → 0 = full left, 1 = full right
    //    Standard equal-power pan law:
    //      right_pan_gain = sqrt(panning)
    //      left_pan_gain  = sqrt(1 - panning)
    //
    //    We use a linear blend for Sprint-1 (simple head shadow):
    //      right_gain_ild = 0.5 + 0.5 * azimuth_dot   → [0, 1]
    //      left_gain_ild  = 0.5 - 0.5 * azimuth_dot   → [0, 1]  (symmetric)
    // -------------------------------------------------------------------------
    const float right_ild = std::clamp(0.5F + 0.5F * azimuth_dot, 0.0F, 1.0F);
    const float left_ild  = std::clamp(0.5F - 0.5F * azimuth_dot, 0.0F, 1.0F);

    // Apply source gain and distance attenuation to ILD
    mix.left_gain  = left_ild  * atten * src.gain;
    mix.right_gain = right_ild * atten * src.gain;

    // -------------------------------------------------------------------------
    // 5. Doppler pitch shift
    //
    //    Direction from source to listener (the axis of motion that matters):
    //      d_vec = normalise(listener.pos - src.pos)
    //
    //    Projected velocities (positive = moving toward each other):
    //      v_listener = dot(listener.velocity,  d_vec)   (toward source is positive)
    //      v_source   = dot(src.velocity,       d_vec)   (toward listener is positive,
    //                                                      but source moving toward
    //                                                      listener means d_vec for
    //                                                      source is negated)
    //
    //    pitch_shift = (c + v_listener) / (c + v_source)
    //    where c = speed of sound.
    //
    //    "d_vec" from SOURCE to LISTENER = -to_src (we had to_src = listener→source).
    // -------------------------------------------------------------------------
    const std::array<float, 3> src_to_listener {
        -to_src[0], -to_src[1], -to_src[2]
    };

    // Listener moves toward source → positive component along src_to_listener
    const float v_listener = dot(listener_.velocity, src_to_listener);

    // Source moves toward listener → positive component along src_to_listener
    // (we negate to_src: source moving along src_to_listener = closing in)
    const float v_source_toward = dot(src.velocity, src_to_listener);

    // Classical Doppler: f' / f = (c + v_listener) / (c + v_source)
    // Guard denominator: speed of sound minus source velocity (approaching
    // listener adds to denominator in standard formula; we flip sign:
    //   denominator = c - dot(src.velocity, to_listener)
    //               = c - v_source_toward
    const float numerator   = kSpeedOfSound + v_listener;
    const float denominator = kSpeedOfSound - v_source_toward;

    float doppler = 1.0F;
    if (std::abs(denominator) > 1e-3F)
        doppler = numerator / denominator;

    // Apply source base pitch and clamp to sane range [0.1, 4.0]
    mix.doppler_pitch = std::clamp(src.pitch * doppler, 0.1F, 4.0F);

    return mix;
}

// =============================================================================
// Static helpers
// =============================================================================

std::array<float, 3> SpatialMixer::rotate_by_quat(
    std::array<float, 3> v,
    std::array<float, 4> q) noexcept
{
    // Sandwich product: v' = q * (0, v) * q_conjugate
    // Rodrigues' rotation formula (quaternion form):
    //   v' = v + 2w*(q_xyz × v) + 2*(q_xyz × (q_xyz × v))
    // where q = (q_xyz, w) = (q[0], q[1], q[2], q[3])
    const float qx = q[0];
    const float qy = q[1];
    const float qz = q[2];
    const float qw = q[3];

    // t = 2 * (q_xyz cross v)
    const float tx = 2.0F * (qy * v[2] - qz * v[1]);
    const float ty = 2.0F * (qz * v[0] - qx * v[2]);
    const float tz = 2.0F * (qx * v[1] - qy * v[0]);

    // v' = v + w*t + (q_xyz cross t)
    return {
        v[0] + qw * tx + (qy * tz - qz * ty),
        v[1] + qw * ty + (qz * tx - qx * tz),
        v[2] + qw * tz + (qx * ty - qy * tx)
    };
}

std::array<float, 3> SpatialMixer::direction(
    std::array<float, 3> from,
    std::array<float, 3> to) noexcept
{
    const float dx = to[0] - from[0];
    const float dy = to[1] - from[1];
    const float dz = to[2] - from[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-7F)
        return { 0.0F, 0.0F, 0.0F };
    const float inv = 1.0F / len;
    return { dx * inv, dy * inv, dz * inv };
}

float SpatialMixer::distance(
    std::array<float, 3> a,
    std::array<float, 3> b) noexcept
{
    const float dx = b[0] - a[0];
    const float dy = b[1] - a[1];
    const float dz = b[2] - a[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float SpatialMixer::dot(
    std::array<float, 3> a,
    std::array<float, 3> b) noexcept
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

}  // namespace cd::audio::spatial
