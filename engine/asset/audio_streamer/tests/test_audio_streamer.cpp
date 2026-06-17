// =============================================================================
// CHROMODYNAMIC — engine/asset/audio_streamer/tests/test_audio_streamer.cpp
// Phase 619 — cd::asset::audio_streamer unit tests (sync path)
// Band 6   — real WAV PCM decode wired: tests assert REAL format, not a hash.
//
// Tests write real minimal WAV fixtures to a temp dir so the decode dispatch
// produces actual PCM + (channels, sample_rate, frame_count) end-to-end.
//
// Tests:
//   T1  enqueue + tick + is_loaded round-trip — REAL format matches fixture
//   T2  cancel removes pending request
//   T3  priority ordering: higher priority served first
//   T4  decode-failure / sealed-.ogg path: completed_count stays zero
//   T5  completed_count grows on each successful tick
//   T6  enqueue is idempotent (duplicate enqueue does not double-count)
//   T7  get_loaded returns nullopt for unknown path
//   T8  decode_audio_file: WAV fixture yields real PCM + format
//   T9  decode_audio_file: .ogg input is sealed (returns nullopt)
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

using cd::asset::audio_streamer::AudioStreamer;

// ---- Fixture: write a minimal valid mono/stereo PCM WAV ---------------------

void put_u16(std::vector<std::uint8_t>& v, std::uint16_t x)
{
    v.push_back(static_cast<std::uint8_t>(x & 0xFFU));
    v.push_back(static_cast<std::uint8_t>((x >> 8U) & 0xFFU));
}

void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(static_cast<std::uint8_t>(x & 0xFFU));
    v.push_back(static_cast<std::uint8_t>((x >> 8U) & 0xFFU));
    v.push_back(static_cast<std::uint8_t>((x >> 16U) & 0xFFU));
    v.push_back(static_cast<std::uint8_t>((x >> 24U) & 0xFFU));
}

void put_tag(std::vector<std::uint8_t>& v, const char (&tag)[5])
{
    for (int i = 0; i < 4; ++i)
    {
        v.push_back(static_cast<std::uint8_t>(tag[i]));
    }
}

[[nodiscard]] std::vector<std::uint8_t>
make_wav_bytes(std::uint16_t channels, std::uint32_t sample_rate, std::uint16_t bits, std::uint32_t frames)
{
    const std::uint32_t data_size = frames * channels * (bits / 8U);
    const std::uint32_t fmt_size  = 16U;
    const std::uint32_t riff_size = 4U + 8U + fmt_size + 8U + data_size;

    std::vector<std::uint8_t> v;
    v.reserve(8U + riff_size);
    put_tag(v, "RIFF");
    put_u32(v, riff_size);
    put_tag(v, "WAVE");
    put_tag(v, "fmt ");
    put_u32(v, fmt_size);
    put_u16(v, 1U);  // PCM
    put_u16(v, channels);
    put_u32(v, sample_rate);
    put_u32(v, sample_rate * channels * (bits / 8U));
    put_u16(v, static_cast<std::uint16_t>(channels * (bits / 8U)));
    put_u16(v, bits);
    put_tag(v, "data");
    put_u32(v, data_size);
    for (std::uint32_t i = 0; i < data_size; ++i)
    {
        v.push_back(0U);
    }
    return v;
}

[[nodiscard]] fs::path tmp_audio_path(const char* ext)
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("cd_as_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
            std::to_string(seq.fetch_add(1)) + ext);
}

struct PathGuard
{
    fs::path path;

    explicit PathGuard(fs::path p)
        : path { std::move(p) }
    {
    }

    ~PathGuard()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }

    PathGuard(const PathGuard&)            = delete;
    PathGuard& operator=(const PathGuard&) = delete;
    PathGuard(PathGuard&&)                 = delete;
    PathGuard& operator=(PathGuard&&)      = delete;
};

void write_wav(const fs::path& p, std::uint16_t ch, std::uint32_t rate, std::uint16_t bits, std::uint32_t frames)
{
    const auto bytes = make_wav_bytes(ch, rate, bits, frames);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

// ---- T1: enqueue + tick + is_loaded round-trip with REAL format -------------

TEST(AudioStreamer, EnqueueTickIsLoadedRealFormat)
{
    PathGuard g { tmp_audio_path(".wav") };
    write_wav(g.path, 1U, 44100U, 16U, 1000U);
    const std::string path = g.path.string();

    AudioStreamer as;
    as.enqueue({ path, 0U, 200U });
    EXPECT_EQ(as.pending_count(), 1U);
    EXPECT_FALSE(as.is_loaded(path));

    as.tick(0.016F);

    EXPECT_TRUE(as.is_loaded(path));
    EXPECT_EQ(as.pending_count(),   0U);
    EXPECT_EQ(as.completed_count(), 1U);

    const auto id = as.get_loaded(path);
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(id->is_valid());

    // Load-bearing: REAL decoded format from the file, not a path-hash.
    const auto fmt = as.get_format(path);
    ASSERT_TRUE(fmt.has_value());
    EXPECT_EQ(fmt->channels,    1U);
    EXPECT_EQ(fmt->sample_rate, 44100U);
    EXPECT_EQ(fmt->frame_count, 1000U);
}

// ---- T2: cancel removes pending request -------------------------------------

TEST(AudioStreamer, CancelRemovesPending)
{
    PathGuard ga { tmp_audio_path(".wav") };
    PathGuard gb { tmp_audio_path(".wav") };
    write_wav(ga.path, 1U, 22050U, 16U, 100U);
    write_wav(gb.path, 2U, 48000U, 16U, 200U);
    const std::string pa = ga.path.string();
    const std::string pb = gb.path.string();

    AudioStreamer as;
    as.enqueue({ pa, 0U, 50U });
    as.enqueue({ pb, 0U, 80U });
    EXPECT_EQ(as.pending_count(), 2U);

    as.cancel(pa);
    EXPECT_EQ(as.pending_count(), 1U);

    as.tick(0.016F);  // processes pb (only remaining request)

    EXPECT_EQ(as.pending_count(), 0U);
    EXPECT_FALSE(as.is_loaded(pa));  // Was cancelled, never loaded.
    EXPECT_TRUE(as.is_loaded(pb));
}

// ---- T3: priority ordering: higher priority served first --------------------

TEST(AudioStreamer, HigherPriorityServedFirst)
{
    PathGuard glo { tmp_audio_path(".wav") };
    PathGuard ghi { tmp_audio_path(".wav") };
    write_wav(glo.path, 1U, 8000U, 16U, 10U);
    write_wav(ghi.path, 2U, 48000U, 16U, 512U);
    const std::string lo = glo.path.string();
    const std::string hi = ghi.path.string();

    AudioStreamer as;
    as.enqueue({ lo, 0U,  10U });
    as.enqueue({ hi, 0U, 200U });
    EXPECT_EQ(as.pending_count(), 2U);

    as.tick(0.016F);
    EXPECT_EQ(as.pending_count(), 1U);

    as.cancel(lo);
    EXPECT_EQ(as.pending_count(), 0U);

    EXPECT_TRUE(as.is_loaded(hi));
    EXPECT_FALSE(as.is_loaded(lo));

    // High-priority clip decoded to its real stereo / 48k / 512-frame format.
    const auto fmt = as.get_format(hi);
    ASSERT_TRUE(fmt.has_value());
    EXPECT_EQ(fmt->channels,    2U);
    EXPECT_EQ(fmt->sample_rate, 48000U);
    EXPECT_EQ(fmt->frame_count, 512U);
}

// ---- T4: decode-failure path: completed_count stays zero --------------------

TEST(AudioStreamer, DecodeFailureLeavesCompletedZero)
{
    AudioStreamer as;

    // Nonexistent .wav + a sealed .ogg both fail to decode → dropped.
    as.enqueue({ "__no_such_clip__.wav", 0U, 200U });
    as.enqueue({ "music/track.ogg",      0U, 150U });
    EXPECT_EQ(as.pending_count(), 2U);

    as.tick(0.016F);
    as.tick(0.016F);

    EXPECT_EQ(as.pending_count(),   0U);  // both consumed
    EXPECT_EQ(as.completed_count(), 0U);  // neither decoded
    EXPECT_FALSE(as.is_loaded("__no_such_clip__.wav"));
    EXPECT_FALSE(as.is_loaded("music/track.ogg"));
}

// ---- T5: completed_count grows on each successful tick ----------------------

TEST(AudioStreamer, CompletedCountGrowsOnSuccess)
{
    PathGuard g1 { tmp_audio_path(".wav") };
    PathGuard g2 { tmp_audio_path(".wav") };
    write_wav(g1.path, 1U, 44100U, 16U, 64U);
    write_wav(g2.path, 1U, 44100U, 16U, 64U);
    const std::string p1 = g1.path.string();
    const std::string p2 = g2.path.string();

    AudioStreamer as;
    as.enqueue({ p1, 0U, 100U });
    as.enqueue({ p2, 1U, 100U });

    EXPECT_EQ(as.completed_count(), 0U);

    as.tick(0.016F);
    EXPECT_EQ(as.completed_count(), 1U);

    as.tick(0.016F);
    EXPECT_EQ(as.completed_count(), 2U);

    EXPECT_TRUE(as.is_loaded(p1));
    EXPECT_TRUE(as.is_loaded(p2));
}

// ---- T6: enqueue is idempotent (duplicate enqueue does not double-count) ----

TEST(AudioStreamer, EnqueueIdempotent)
{
    AudioStreamer as;

    as.enqueue({ "audio/clip.wav", 0U, 100U });
    as.enqueue({ "audio/clip.wav", 0U, 200U });  // Duplicate — must be ignored.
    as.enqueue({ "audio/clip.wav", 1U,  50U });  // Another duplicate.

    EXPECT_EQ(as.pending_count(), 1U);
}

// ---- T7: get_loaded returns nullopt for unknown path ------------------------

TEST(AudioStreamer, GetLoadedUnknownReturnsNullopt)
{
    const AudioStreamer as;

    EXPECT_EQ(as.get_loaded("audio/unknown.wav"), std::nullopt);
    EXPECT_EQ(as.get_format("audio/unknown.wav"), std::nullopt);
    EXPECT_FALSE(as.is_loaded("audio/unknown.wav"));
    EXPECT_EQ(as.pending_count(),   0U);
    EXPECT_EQ(as.completed_count(), 0U);
}

// ---- T8: decode_audio_file directly returns real PCM + format ---------------

TEST(AudioStreamer, DecodeAudioFileReturnsRealWav)
{
    PathGuard g { tmp_audio_path(".wav") };
    write_wav(g.path, 2U, 48000U, 16U, 256U);

    const auto decoded = cd::asset::audio_streamer::decode_audio_file(g.path.string());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->channels,        2U);
    EXPECT_EQ(decoded->sample_rate,     48000U);
    EXPECT_EQ(decoded->bits_per_sample, 16U);
    EXPECT_EQ(decoded->frame_count,     256U);
    EXPECT_TRUE(decoded->has_samples());
    // 256 frames × 2 channels × 2 bytes per s16 sample.
    EXPECT_EQ(decoded->pcm.size(), 256U * 2U * 2U);
}

// ---- T9: .ogg input is sealed (returns nullopt) -----------------------------

TEST(AudioStreamer, DecodeAudioFileOggIsSealed)
{
    // Even if an .ogg fixture existed, the asset layer has no Vorbis decoder
    // yet, so the dispatch returns nullopt (sealed — see Band-6 ADR).
    const auto decoded = cd::asset::audio_streamer::decode_audio_file("music/track.ogg");
    EXPECT_FALSE(decoded.has_value());
}
