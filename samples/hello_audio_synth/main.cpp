// =============================================================================
// CHROMODYNAMIC — samples/hello_audio_synth
//
// Headless audio pipeline demo. Synthesizes a 440 Hz sine wave at 44100 Hz
// mono 16-bit PCM, encodes a minimal RIFF/WAVE byte stream, writes it to
// disk, then loads it back through cd::asset_wav and asserts the
// round-trip is bit-identical.
//
// No platform audio output (deferred to cd::audio integration). The point
// here is to prove the cook/load contract end-to-end so an audio sample
// that DOES go to the speakers can rely on cd::asset_wav.
// =============================================================================
#include <cd/asset_wav/Wav.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <vector>

namespace
{

constexpr std::uint32_t kSampleRate = 44100;
constexpr float        kFreqHz      = 440.0F;
constexpr float        kDurationSec = 0.25F;

/// Append a little-endian integer of N bytes to `bytes`.
template <class T>
void push_le(std::vector<std::byte>& bytes, T value, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)
        bytes.push_back(std::byte { static_cast<unsigned char>((static_cast<std::uint64_t>(value) >> (i * 8u)) & 0xFFu) });
}

void push_tag(std::vector<std::byte>& bytes, const char (&tag)[5])
{
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::byte>(tag[i]));
}

/// Build a minimal mono s16 PCM WAV byte stream from `samples` (frame
/// count = samples.size()).
std::vector<std::byte> encode_wav_mono_s16(const std::vector<std::int16_t>& samples)
{
    const std::uint32_t data_size = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const std::uint32_t fmt_size  = 16;
    const std::uint32_t riff_size = 4 + 8 + fmt_size + 8 + data_size;

    std::vector<std::byte> bytes;
    bytes.reserve(8 + riff_size);
    push_tag(bytes, "RIFF");
    push_le(bytes, riff_size, 4);
    push_tag(bytes, "WAVE");

    push_tag(bytes, "fmt ");
    push_le(bytes, fmt_size, 4);
    push_le(bytes, 1u, 2);                  // PCM
    push_le(bytes, 1u, 2);                  // mono
    push_le(bytes, kSampleRate, 4);         // sample rate
    push_le(bytes, kSampleRate * 1u * 2u, 4); // byte rate
    push_le(bytes, 2u, 2);                  // block align
    push_le(bytes, 16u, 2);                 // bits per sample

    push_tag(bytes, "data");
    push_le(bytes, data_size, 4);
    bytes.insert(bytes.end(),
                 reinterpret_cast<const std::byte*>(samples.data()),
                 reinterpret_cast<const std::byte*>(samples.data() + samples.size()));
    return bytes;
}

}  // namespace

int main()
{
    std::printf("=== hello_audio_synth — sine -> WAV -> cd::asset_wav ===\n");

    // 1. Synthesize a 440 Hz sine, 250 ms.
    const auto frame_count = static_cast<std::size_t>(kSampleRate * kDurationSec);
    std::vector<std::int16_t> samples;
    samples.reserve(frame_count);
    const float w = 2.0F * 3.14159265358979F * kFreqHz / static_cast<float>(kSampleRate);
    for (std::size_t i = 0; i < frame_count; ++i)
    {
        const float s = std::sin(static_cast<float>(i) * w);
        samples.push_back(static_cast<std::int16_t>(s * 30000.0F));
    }

    // 2. Encode + write to disk.
    const auto bytes = encode_wav_mono_s16(samples);
    const std::string path = "hello_audio_synth_out.wav";
    {
        std::ofstream f { path, std::ios::binary | std::ios::trunc };
        if (!f)
        {
            std::printf("error: cannot open %s for writing\n", path.c_str());
            return 1;
        }
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    std::printf("wrote %zu bytes -> %s\n", bytes.size(), path.c_str());

    // 3. Load back via cd::asset_wav.
    auto r = cd::asset_wav::load(path);
    if (!r)
    {
        std::printf("load failed: %.*s\n",
            static_cast<int>(r.error().message.size()), r.error().message.data());
        return 2;
    }
    const auto& w_out = *r;

    // 4. Assert round-trip.
    bool ok = true;
    if (w_out.channels != 1u)        { std::printf("channels mismatch (%u)\n", w_out.channels); ok = false; }
    if (w_out.sample_rate != kSampleRate) { std::printf("sample_rate mismatch\n"); ok = false; }
    if (w_out.bits_per_sample != 16u) { std::printf("bits mismatch\n"); ok = false; }
    if (w_out.frame_count() != frame_count) {
        std::printf("frame_count mismatch (%zu vs %zu)\n", w_out.frame_count(), frame_count);
        ok = false;
    }
    if (ok && w_out.samples.size() == samples.size() * sizeof(std::int16_t))
    {
        if (std::memcmp(w_out.samples.data(), samples.data(), w_out.samples.size()) != 0)
        {
            std::printf("sample payload mismatch\n");
            ok = false;
        }
    }
    else if (ok)
    {
        std::printf("byte length mismatch\n");
        ok = false;
    }

    if (!ok)
        return 3;

    std::printf(
        "[hello_audio_synth] OK | %u Hz, %.3f s, %zu samples, %.2f s duration\n",
        kSampleRate,
        static_cast<double>(kDurationSec),
        w_out.frame_count(),
        w_out.duration_seconds()
    );
    return 0;
}
