// =============================================================================
// CHROMODYNAMIC — cd/audio/FileSinkBackend.hpp
//
// Offline mixer backend. Runs the full IAudioBackend voice state machine
// (real mixing, real volume, real looping), but instead of pushing samples
// to a sound card it accumulates them in memory and writes a RIFF/WAVE
// file on `render_to_file()`. Lets the audio pipeline ship as a fully
// audible deliverable (the WAV plays in any system audio player) without
// the COM / WASAPI / CoreAudio / ALSA platform-output complexity.
//
// The "platform output" backend (WASAPI / WinMM / DSound) is deferred to
// a follow-up sprint where the engine grows a dedicated audio thread
// (Phase 4 closure ADR S4.5.b).
// =============================================================================
#pragma once

#include <cd/audio/IAudioBackend.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

namespace cd::audio
{

class IFileSinkBackend : public IAudioBackend
{
public:
    /// Advance the mixer by `samples_per_channel` frames. Voices play
    /// (or finish) for that many frames; the rendered samples accumulate
    /// in an internal buffer.
    virtual void render(std::uint32_t samples_per_channel) = 0;

    /// Write everything rendered so far to `path` as a 16-bit interleaved
    /// PCM WAV file. Returns kBackendError on I/O failure.
    [[nodiscard]] virtual cd::core::Result<void>
    write_wav(std::string_view path) const = 0;

    /// Total rendered samples (per channel) since construction.
    [[nodiscard]] virtual std::uint32_t rendered_frames() const noexcept = 0;
};

/// Build an offline file-sink backend. `sample_rate` (default 48000) and
/// `channels` (default 2) determine the rendered WAV format.
[[nodiscard]] std::unique_ptr<IFileSinkBackend>
make_file_sink_audio_backend(std::uint32_t sample_rate = 48000,
                             std::uint32_t channels = 2);

}  // namespace cd::audio
