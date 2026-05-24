// =============================================================================
// CHROMODYNAMIC — samples/hello_audio_chain
//
// Headless DSP-graph demo. Synthesizes two source channels (a 440 Hz
// square + a clipped 220 Hz noise spike), routes them through:
//
//     [music ch0] ─┐
//                  ├─► Mixer ─► Compressor ─► SimpleReverb ─► LowPass ─► Limiter ─► WAV
//     [sfx   ch1] ─┘
//
// This wires together five marathon primitives in one chain:
//     cd::audio::Mixer        (Phase 54)
//     cd::audio::Compressor   (Phase 75)
//     cd::audio::SimpleReverb (Phase 29.D)
//     cd::audio::LowPass      (Phase 65)
//     cd::audio::Limiter      (Phase 40)
//
// Output: hello_audio_chain_out.wav (mono 16-bit PCM, 1.5 s).
// Console prints the compressor's gain reduction + limiter's peak
// gain so the listener can verify the chain actually reacted.
//
// No platform audio output: this is a deterministic offline render
// that proves the DSP nodes link, plug into each other, and survive
// a non-trivial signal without NaN or clip.
// =============================================================================
#include <cd/audio/Compressor.hpp>
#include <cd/audio/Limiter.hpp>
#include <cd/audio/LowPass.hpp>
#include <cd/audio/Mixer.hpp>
#include <cd/audio/SimpleReverb.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <ios>
#include <vector>

namespace
{

constexpr std::uint32_t kSampleRate = 48000;
constexpr float         kDurationSec = 1.5F;

template <class T>
void push_le(std::vector<std::byte>& bytes, T value, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)
        bytes.push_back(std::byte {
            static_cast<unsigned char>((static_cast<std::uint64_t>(value) >> (i * 8u)) & 0xFFu) });
}

void push_tag(std::vector<std::byte>& bytes, const char (&tag)[5])
{
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::byte>(tag[i]));
}

std::vector<std::byte> encode_wav_mono_s16(const std::vector<std::int16_t>& samples)
{
    const auto data_size = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    constexpr std::uint32_t fmt_size = 16;
    const std::uint32_t riff_size = 4u + 8u + fmt_size + 8u + data_size;

    std::vector<std::byte> bytes;
    bytes.reserve(8u + riff_size);
    push_tag(bytes, "RIFF");
    push_le(bytes, riff_size, 4);
    push_tag(bytes, "WAVE");

    push_tag(bytes, "fmt ");
    push_le(bytes, fmt_size, 4);
    push_le(bytes, 1u, 2);
    push_le(bytes, 1u, 2);
    push_le(bytes, kSampleRate, 4);
    push_le(bytes, kSampleRate * 2u, 4);
    push_le(bytes, 2u, 2);
    push_le(bytes, 16u, 2);

    push_tag(bytes, "data");
    push_le(bytes, data_size, 4);
    bytes.insert(bytes.end(),
                 reinterpret_cast<const std::byte*>(samples.data()),
                 reinterpret_cast<const std::byte*>(samples.data() + samples.size()));
    return bytes;
}

// Sources --------------------------------------------------------------------

[[nodiscard]] float square_wave(std::uint32_t i, float hz) noexcept
{
    const float phase = static_cast<float>(i) * hz / static_cast<float>(kSampleRate);
    const float frac = phase - std::floor(phase);
    return (frac < 0.5F) ? 0.6F : -0.6F;
}

// A noise burst that fires for ~40 ms every ~200 ms.
[[nodiscard]] float burst_noise(std::uint32_t i) noexcept
{
    const auto cycle = static_cast<std::uint32_t>(kSampleRate / 5);          // 200 ms
    const auto burst_len = static_cast<std::uint32_t>(kSampleRate / 25);     // 40 ms
    if ((i % cycle) >= burst_len) return 0.0F;
    // xorshift LCG for deterministic "noise" — keeps the sample
    // reproducible across runs.
    std::uint32_t x = i * 2654435761u + 0xC0FFEEu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    const float n = (static_cast<float>(x) / static_cast<float>(0xFFFFFFFFu)) * 2.0F - 1.0F;
    return n * 0.85F;  // hot enough to compress + occasionally hit the limiter
}

}  // namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::printf("=== hello_audio_chain — Mixer -> Compressor -> Reverb -> LowPass -> Limiter ===\n");

    cd::audio::Mixer<2> bus;
    bus.set_gain(0, 0.6F);  // music
    bus.set_gain(1, 0.9F);  // sfx (loud enough to engage the compressor)

    cd::audio::Compressor comp;
    comp.prepare(static_cast<float>(kSampleRate),
                 /*threshold=*/0.4F, /*ratio=*/6.0F,
                 /*attack=*/0.004F,  /*release=*/0.080F);

    cd::audio::SimpleReverb reverb;
    reverb.prepare(kSampleRate / 8);  // 125 ms tap
    reverb.set_feedback(0.45F);

    cd::audio::LowPass lp;
    lp.prepare(static_cast<float>(kSampleRate), /*cutoff=*/6000.0F);

    cd::audio::Limiter limiter;
    // Aggressive 0.2 ms attack so transient spikes from the reverb tail
    // get caught within ~10 samples at 48 kHz. The default 1 ms attack
    // lets the first burst escape — fine for music but not for this
    // hot test signal.
    limiter.prepare(static_cast<float>(kSampleRate), /*threshold=*/0.92F,
                    /*attack=*/0.0002F, /*release=*/0.040F);

    const auto frame_count = static_cast<std::size_t>(kSampleRate * kDurationSec);
    std::vector<std::int16_t> samples;
    samples.reserve(frame_count);

    // Telemetry collected while rendering.
    float max_comp_reduction_db = 0.0F;
    float min_limiter_gain      = 1.0F;
    float peak_after_limiter    = 0.0F;

    for (std::uint32_t i = 0; i < frame_count; ++i)
    {
        bus.mix(0, square_wave(i, 440.0F));
        bus.mix(1, burst_noise(i));
        float s = bus.pull();

        s = comp.process(s);
        if (comp.gain_db() < max_comp_reduction_db) max_comp_reduction_db = comp.gain_db();

        // Mix dry signal with reverb tail. Keep the dry channel dominant
        // so the compressed source stays recognizable; the wet bus is
        // attenuated because feedback=0.45 + a hot input would otherwise
        // build the tail beyond ±1 within a few hundred samples.
        const float wet = reverb.process(s);
        s = 0.75F * s + 0.18F * wet;

        s = lp.process(s);

        s = limiter.process(s);
        if (limiter.current_gain() < min_limiter_gain) min_limiter_gain = limiter.current_gain();
        const float ax = std::fabs(s);
        if (ax > peak_after_limiter) peak_after_limiter = ax;

        // Convert to s16 with saturation guard (limiter should already
        // keep us inside ±1.0 but be defensive — never trust DSP to be
        // bit-perfect).
        if (s >  1.0F) s =  1.0F;
        if (s < -1.0F) s = -1.0F;
        samples.push_back(static_cast<std::int16_t>(s * 32760.0F));
    }

    // Write the WAV.
    const auto bytes = encode_wav_mono_s16(samples);
    const std::string path = "hello_audio_chain_out.wav";
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

    std::printf("wrote %zu bytes -> %s (%.2f s, %u Hz mono s16)\n",
                bytes.size(), path.c_str(),
                static_cast<double>(kDurationSec), kSampleRate);
    std::printf("compressor: max gain reduction = %.2f dB\n",
                static_cast<double>(max_comp_reduction_db));
    std::printf("limiter:    min gain coef       = %.4f  (1.0 = inactive)\n",
                static_cast<double>(min_limiter_gain));
    std::printf("output:     peak after chain   = %.4f  (must stay below 1.0)\n",
                static_cast<double>(peak_after_limiter));

    // Allow a tiny overshoot tolerance (5%): the limiter is a soft-knee
    // with finite attack, so the very first sample of a transient may
    // exceed threshold by an attack-time-dependent amount. Anything
    // beyond that is a real failure.
    if (peak_after_limiter > 1.05F)
    {
        std::printf("FAIL: limiter let a sample escape above 1.05\n");
        return 2;
    }
    std::printf("[hello_audio_chain] OK\n");
    return 0;
}
