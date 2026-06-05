// =============================================================================
// CHROMODYNAMIC — cd/audio/spatial/HrtfMixer.hpp
// Phase 751 — cd::audio::spatial Sprint-2: real HRTF convolution via OpenAL Soft.
//
// Design intent:
//   Sprint-1 (AudioSpatial.hpp) delivers gain metadata (ILD) + Doppler pitch.
//   Sprint-2 (HrtfMixer) uses OpenAL Soft's built-in HRTF dataset to deliver
//   *real* binaural convolution: the HRTF filters encode pinnae reflections,
//   ITD delay lines, and head-shadow at each measurement angle in a compact
//   impulse response database.  The result is the Half-Life Alyx spatial audio
//   quality: sounds appear firmly localised in 3D space through headphones.
//
// Architecture:
//   HrtfMixer wraps an ALCdevice / ALCcontext pair initialised with the
//   ALC_HRTF_SOFT extension.  Each registered SoundSource maps to one ALuint
//   AL source object.  When the caller feeds PCM frames (push model), HrtfMixer
//   queues them via AL buffer streaming.  Listener pose is forwarded to
//   alListener3f / alListenerfv every frame.
//
//   The class is intentionally thin — it does NOT own a mixing thread; that
//   belongs to cd::audio's IAudioBackend.  HrtfMixer is the *spatialization
//   bridge*: it owns the OpenAL device/context lifecycle and exposes a
//   submit_pcm() / set_source_pose() / update_listener() surface that the
//   cd::audio::spatial::SpatialMixer caller (or the samples layer) drives.
//
// AL type isolation:
//   AL/al.h, AL/alc.h, and AL/alext.h are included ONLY in HrtfMixer.cpp.
//   The header is opaque w.r.t. OpenAL types: the private AL state is stored
//   as void* (erased) in the no-openal path and as opaque handles, avoiding
//   inclusion of AL headers in downstream TUs.  The compile-time flag
//   CD_AUDIO_SPATIAL_HAS_OPENAL (0 or 1) is propagated via PUBLIC compile
//   definition from cd_audio_spatial's CMakeLists so callers can #if-guard
//   without needing the headers themselves.
//
// Fallback:
//   When OpenAL Soft is unavailable (CD_AUDIO_SPATIAL_HAS_OPENAL == 0) the
//   class still compiles — all methods become no-ops that return false / 0,
//   and SpatialMixer Sprint-1 gains are the operative path.
//
// LGPL-2.0-or-later notice:
//   openal-soft is LGPL.  Linked as a SHARED library (vcpkg default on
//   Windows) so the CHROMODYNAMIC binary itself is not statically affected.
//   See docs/ADR/ADR-20260605-openal-soft-lgpl.md for the full analysis.
//
// Thread-safety: none — caller must serialise.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace cd::audio::spatial
{

// ---------------------------------------------------------------------------
// HrtfSourceId — opaque handle returned by add_hrtf_source().
//   Callers keep this handle; pass it to submit_pcm() / remove_hrtf_source().
// ---------------------------------------------------------------------------
using HrtfSourceId = uint64_t;

// ---------------------------------------------------------------------------
// PcmFormat — describes the interleaved PCM frames pushed to submit_pcm().
// ---------------------------------------------------------------------------
struct PcmFormat
{
    uint32_t  sample_rate   { 48000 };  ///< Hz — must match device sample rate
    uint32_t  channel_count { 1 };      ///< 1 = mono (recommended for HRTF)
    bool      is_float32    { true };   ///< true = float32; false = int16
};

// ---------------------------------------------------------------------------
// HrtfMixer — OpenAL Soft HRTF binaural convolution bridge.
//
// Typical lifecycle:
//
//   HrtfMixer hrtf;
//   if (!hrtf.init())                     // opens device + context, enables HRTF
//       /* fallback to SpatialMixer */;
//
//   HrtfSourceId id = hrtf.add_hrtf_source(1u, { 5.f, 0.f, 0.f });
//   hrtf.submit_pcm(id, fmt, samples.data(), samples.size());
//
//   // each frame:
//   hrtf.set_source_pose(id, position, velocity);
//   hrtf.update_listener(position, orientation_quat, velocity);
//
//   hrtf.remove_hrtf_source(id);
//   hrtf.shutdown();                       // optional; called by dtor
// ---------------------------------------------------------------------------
class HrtfMixer
{
public:
    // -------------------------------------------------------------------------
    // Construction / destruction
    // -------------------------------------------------------------------------

    HrtfMixer() noexcept = default;
    ~HrtfMixer() noexcept;

    HrtfMixer(const HrtfMixer&)            = delete;
    HrtfMixer& operator=(const HrtfMixer&) = delete;
    HrtfMixer(HrtfMixer&&)                 = delete;
    HrtfMixer& operator=(HrtfMixer&&)      = delete;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// Open the default audio device and create an OpenAL context with HRTF
    /// enabled.  Returns true on success.  On failure (no device, HRTF
    /// extension absent) the HrtfMixer is left in a "disabled" state where
    /// every other method is a safe no-op.
    ///
    /// preferred_device: pass "" or std::nullopt for the system default.
    [[nodiscard]] bool init(
        const std::optional<std::string>& preferred_device = std::nullopt) noexcept;

    /// Release all OpenAL resources.  Safe to call multiple times.
    void shutdown() noexcept;

    /// Returns true if init() succeeded and HRTF is active.
    [[nodiscard]] bool is_hrtf_active() const noexcept;

    /// Returns true if init() was called and the device is open
    /// (regardless of HRTF availability — allows graceful degradation).
    [[nodiscard]] bool is_initialized() const noexcept;

    // -------------------------------------------------------------------------
    // Source management
    // -------------------------------------------------------------------------

    /// Register a spatial sound source.  Returns the assigned HrtfSourceId.
    /// id        : caller-assigned logical id (matches SoundSource::id).
    /// position  : world-space [x, y, z] metres.
    ///
    /// Returns 0 if the HrtfMixer is not initialized or source creation fails.
    [[nodiscard]] HrtfSourceId add_hrtf_source(
        uint64_t                 logical_id,
        std::array<float, 3>     position) noexcept;

    /// Remove and destroy a previously registered source.  No-op if not found.
    void remove_hrtf_source(HrtfSourceId id) noexcept;

    /// Update the 3D pose of a source.  No-op if id is unknown.
    void set_source_pose(
        HrtfSourceId             id,
        std::array<float, 3>     position,
        std::array<float, 3>     velocity = { 0.0F, 0.0F, 0.0F }) noexcept;

    // -------------------------------------------------------------------------
    // PCM push
    // -------------------------------------------------------------------------

    /// Push interleaved PCM frames to a source's streaming buffer queue.
    /// The data is copied immediately — the caller may free it after return.
    /// format    : sample_rate, channel_count, is_float32
    /// data      : raw PCM bytes (float32 or int16 per format)
    /// byte_count: total bytes (NOT frame count)
    ///
    /// Returns true if the data was accepted.  Returns false if the source is
    /// unknown or the HrtfMixer is not initialized.
    bool submit_pcm(
        HrtfSourceId             id,
        const PcmFormat&         format,
        const void*              data,
        std::size_t              byte_count) noexcept;

    // -------------------------------------------------------------------------
    // Listener
    // -------------------------------------------------------------------------

    /// Update the listener pose.
    /// position         : world-space [x, y, z] metres.
    /// orientation_quat : unit quaternion [x, y, z, w] (right-handed, -Z forward).
    /// velocity         : world-space [x, y, z] m/s.
    void update_listener(
        std::array<float, 3>     position,
        std::array<float, 4>     orientation_quat,
        std::array<float, 3>     velocity = { 0.0F, 0.0F, 0.0F }) noexcept;

    // -------------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------------

    /// Returns the name of the audio device opened by init(), or "" if none.
    [[nodiscard]] std::string device_name() const noexcept;

    /// Returns the number of currently registered HRTF sources.
    [[nodiscard]] std::size_t source_count() const noexcept;

private:
    // -------------------------------------------------------------------------
    // Internal helpers — implemented in HrtfMixer.cpp (no AL types in header)
    // -------------------------------------------------------------------------

    /// Derive OpenAL forward + up vectors from a quaternion.
    /// forward = rotate(q, {0,0,-1}); up = rotate(q, {0,1,0}).
    static void quat_to_al_orientation(
        std::array<float, 4>     q,
        std::array<float, 3>&    out_forward,
        std::array<float, 3>&    out_up) noexcept;

    /// Rotate a 3-vector by a unit quaternion.
    [[nodiscard]] static std::array<float, 3> rotate_by_quat(
        std::array<float, 3>     v,
        std::array<float, 4>     q) noexcept;

    // -------------------------------------------------------------------------
    // OpenAL device / context — erased as void* in the public header so that
    // downstream TUs do NOT need to include AL/al.h / AL/alc.h.
    // The .cpp casts them back to ALCdevice* / ALCcontext* internally.
    // In the no-openal build (CD_AUDIO_SPATIAL_HAS_OPENAL == 0) they stay null.
    // -------------------------------------------------------------------------
    void* device_  { nullptr };  // ALCdevice*
    void* context_ { nullptr };  // ALCcontext*

    bool         initialized_  { false };
    bool         hrtf_active_  { false };
    std::string  device_name_;

    // -------------------------------------------------------------------------
    // Source table — AL source handles erased as uint32_t to avoid AL/al.h
    // in the public header.
    // -------------------------------------------------------------------------
    struct SourceEntry
    {
        uint64_t  logical_id { 0 };
        uint32_t  al_source  { 0 };  // ALuint — zero means unallocated
    };

    std::unordered_map<HrtfSourceId, SourceEntry> sources_;
    HrtfSourceId next_id_ { 1 };
};

}  // namespace cd::audio::spatial
