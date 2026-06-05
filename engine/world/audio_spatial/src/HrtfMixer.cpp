// =============================================================================
// CHROMODYNAMIC — cd/audio/spatial/HrtfMixer.cpp
// Phase 751 — cd::audio::spatial Sprint-2: OpenAL Soft HRTF convolution bridge.
//
// OpenAL Soft HRTF path (CD_AUDIO_SPATIAL_HAS_OPENAL == 1):
//   - ALC_HRTF_SOFT extension: enables the built-in HRTF dataset (44.1/48 kHz).
//   - Each source is AL_SOURCE_RELATIVE = AL_FALSE so OpenAL spatialises it
//     against the listener in world-space.
//   - PCM is fed via buffer queuing (standard AL buffer queue protocol).
//   - Listener orientation forwarded from quaternion each update_listener() call.
//
// AL type isolation: AL/al.h, AL/alc.h, and AL/alext.h are included here only.
//   The public header stores device/context as void* and al_source as uint32_t
//   to keep downstream TUs clean.  We cast back in this TU.
//
// Fallback (CD_AUDIO_SPATIAL_HAS_OPENAL == 0):
//   All methods are compiled as safe no-ops.  SpatialMixer Sprint-1 ILD/Doppler
//   gains remain the operative spatialization path.
// =============================================================================

#include <cd/audio/spatial/HrtfMixer.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// AL includes — ONLY in this TU, never in the public header.
// ---------------------------------------------------------------------------
#if CD_AUDIO_SPATIAL_HAS_OPENAL
#  include <AL/al.h>
#  include <AL/alc.h>
#  include <AL/alext.h>
#endif

namespace cd::audio::spatial
{

// ---------------------------------------------------------------------------
// Convenience casts — device_/context_ stored as void* in header to avoid
// AL types leaking into downstream TUs.
// ---------------------------------------------------------------------------
#if CD_AUDIO_SPATIAL_HAS_OPENAL
static inline ALCdevice*  as_device (void* p) noexcept { return static_cast<ALCdevice*>(p);  }
static inline ALCcontext* as_context(void* p) noexcept { return static_cast<ALCcontext*>(p); }
#endif

// =============================================================================
// Destructor
// =============================================================================

HrtfMixer::~HrtfMixer() noexcept
{
    shutdown();
}

// =============================================================================
// init() — open device + context + enable HRTF
// =============================================================================

bool HrtfMixer::init(const std::optional<std::string>& preferred_device) noexcept
{
#if CD_AUDIO_SPATIAL_HAS_OPENAL
    if (initialized_)
        return hrtf_active_;

    // -------------------------------------------------------------------------
    // 1. Open device
    // -------------------------------------------------------------------------
    const char* dev_name_cstr =
        (preferred_device.has_value() && !preferred_device->empty())
            ? preferred_device->c_str()
            : nullptr;  // nullptr = system default

    auto* raw_device = alcOpenDevice(dev_name_cstr);
    if (raw_device == nullptr)
        return false;  // no device — caller should degrade to SpatialMixer

    device_ = raw_device;

    // Record the actual device name for diagnostics.
    const ALCchar* actual = alcGetString(raw_device, ALC_DEVICE_SPECIFIER);
    device_name_ = (actual != nullptr) ? actual : "(unknown)";

    // -------------------------------------------------------------------------
    // 2. Probe HRTF extension
    // -------------------------------------------------------------------------
    const bool has_hrtf_ext =
        alcIsExtensionPresent(raw_device, "ALC_SOFT_HRTF") == AL_TRUE;

    // -------------------------------------------------------------------------
    // 3. Create context — request HRTF if supported
    // -------------------------------------------------------------------------
    ALCcontext* raw_context = nullptr;
    if (has_hrtf_ext)
    {
        // ALC_HRTF_SOFT = ALC_TRUE requests the HRTF renderer at context
        // creation time.  The driver may still refuse (e.g. unsupported rate).
        const ALCint attribs[] = {
            ALC_HRTF_SOFT, ALC_TRUE,
            0
        };
        raw_context = alcCreateContext(raw_device, attribs);
    }
    else
    {
        raw_context = alcCreateContext(raw_device, nullptr);
    }

    if (raw_context == nullptr)
    {
        alcCloseDevice(raw_device);
        device_ = nullptr;
        return false;
    }

    context_ = raw_context;
    alcMakeContextCurrent(raw_context);

    // -------------------------------------------------------------------------
    // 4. Verify HRTF actually activated
    // -------------------------------------------------------------------------
    if (has_hrtf_ext)
    {
        ALCint hrtf_status = ALC_FALSE;
        alcGetIntegerv(raw_device, ALC_HRTF_STATUS_SOFT, 1, &hrtf_status);
        hrtf_active_ = (hrtf_status == ALC_HRTF_ENABLED_SOFT);
    }

    initialized_ = true;
    return true;

#else
    // Suppress unused-parameter warning in no-openal build.
    (void)preferred_device;
    return false;
#endif
}

// =============================================================================
// shutdown()
// =============================================================================

void HrtfMixer::shutdown() noexcept
{
#if CD_AUDIO_SPATIAL_HAS_OPENAL
    if (!initialized_)
        return;

    // Delete all AL sources.
    for (auto& [id, entry] : sources_)
    {
        auto al_src = static_cast<ALuint>(entry.al_source);
        alSourceStop(al_src);
        alDeleteSources(1, &al_src);
    }
    sources_.clear();

    alcMakeContextCurrent(nullptr);

    if (context_ != nullptr)
    {
        alcDestroyContext(as_context(context_));
        context_ = nullptr;
    }
    if (device_ != nullptr)
    {
        alcCloseDevice(as_device(device_));
        device_ = nullptr;
    }

    initialized_ = false;
    hrtf_active_ = false;
#endif
}

// =============================================================================
// Queries
// =============================================================================

bool HrtfMixer::is_hrtf_active() const noexcept
{
    return hrtf_active_;
}

bool HrtfMixer::is_initialized() const noexcept
{
    return initialized_;
}

std::string HrtfMixer::device_name() const noexcept
{
    return device_name_;
}

std::size_t HrtfMixer::source_count() const noexcept
{
    return sources_.size();
}

// =============================================================================
// add_hrtf_source()
// =============================================================================

HrtfSourceId HrtfMixer::add_hrtf_source(
    uint64_t             logical_id,
    std::array<float, 3> position) noexcept
{
#if CD_AUDIO_SPATIAL_HAS_OPENAL
    if (!initialized_)
        return 0;

    ALuint al_src = 0;
    alGenSources(1, &al_src);
    if (alGetError() != AL_NO_ERROR)
        return 0;

    // World-space source: NOT relative to listener.
    alSourcei (al_src, AL_SOURCE_RELATIVE, AL_FALSE);
    alSource3f(al_src, AL_POSITION,
               position[0], position[1], position[2]);
    alSource3f(al_src, AL_VELOCITY,    0.0F, 0.0F, 0.0F);
    alSourcef (al_src, AL_GAIN,        1.0F);
    alSourcef (al_src, AL_PITCH,       1.0F);
    // Disable built-in distance rolloff — cd::audio::spatial SpatialMixer
    // already owns 1/r attenuation; we don't want double attenuation.
    alDistanceModel(AL_NONE);

    const HrtfSourceId handle = next_id_++;
    sources_[handle] = SourceEntry{ logical_id, static_cast<uint32_t>(al_src) };
    return handle;

#else
    (void)logical_id;
    (void)position;
    return 0;
#endif
}

// =============================================================================
// remove_hrtf_source()
// =============================================================================

void HrtfMixer::remove_hrtf_source(HrtfSourceId id) noexcept
{
#if CD_AUDIO_SPATIAL_HAS_OPENAL
    const auto it = sources_.find(id);
    if (it == sources_.end())
        return;

    auto al_src = static_cast<ALuint>(it->second.al_source);
    alSourceStop(al_src);
    alDeleteSources(1, &al_src);
    sources_.erase(it);
#else
    (void)id;
#endif
}

// =============================================================================
// set_source_pose()
// =============================================================================

void HrtfMixer::set_source_pose(
    HrtfSourceId         id,
    std::array<float, 3> position,
    std::array<float, 3> velocity) noexcept
{
#if CD_AUDIO_SPATIAL_HAS_OPENAL
    const auto it = sources_.find(id);
    if (it == sources_.end())
        return;

    const auto al_src = static_cast<ALuint>(it->second.al_source);
    alSource3f(al_src, AL_POSITION, position[0], position[1], position[2]);
    alSource3f(al_src, AL_VELOCITY, velocity[0], velocity[1], velocity[2]);
#else
    (void)id;
    (void)position;
    (void)velocity;
#endif
}

// =============================================================================
// submit_pcm()
// =============================================================================

bool HrtfMixer::submit_pcm(
    HrtfSourceId      id,
    const PcmFormat&  format,
    const void*       data,
    std::size_t       byte_count) noexcept
{
#if CD_AUDIO_SPATIAL_HAS_OPENAL
    if (!initialized_ || data == nullptr || byte_count == 0)
        return false;

    const auto it = sources_.find(id);
    if (it == sources_.end())
        return false;

    const auto al_src = static_cast<ALuint>(it->second.al_source);

    // Determine AL format.
    ALenum al_fmt = AL_FORMAT_MONO16;
    if (format.channel_count == 1)
    {
        al_fmt = format.is_float32 ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_MONO16;
    }
    else if (format.channel_count == 2)
    {
        al_fmt = format.is_float32 ? AL_FORMAT_STEREO_FLOAT32 : AL_FORMAT_STEREO16;
    }
    else
    {
        return false;  // unsupported channel layout
    }

    // Reclaim processed buffers from the queue.
    {
        ALint processed = 0;
        alGetSourcei(al_src, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0)
        {
            ALuint buf = 0;
            alSourceUnqueueBuffers(al_src, 1, &buf);
            alDeleteBuffers(1, &buf);
        }
    }

    // Create a new buffer, upload PCM, queue it.
    ALuint buf = 0;
    alGenBuffers(1, &buf);
    if (alGetError() != AL_NO_ERROR)
        return false;

    alBufferData(buf, al_fmt, data,
                 static_cast<ALsizei>(byte_count),
                 static_cast<ALsizei>(format.sample_rate));

    if (alGetError() != AL_NO_ERROR)
    {
        alDeleteBuffers(1, &buf);
        return false;
    }

    alSourceQueueBuffers(al_src, 1, &buf);

    // Auto-start playback if not already playing.
    ALint state = AL_STOPPED;
    alGetSourcei(al_src, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING)
        alSourcePlay(al_src);

    return alGetError() == AL_NO_ERROR;

#else
    (void)id;
    (void)format;
    (void)data;
    (void)byte_count;
    return false;
#endif
}

// =============================================================================
// update_listener()
// =============================================================================

void HrtfMixer::update_listener(
    std::array<float, 3> position,
    std::array<float, 4> orientation_quat,
    std::array<float, 3> velocity) noexcept
{
#if CD_AUDIO_SPATIAL_HAS_OPENAL
    if (!initialized_)
        return;

    alListener3f(AL_POSITION, position[0], position[1], position[2]);
    alListener3f(AL_VELOCITY, velocity[0], velocity[1], velocity[2]);

    // OpenAL wants [forward_x, forward_y, forward_z, up_x, up_y, up_z].
    std::array<float, 3> fwd {};
    std::array<float, 3> up  {};
    quat_to_al_orientation(orientation_quat, fwd, up);

    const ALfloat at_up[6] = {
        fwd[0], fwd[1], fwd[2],
        up[0],  up[1],  up[2]
    };
    alListenerfv(AL_ORIENTATION, at_up);
#else
    (void)position;
    (void)orientation_quat;
    (void)velocity;
#endif
}

// =============================================================================
// Static helpers
// =============================================================================

void HrtfMixer::quat_to_al_orientation(
    std::array<float, 4> q,
    std::array<float, 3>& out_forward,
    std::array<float, 3>& out_up) noexcept
{
    // Listener faces -Z (right-handed); up = +Y.
    // forward = rotate(q, {0,0,-1})
    // up      = rotate(q, {0,1, 0})
    out_forward = rotate_by_quat({ 0.0F,  0.0F, -1.0F }, q);
    out_up      = rotate_by_quat({ 0.0F,  1.0F,  0.0F }, q);
}

std::array<float, 3> HrtfMixer::rotate_by_quat(
    std::array<float, 3> v,
    std::array<float, 4> q) noexcept
{
    // Rodrigues' rotation formula (quaternion form):
    //   v' = v + 2w*(q_xyz × v) + 2*(q_xyz × (q_xyz × v))
    const float qx = q[0];
    const float qy = q[1];
    const float qz = q[2];
    const float qw = q[3];

    const float tx = 2.0F * (qy * v[2] - qz * v[1]);
    const float ty = 2.0F * (qz * v[0] - qx * v[2]);
    const float tz = 2.0F * (qx * v[1] - qy * v[0]);

    return {
        v[0] + qw * tx + (qy * tz - qz * ty),
        v[1] + qw * ty + (qz * tx - qx * tz),
        v[2] + qw * tz + (qx * ty - qy * tx)
    };
}

}  // namespace cd::audio::spatial
