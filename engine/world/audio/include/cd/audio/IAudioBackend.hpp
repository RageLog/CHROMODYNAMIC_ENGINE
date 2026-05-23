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
};

/// Null backend: accepts everything, plays nothing. Returns deterministic
/// handles so unit tests can verify call patterns.
[[nodiscard]] std::unique_ptr<IAudioBackend> make_null_audio_backend();

}  // namespace cd::audio
