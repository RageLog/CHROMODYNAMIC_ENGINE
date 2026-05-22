// =============================================================================
// CHROMODYNAMIC — cd/asset_wav/Wav.hpp
//
// Minimal RIFF/WAVE PCM loader. Reads:
//   * Chunk header: "RIFF" + size + "WAVE"
//   * "fmt " sub-chunk: format (1 = PCM, 3 = IEEE float), channels, sample
//     rate, byte rate, block align, bits per sample
//   * "data" sub-chunk: raw samples (interleaved L,R,L,R,... for stereo)
//
// Out of scope (v1):
//   * Compressed WAV (ADPCM, μ-law, A-law)
//   * Multi-data-chunk files
//   * WAVE_FORMAT_EXTENSIBLE (uses GUID for codec; rare for game audio)
//   * Cue / playlist / list chunks (we skip non-fmt/data chunks silently)
//
// The output is a `Wav` struct with the raw PCM bytes plus channel/rate
// metadata. The audio system (cd::audio) does its own sample-format
// conversion (s16→f32 for the mixer), so we don't decode here — keep the
// loader cheap and the engine pipeline reads `bytes_per_sample` to know
// what to do.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>
#include <cd/core/ErrorCode.hpp>
#include <cd/core/Result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cd::asset_wav
{

namespace wav_errors
{
inline constexpr std::uint32_t kDomain = 0x0014;

enum class Code : std::uint32_t
{
    kOk = 0,
    kFileNotFound = 1,
    kIoError = 2,
    kMagicMismatch = 3,
    kCorrupt = 4,
    kUnsupportedFormat = 5,
    kInvalidArgument = 6,
};

[[nodiscard]] inline cd::core::ErrorCode make(Code c, std::string_view m = {}) noexcept
{
    return cd::core::ErrorCode { kDomain, static_cast<std::uint32_t>(c), m };
}
}  // namespace wav_errors

enum class SampleFormat : std::uint16_t
{
    kPcmInt = 1,    ///< Linear integer PCM. Bit depth in `bits_per_sample`.
    kIeeeFloat = 3, ///< IEEE 754 float; `bits_per_sample` is always 32 for f32 or 64 for f64.
};

struct Wav
{
    SampleFormat format { SampleFormat::kPcmInt };
    /// 1 = mono, 2 = stereo. Higher channel counts (4/6/8) accepted; the
    /// audio engine downmixes if needed.
    std::uint16_t channels { 0 };
    /// Hz — e.g. 44'100, 48'000, 96'000.
    std::uint32_t sample_rate { 0 };
    /// Per sample, NOT per frame. Stereo s16 has 16 bits_per_sample, frame is 4 bytes.
    std::uint16_t bits_per_sample { 0 };
    /// `data` chunk verbatim: interleaved samples. Caller knows the format
    /// from the metadata above and can reinterpret as needed.
    std::vector<std::byte> samples;

    /// Convenience: total number of frames (one frame = `channels`
    /// samples). Computed from `samples.size()` and the metadata.
    [[nodiscard]] std::size_t frame_count() const noexcept
    {
        const auto bytes_per_frame
            = static_cast<std::size_t>(channels) * (bits_per_sample / 8u);
        if (bytes_per_frame == 0)
            return 0;
        return samples.size() / bytes_per_frame;
    }

    [[nodiscard]] double duration_seconds() const noexcept
    {
        if (sample_rate == 0)
            return 0.0;
        return static_cast<double>(frame_count()) / static_cast<double>(sample_rate);
    }
};

/// Load a .wav file from disk. Returns Wav on success or a typed error.
[[nodiscard]] cd::core::Result<Wav> load(std::string_view path);

/// Decode a WAV file already in memory (e.g. from cd::vfs).
[[nodiscard]] cd::core::Result<Wav> decode(const std::byte* data, std::size_t size);

}  // namespace cd::asset_wav
