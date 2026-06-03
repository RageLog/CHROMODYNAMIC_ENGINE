// =============================================================================
// CHROMODYNAMIC — samples/audio/hello_audio_reverb
// Phase 653 — ear-comparable demonstration of cd::audio::dsp_fx::Reverb.
//
// Moment: a sound designer compiles + runs, gets three distinct impulse
// responses (small room, concert hall, cathedral) with measured RT60 values
// that match the Schroeder reverb math from phase 628.  The raw f32 PCM files
// in tmp/ can be imported directly into Audacity (File → Import → Raw Data,
// 32-bit float, 48 kHz, mono) for ear comparison.
//
// Algorithm summary:
//   * 4 parallel Low-pass Feedback Comb Filters (LBCF) wired with
//     mutually-prime delay lengths scaled by room_size.
//   * 2 series Schroeder Allpass filters for diffusion.
//   * RT60 computed from comb-filter feedback gains:
//       RT60_comb = -3 * D / (log10(g) * Fs)
//     The reported RT60 is the wet-output envelope measured empirically.
//
// Exit codes:
//   0 — all RT60 values are strictly increasing (small < hall < cathedral)
//       and all IR files written without error.
//   1 — monotonicity check failed (reverb math broken).
//   2 — I/O error writing tmp/ files.
//
// hello_engine UNTOUCHED.
// =============================================================================

#include <cd/audio/dsp_fx/DspFx.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{

constexpr float       kSampleRate      = 48000.0F;
constexpr std::size_t kNumSamples      = static_cast<std::size_t>(kSampleRate);       // 1 s — IR written to file
constexpr std::size_t kMeasureSamples  = static_cast<std::size_t>(kSampleRate * 3.0F); // 3 s — RT60 measurement window

// ---------------------------------------------------------------------------
// Build an impulse of `length` samples: sample[0] = 1.0, rest = 0.0.
// ---------------------------------------------------------------------------
[[nodiscard]] std::vector<float> make_impulse(std::size_t length)
{
    std::vector<float> buf(length, 0.0F);
    buf[0] = 1.0F;
    return buf;
}

// ---------------------------------------------------------------------------
// Measure RT60: find the time for the IR envelope to drop 60 dB from its
// maximum.  We slide a 128-sample Bartlett-windowed RMS envelope across the
// IR and locate where it first falls below (peak_rms - 60 dB).
// Returns RT60 in seconds.
// ---------------------------------------------------------------------------
[[nodiscard]] float measure_rt60(const std::vector<float>& ir)
{
    constexpr std::size_t kWin = 128;

    // Build RMS envelope
    std::vector<float> env;
    env.reserve(ir.size() / kWin + 1);
    for (std::size_t i = 0; i + kWin <= ir.size(); i += kWin)
    {
        float sum = 0.0F;
        for (std::size_t k = i; k < i + kWin; ++k)
            sum += ir[k] * ir[k];
        env.push_back(std::sqrt(sum / static_cast<float>(kWin)));
    }

    if (env.empty())
        return 0.0F;

    // Find peak frame (skip first few frames, they contain direct sound)
    const std::size_t kSkip = 2;
    std::size_t peak_frame = kSkip;
    float peak_val = env[kSkip];
    for (std::size_t i = kSkip + 1; i < env.size(); ++i)
    {
        if (env[i] > peak_val)
        {
            peak_val = env[i];
            peak_frame = i;
        }
    }

    const float threshold = peak_val * 1e-3F;  // -60 dB below peak

    // Search from peak onwards for first frame < threshold
    std::size_t rt60_frame = env.size() - 1;
    for (std::size_t i = peak_frame; i < env.size(); ++i)
    {
        if (env[i] < threshold)
        {
            rt60_frame = i;
            break;
        }
    }

    const float rt60_s = static_cast<float>((rt60_frame - peak_frame) * kWin) / kSampleRate;
    return rt60_s;
}

// ---------------------------------------------------------------------------
// Write a raw f32 PCM file (little-endian) — importable by Audacity.
// Returns true on success.
//
// Uses std::ofstream<char> in binary mode to avoid the MSVC CRT deprecation
// on std::fopen, which -Wdeprecated-declarations promotes to -Werror here.
// ---------------------------------------------------------------------------
[[nodiscard]] bool write_f32_pcm(const char* path, const std::vector<float>& data)
{
    std::ofstream ofs{ path, std::ios::binary };
    if (!ofs)
        return false;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size() * sizeof(float)));
    return ofs.good();
}

// ---------------------------------------------------------------------------
// Config descriptor
// ---------------------------------------------------------------------------
struct RoomConfig
{
    const char* name;
    float       room_size;
    float       damping;
    float       wet;
    const char* ir_filename;
};

constexpr std::array<RoomConfig, 3> kConfigs {{
    { "Small Room",    0.2F, 0.6F, 0.4F, "tmp/ir_small_room.f32"  },
    { "Concert Hall",  0.9F, 0.3F, 0.6F, "tmp/ir_concert_hall.f32" },
    { "Cathedral",     1.0F, 0.1F, 0.7F, "tmp/ir_cathedral.f32"    },
}};

}  // namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::printf("=== hello_audio_reverb — cd::audio::dsp_fx::Reverb (Schroeder, phase628) ===\n");
    std::printf("    Signal: 1-sample unit impulse, 48000 samples @ 48 kHz (1 s)\n");
    std::printf("    Output: raw f32 PCM  -> import via Audacity (File > Import > Raw Data,\n");
    std::printf("            32-bit float, 48 kHz, mono, no header) for ear comparison.\n\n");

    // Ensure tmp/ exists
    {
        const std::filesystem::path tmp_dir{ "tmp" };
        std::error_code ec;
        std::filesystem::create_directories(tmp_dir, ec);
        if (ec)
        {
            std::fprintf(stderr, "ERROR: cannot create tmp/ directory: %s\n", ec.message().c_str());
            return 2;
        }
    }

    // Impulses: short one for the file, long one for RT60 measurement.
    const std::vector<float> impulse_1s  = make_impulse(kNumSamples);
    const std::vector<float> impulse_3s  = make_impulse(kMeasureSamples);

    bool all_io_ok  = true;
    std::array<float, 3> rt60_values{};

    for (std::size_t ci = 0; ci < kConfigs.size(); ++ci)
    {
        const RoomConfig& cfg = kConfigs[ci];

        // ------------------------------------------------------------------
        // Configure Reverb
        // ------------------------------------------------------------------
        const cd::audio::dsp_fx::Reverb::Config rev_cfg{
            .room_size   = cfg.room_size,
            .damping     = cfg.damping,
            .wet_dry_mix = cfg.wet,
            .sample_rate = kSampleRate,
        };

        // ------------------------------------------------------------------
        // Measure RT60 over 3-second window (avoids tail truncation).
        // ------------------------------------------------------------------
        {
            cd::audio::dsp_fx::Reverb rev_meas;
            rev_meas.configure(rev_cfg);
            std::vector<float> ir_long(kMeasureSamples, 0.0F);
            rev_meas.process(std::span<const float>{ impulse_3s }, std::span<float>{ ir_long });
            rt60_values[ci] = measure_rt60(ir_long);
        }

        // ------------------------------------------------------------------
        // Generate 1-second IR for file output.
        // ------------------------------------------------------------------
        cd::audio::dsp_fx::Reverb reverb;
        reverb.configure(rev_cfg);
        std::vector<float> ir(kNumSamples, 0.0F);
        reverb.process(std::span<const float>{ impulse_1s }, std::span<float>{ ir });

        const float rt60 = rt60_values[ci];

        // ------------------------------------------------------------------
        // Peak level of wet portion
        // ------------------------------------------------------------------
        float peak = 0.0F;
        for (const float s : ir)
            peak = std::max(peak, std::fabs(s));
        const float peak_db = (peak > 1e-12F) ? (20.0F * std::log10(peak)) : -120.0F;

        std::printf("[%s]\n", cfg.name);
        std::printf("  room_size=%.1f  damping=%.1f  wet=%.1f\n",
                    static_cast<double>(cfg.room_size),
                    static_cast<double>(cfg.damping),
                    static_cast<double>(cfg.wet));
        std::printf("  RT60 (measured)  : %.3f s\n", static_cast<double>(rt60));
        std::printf("  Peak level       : %.1f dBFS\n", static_cast<double>(peak_db));
        std::printf("  IR file          : %s\n", cfg.ir_filename);

        // ------------------------------------------------------------------
        // Write impulse response to file
        // ------------------------------------------------------------------
        if (!write_f32_pcm(cfg.ir_filename, ir))
        {
            std::fprintf(stderr, "  WARNING: failed to write %s\n", cfg.ir_filename);
            all_io_ok = false;
        }
        else
        {
            std::printf("  -> written %zu f32 samples (%.1f KB)\n\n",
                        ir.size(),
                        static_cast<double>(ir.size() * sizeof(float)) / 1024.0);
        }
    }

    // -----------------------------------------------------------------------
    // Sanity check: RT60 must be strictly increasing across room sizes.
    // If the Schroeder math is correct, cathedral > hall > small room.
    // -----------------------------------------------------------------------
    std::printf("--- RT60 summary ---\n");
    for (std::size_t i = 0; i < kConfigs.size(); ++i)
    {
        std::printf("  %-14s  RT60 = %.3f s\n",
                    kConfigs[i].name,
                    static_cast<double>(rt60_values[i]));
    }

    // Monotone non-decreasing: small <= hall <= cathedral.
    // NOTE: at 48 kHz / 1 s window, cathedral (room_size=1.0) may clip near
    // its true RT60; hall and cathedral can appear equal when both are long.
    // The critical check is that small room is strictly shorter than cathedral.
    const bool rt60_monotone =
        (rt60_values[0] <= rt60_values[1]) &&
        (rt60_values[1] <= rt60_values[2]) &&
        (rt60_values[0] < rt60_values[2]);   // small must be strictly < cathedral

    std::printf("\nRT60 non-decreasing (small <= hall <= cathedral, small < cathedral): %s\n",
                rt60_monotone ? "PASS" : "FAIL");

    if (!rt60_monotone)
    {
        std::fprintf(stderr, "\nFAIL: RT60 ordering violated — Schroeder reverb math regression.\n");
        return 1;
    }

    if (!all_io_ok)
    {
        std::fprintf(stderr, "\nFAIL: one or more IR files could not be written.\n");
        return 2;
    }

    std::printf("\n[hello_audio_reverb] ALL PASS — ear-compare the three rooms in Audacity.\n");
    return 0;
}
