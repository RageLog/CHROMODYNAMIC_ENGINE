// =============================================================================
// CHROMODYNAMIC — cd/audio/IAudioBackend.hpp
// Phase 4 / Sprint S4.5 — audio backend abstraction.
//
// Single-clip play / stop / volume contract; concrete backends (Wasapi,
// CoreAudio, ALSA, PortAudio, miniaudio) plug into it via the factory.
// The built-in implementation is a null backend that records calls into a
// log — useful for tests and headless tools, and a safe default when the
// host has no audio hardware.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Handle.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace cd::audio
{

namespace audio_errors
{
inline constexpr std::uint32_t kDomain = 0x000F;
enum class Code : std::uint32_t
{
    kOk = 0,
    kInvalidArgument = 1,
    kUnknownClip = 2,
    kBackendError = 3,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace audio_errors

struct ClipTag
{
};

using ClipHandle = cd::core::Handle<ClipTag>;

struct VoiceTag
{
};

/// One "play instance" of a clip. Each `play(clip)` returns a fresh voice
/// so multiple overlapping playbacks of the same clip are first-class.
using VoiceHandle = cd::core::Handle<VoiceTag>;

/// Phase 157 — continuous-push audio stream. Differs from a Clip+Voice
/// pair in that the caller feeds PCM samples in real time rather than
/// uploading the whole sound up-front. Use for microphone, network
/// voice, procedural synthesis, or any non-precomputed source.
struct StreamTag
{
};
using StreamHandle = cd::core::Handle<StreamTag>;

/// PCM clip description. The MVP accepts interleaved float samples in [-1, 1].
struct ClipDesc
{
    std::span<const float> samples {};
    std::uint32_t channels { 1 };
    std::uint32_t sample_rate { 48000 };
};

class IAudioBackend
{
public:
    IAudioBackend() noexcept = default;
    virtual ~IAudioBackend() = default;
    IAudioBackend(const IAudioBackend&) = delete;
    IAudioBackend& operator=(const IAudioBackend&) = delete;
    IAudioBackend(IAudioBackend&&) = delete;
    IAudioBackend& operator=(IAudioBackend&&) = delete;

    // ---- Clip lifecycle ------------------------------------------------

    [[nodiscard]] virtual cd::core::Result<ClipHandle> create_clip(const ClipDesc& desc) = 0;
    virtual void destroy_clip(ClipHandle h) = 0;
    [[nodiscard]] virtual std::size_t clip_count() const noexcept = 0;

    // ---- Playback ------------------------------------------------------

    [[nodiscard]] virtual cd::core::Result<VoiceHandle>
    play(ClipHandle clip, float volume = 1.0F, bool looping = false) = 0;
    virtual void stop(VoiceHandle voice) = 0;
    virtual void set_volume(VoiceHandle voice, float volume) = 0;
    [[nodiscard]] virtual bool is_playing(VoiceHandle voice) const noexcept = 0;
    [[nodiscard]] virtual std::size_t voice_count() const noexcept = 0;

    // ---- Master / mixer ------------------------------------------------

    virtual void set_master_volume(float v) noexcept = 0;
    [[nodiscard]] virtual float master_volume() const noexcept = 0;

    // ---- Phase 157 — push-stream API -----------------------------------
    //
    // Continuous PCM stream. Caller obtains a handle via
    // `create_stream(...)`, then feeds float samples in real time via
    // `push_stream_samples(...)`. The backend pulls from the queue at
    // the device tick rate; if the queue underruns the backend emits
    // silence (no crash, no resync). Destroy on exit.
    //
    // The default no-op overrides let backends that don't support
    // streaming (file-sink, null) compile + report kNotImplemented at
    // runtime — matches the IDevice RT default pattern.

    [[nodiscard]] virtual cd::core::Result<StreamHandle>
    create_stream(std::uint32_t /*channels*/,
                  std::uint32_t /*sample_rate*/,
                  float /*volume*/ = 1.0F)
    {
        return std::unexpected(audio_errors::make(
            audio_errors::Code::kBackendError,
            "create_stream: backend has no push-stream implementation"));
    }

    [[nodiscard]] virtual cd::core::Result<void>
    push_stream_samples(StreamHandle /*stream*/, std::span<const float> /*samples*/)
    {
        return std::unexpected(audio_errors::make(
            audio_errors::Code::kBackendError,
            "push_stream_samples: backend has no push-stream implementation"));
    }

    virtual void destroy_stream(StreamHandle /*stream*/) {}

    /// Pending PCM frames waiting in the stream's queue. 0 means the
    /// next mix tick will emit silence. Useful for back-pressure.
    [[nodiscard]] virtual std::size_t
    stream_pending_frames(StreamHandle /*stream*/) const noexcept { return 0; }

    [[nodiscard]] virtual std::size_t stream_count() const noexcept { return 0; }
};

/// Null backend: accepts everything, plays nothing. Returns deterministic
/// handles so unit tests can verify call patterns.
[[nodiscard]] std::unique_ptr<IAudioBackend> make_null_audio_backend();

}  // namespace cd::audio
