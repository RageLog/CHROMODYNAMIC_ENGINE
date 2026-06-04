// =============================================================================
// CHROMODYNAMIC — cd/audio/spatial/AudioSpatial.hpp
// Phase 693 — cd::audio::spatial Sprint-1 public API.
//
// 3D positional audio sidecar to cd::audio (2D mixer).
// cd::audio handles playback / mixing of audio buffers.
// cd::audio::spatial handles WHERE sounds come from in 3D space.
//
// Sprint-1 DSP:
//   ITD  — Interaural Time Difference: millisecond delay between ears based
//           on azimuth angle.  Models the path-length difference as the sound
//           wraps around the head (~21 cm head radius model).
//   ILD  — Interaural Level Difference: linear gain split between L and R
//           channels based on azimuth (simple cos/sin shadow model).
//   Distance attenuation — inverse-square (1/r) law clamped to [inner, outer]
//           radius:  0 attenuation inside inner, 0 gain outside outer.
//   Doppler — classical shift: f' = f * (c + v_listener) / (c + v_source)
//             approximated in linear pitch ratio.
//
// Sprint-2 queue: HRTF convolution (openal-soft / steam-audio vcpkg).
//
// Thread-safety: none — caller must serialise.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace cd::audio::spatial
{

// =============================================================================
// SoundSource — describes a 3D positional sound emitter
//
// id            : unique emitter identifier (caller-assigned).
// position      : world-space position [x, y, z] in metres.
// velocity      : world-space velocity [x, y, z] in m/s (used for Doppler).
// gain          : master gain scalar [0, ∞).  1.0 = unity.
// pitch         : base pitch shift applied before Doppler (1.0 = none).
// radius_inner  : distance (m) inside which attenuation is 1.0 (no falloff).
// radius_outer  : distance (m) outside which attenuation is 0.0 (full cut).
// loop          : hint to the 2D audio mixer whether this source loops.
// =============================================================================
struct SoundSource
{
    uint64_t                 id            { 0 };
    std::array<float, 3>     position      { 0.0F, 0.0F, 0.0F };
    std::array<float, 3>     velocity      { 0.0F, 0.0F, 0.0F };
    float                    gain          { 1.0F };
    float                    pitch         { 1.0F };
    float                    radius_inner  { 1.0F };
    float                    radius_outer  { 20.0F };
    bool                     loop          { false };
};

// =============================================================================
// Listener — describes the player / camera ears
//
// position         : world-space position [x, y, z] in metres.
// orientation_quat : unit quaternion [x, y, z, w] — head orientation.
//                    The "forward" vector is derived as:
//                      forward = rotate(quat, {0,0,-1})   (right-handed, -Z forward)
//                    The "right"   vector is derived as:
//                      right   = rotate(quat, {1,0, 0})
// velocity         : world-space velocity [x, y, z] in m/s (Doppler).
// =============================================================================
struct Listener
{
    std::array<float, 3>     position         { 0.0F, 0.0F, 0.0F };
    std::array<float, 4>     orientation_quat { 0.0F, 0.0F, 0.0F, 1.0F };  // identity
    std::array<float, 3>     velocity         { 0.0F, 0.0F, 0.0F };
};

// =============================================================================
// SourceMix — computed per-source stereo mix result
//
// left_gain            : [0, 1] gain for left channel (ILD + distance).
// right_gain           : [0, 1] gain for right channel (ILD + distance).
// doppler_pitch        : pitch multiplier (1.0 = no shift).  >1 = higher pitch.
// distance_attenuation : [0, 1] scalar from 1/r falloff model.
// =============================================================================
struct SourceMix
{
    float left_gain            { 1.0F };
    float right_gain           { 1.0F };
    float doppler_pitch        { 1.0F };
    float distance_attenuation { 1.0F };
};

// =============================================================================
// SpatialMixer — computes 3D panning / attenuation for a set of SoundSources
//
// Usage:
//   SpatialMixer mixer;
//   mixer.set_listener(listener);
//   mixer.add_source(source);
//   SourceMix mix = mixer.compute_mix(source.id);
//   // apply mix.left_gain / mix.right_gain to the 2D cd::audio bus
//
// The mixer does NOT produce audio samples — it provides gain + pitch metadata
// that the caller feeds into cd::audio's channel volume / pitch APIs.
// =============================================================================
class SpatialMixer
{
public:
    // -------------------------------------------------------------------------
    // Listener
    // -------------------------------------------------------------------------

    /// Replace the current listener state. Thread-safety: external.
    void set_listener(const Listener& listener) noexcept;

    // -------------------------------------------------------------------------
    // Source management
    // -------------------------------------------------------------------------

    /// Register or replace a sound source.  If id already exists, it is
    /// overwritten with the new SoundSource state.
    void add_source(const SoundSource& source);

    /// Remove a source by id.  No-op if not found.
    void remove_source(uint64_t id);

    /// Update only the 3D position of an existing source.
    /// No-op if the source id is not registered.
    void update_source_position(uint64_t id, std::array<float, 3> pos) noexcept;

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// Compute the stereo mix parameters for a given source.
    /// Returns a default SourceMix (unity / no shift) if the id is not found.
    [[nodiscard]] SourceMix compute_mix(uint64_t source_id) const noexcept;

    /// Returns the number of currently registered sources.
    [[nodiscard]] std::size_t source_count() const noexcept;

private:
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// Rotate a 3-vector by a unit quaternion q (x,y,z,w).
    [[nodiscard]] static std::array<float, 3> rotate_by_quat(
        std::array<float, 3>     v,
        std::array<float, 4>     q) noexcept;

    /// Compute a unit vector from a to b.  Returns {0,0,0} if a == b.
    [[nodiscard]] static std::array<float, 3> direction(
        std::array<float, 3>     from,
        std::array<float, 3>     to) noexcept;

    /// Euclidean distance between two 3D points.
    [[nodiscard]] static float distance(
        std::array<float, 3>     a,
        std::array<float, 3>     b) noexcept;

    /// Dot product of two 3-vectors.
    [[nodiscard]] static float dot(
        std::array<float, 3>     a,
        std::array<float, 3>     b) noexcept;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    Listener                                   listener_ {};
    std::unordered_map<uint64_t, SoundSource>  sources_;

    // Physics constants
    static constexpr float kSpeedOfSound { 343.0F };  ///< m/s at sea level, 20°C
    static constexpr float kHeadRadius   { 0.0875F }; ///< metres (average human)
};

}  // namespace cd::audio::spatial
