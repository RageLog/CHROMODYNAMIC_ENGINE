// =============================================================================
// CHROMODYNAMIC — cd/asset/wav/Wav.cpp
// =============================================================================
#include <cd/asset/wav/Wav.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace cd::asset::wav
{

namespace
{

// Little-endian readers. WAV is always LE per the spec; using bit ops keeps
// the parser portable to BE hosts without conditional compilation.
[[nodiscard]] std::uint16_t read_u16_le(const std::byte* p) noexcept
{
    const auto* b = reinterpret_cast<const unsigned char*>(p);
    const auto lo = static_cast<std::uint16_t>(b[0]);
    const auto hi = static_cast<std::uint16_t>(b[1]);
    return static_cast<std::uint16_t>(lo | static_cast<std::uint16_t>(hi << 8));
}

[[nodiscard]] std::uint32_t read_u32_le(const std::byte* p) noexcept
{
    const auto* b = reinterpret_cast<const unsigned char*>(p);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

[[nodiscard]] bool fourcc_eq(const std::byte* p, const char (&tag)[5]) noexcept
{
    return std::memcmp(p, tag, 4) == 0;
}

}  // namespace

cd::core::Result<Wav> decode(const std::byte* data, std::size_t size)
{
    if (data == nullptr || size < 44u)
    {
        return std::unexpected(
            wav_errors::make(wav_errors::Code::kCorrupt, "wav: input too small for RIFF/fmt/data minimum (44 bytes)")
        );
    }

    // RIFF header
    if (!fourcc_eq(data, "RIFF"))
    {
        return std::unexpected(wav_errors::make(wav_errors::Code::kMagicMismatch, "wav: 'RIFF' fourcc missing"));
    }
    const auto riff_size = read_u32_le(data + 4);
    if (!fourcc_eq(data + 8, "WAVE"))
    {
        return std::unexpected(wav_errors::make(wav_errors::Code::kMagicMismatch, "wav: 'WAVE' fourcc missing"));
    }
    // riff_size counts everything after the size field itself (i.e. payload
    // including "WAVE" fourcc onwards). Total file should be 8 + riff_size.
    // Tolerate slight mismatches (some tools omit the trailing pad byte).
    if (riff_size > size)
    {
        return std::unexpected(wav_errors::make(wav_errors::Code::kCorrupt, "wav: RIFF size exceeds buffer"));
    }

    Wav out;
    bool have_fmt = false;
    bool have_data = false;

    // Walk sub-chunks: header (8 B) + payload, padded to even.
    std::size_t cursor = 12;
    while (cursor + 8 <= size)
    {
        const auto* tag = data + cursor;
        const auto chunk_size = read_u32_le(data + cursor + 4);
        const std::size_t payload_off = cursor + 8;
        if (payload_off + chunk_size > size)
        {
            return std::unexpected(wav_errors::make(wav_errors::Code::kCorrupt, "wav: chunk extends past buffer"));
        }

        if (fourcc_eq(tag, "fmt "))
        {
            if (chunk_size < 16)
            {
                return std::unexpected(
                    wav_errors::make(wav_errors::Code::kCorrupt, "wav: fmt chunk smaller than 16 bytes")
                );
            }
            const auto* fmt = data + payload_off;
            const auto fmt_code = read_u16_le(fmt + 0);
            out.channels = read_u16_le(fmt + 2);
            out.sample_rate = read_u32_le(fmt + 4);
            // bytes 8-11: byte_rate (channels * sample_rate * bits/8)
            // bytes 12-13: block_align (channels * bits/8)
            out.bits_per_sample = read_u16_le(fmt + 14);

            if (fmt_code == 1)
                out.format = SampleFormat::kPcmInt;
            else if (fmt_code == 3)
                out.format = SampleFormat::kIeeeFloat;
            else
            {
                // WAVE_FORMAT_EXTENSIBLE (0xFFFE) and codec-specific codes
                // (μ-law, A-law, ADPCM, MP3-in-WAV) all land here. v1
                // refuses; caller can transcode or pick a different format.
                return std::unexpected(
                    wav_errors::make(
                        wav_errors::Code::kUnsupportedFormat,
                        "wav: only PCM (1) and IEEE float (3) supported"
                    )
                );
            }
            if (out.channels == 0 || out.sample_rate == 0 || out.bits_per_sample == 0 ||
                (out.bits_per_sample % 8u) != 0u)
            {
                return std::unexpected(wav_errors::make(wav_errors::Code::kCorrupt, "wav: malformed fmt fields"));
            }
            have_fmt = true;
        }
        else if (fourcc_eq(tag, "data"))
        {
            if (!have_fmt)
            {
                return std::unexpected(
                    wav_errors::make(wav_errors::Code::kCorrupt, "wav: data chunk before fmt chunk")
                );
            }
            out.samples.resize(chunk_size);
            std::memcpy(out.samples.data(), data + payload_off, chunk_size);
            have_data = true;
        }
        // else: silently skip "LIST", "JUNK", "cue ", "PEAK", etc.

        // Advance with even-size padding per the RIFF spec.
        cursor = payload_off + chunk_size + (chunk_size & 1u);
    }

    if (!have_fmt)
    {
        return std::unexpected(wav_errors::make(wav_errors::Code::kCorrupt, "wav: no fmt chunk found"));
    }
    if (!have_data)
    {
        return std::unexpected(wav_errors::make(wav_errors::Code::kCorrupt, "wav: no data chunk found"));
    }
    return out;
}

cd::core::Result<Wav> load(std::string_view path)
{
    const std::string path_s { path };
    std::ifstream f { path_s, std::ios::binary | std::ios::ate };
    if (!f)
    {
        return std::unexpected(
            wav_errors::make(wav_errors::Code::kFileNotFound, std::string { "wav: cannot open " } + path_s)
        );
    }
    const auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    std::vector<std::byte> buf;
    buf.resize(size);
    if (!f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size)))
    {
        return std::unexpected(
            wav_errors::make(wav_errors::Code::kIoError, std::string { "wav: read failed for " } + path_s)
        );
    }
    return decode(buf.data(), buf.size());
}

}  // namespace cd::asset::wav
