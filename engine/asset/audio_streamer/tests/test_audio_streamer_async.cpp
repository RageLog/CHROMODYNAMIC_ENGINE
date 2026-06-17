// =============================================================================
// CHROMODYNAMIC — engine/asset/audio_streamer/tests/test_audio_streamer_async.cpp
// Phase 756 — cd::asset::audio_streamer async unit tests
// Band 6   — workers run the REAL WAV decode; completions carry real PCM.
//
// Tests write real minimal WAV fixtures to a temp dir so the worker decode
// produces actual format, proving real data flows end-to-end through the pool.
//
// Anti-flakiness: NO sleep_for anywhere. All waiting uses condition_variable
// via join_all() or join_pending() which block on a predicate.
//
// Tests:
//   A1  AsyncAudioPool: decode 5 real fixtures; completed_count + poll match.
//   A2  AsyncAudioPool: completed_count accumulates; second poll empty.
//   A3  AsyncAudioPool: poll_completed non-blocking on un-configured pool.
//   A4  AudioStreamer async: tick + join_pending → completed_count == 5.
//   A5  AudioStreamer async: is_loaded true + REAL format for every path.
//   A6  AudioStreamer async: get_loaded valid AssetId after join_pending.
//   A7  AsyncAudioPool: a path that fails to decode is NOT completed.
// =============================================================================

#include <cd/asset/audio_streamer/AudioStreamer.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;

using cd::asset::audio_streamer::AsyncAudioPool;
using cd::asset::audio_streamer::AudioStreamer;
using cd::asset::audio_streamer::AudioStreamerConfig;
using cd::asset::audio_streamer::StreamRequest;

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

[[nodiscard]] fs::path tmp_wav_path()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("cd_asa_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
            std::to_string(seq.fetch_add(1)) + ".wav");
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

// ---- A1: pool decodes 5 real fixtures; completed_count + poll match ---------

TEST(AudioStreamerAsync, PoolDecodesAllSubmittedFixtures)
{
    PathGuard g0 { tmp_wav_path() };
    PathGuard g1 { tmp_wav_path() };
    PathGuard g2 { tmp_wav_path() };
    PathGuard g3 { tmp_wav_path() };
    PathGuard g4 { tmp_wav_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string(), g3.path.string(), g4.path.string()
    };
    for (const auto& p : paths)
    {
        write_wav(fs::path { p }, 1U, 44100U, 16U, 100U);
    }

    AsyncAudioPool pool;
    pool.configure(2U);
    for (const auto& p : paths)
    {
        pool.submit_async(StreamRequest{ p, 0U, 128U });
    }
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), paths.size());

    const auto done = pool.poll_completed();
    EXPECT_EQ(done.size(), paths.size());
    for (const auto& item : done)
    {
        EXPECT_EQ(item.decoded.channels,    1U);
        EXPECT_EQ(item.decoded.sample_rate, 44100U);
        EXPECT_EQ(item.decoded.frame_count, 100U);
        EXPECT_TRUE(item.decoded.has_samples());
    }
}

// ---- A2: completed_count accumulates; second poll empty ----------------------

TEST(AudioStreamerAsync, CompletedCountNeverDecrementsAcrossPolls)
{
    PathGuard g0 { tmp_wav_path() };
    PathGuard g1 { tmp_wav_path() };
    PathGuard g2 { tmp_wav_path() };
    write_wav(g0.path, 1U, 22050U, 16U, 32U);
    write_wav(g1.path, 1U, 22050U, 16U, 32U);
    write_wav(g2.path, 1U, 22050U, 16U, 32U);

    AsyncAudioPool pool;
    pool.configure(2U);
    pool.submit_async(StreamRequest{ g0.path.string(), 0U, 200U });
    pool.submit_async(StreamRequest{ g1.path.string(), 0U, 100U });
    pool.submit_async(StreamRequest{ g2.path.string(), 0U,  50U });
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), 3U);

    const auto first = pool.poll_completed();
    EXPECT_EQ(first.size(), 3U);

    const auto second = pool.poll_completed();
    EXPECT_TRUE(second.empty());

    EXPECT_EQ(pool.completed_count(), 3U);
}

// ---- A3: poll_completed non-blocking on un-configured pool ------------------

TEST(AudioStreamerAsync, PollCompletedNonBlockingOnEmptyPool)
{
    AsyncAudioPool pool;
    const auto result = pool.poll_completed();
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(pool.completed_count(), 0U);
}

// ---- A4: AudioStreamer async tick + join_pending grows completed_count ------

TEST(AudioStreamerAsync, AudioStreamerAsyncTickGrowsCompletedCount)
{
    PathGuard g0 { tmp_wav_path() };
    PathGuard g1 { tmp_wav_path() };
    PathGuard g2 { tmp_wav_path() };
    PathGuard g3 { tmp_wav_path() };
    PathGuard g4 { tmp_wav_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string(), g3.path.string(), g4.path.string()
    };
    for (const auto& p : paths)
    {
        write_wav(fs::path { p }, 2U, 48000U, 16U, 64U);
    }

    AudioStreamer as { AudioStreamerConfig{ .use_async = true, .worker_count = 2U } };
    for (const auto& p : paths)
    {
        as.enqueue(StreamRequest{ p, 0U, 180U });
    }
    EXPECT_EQ(as.pending_count(), 5U);

    as.tick(0.016F);
    EXPECT_EQ(as.pending_count(), 0U);

    as.tick(0.016F);
    as.tick(0.016F);
    as.join_pending();

    EXPECT_EQ(as.completed_count(), 5U);
}

// ---- A5: is_loaded true + REAL format for every path after join_pending -----

TEST(AudioStreamerAsync, IsLoadedRealFormatAfterJoinPending)
{
    PathGuard g0 { tmp_wav_path() };
    PathGuard g1 { tmp_wav_path() };
    PathGuard g2 { tmp_wav_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string()
    };
    write_wav(g0.path, 1U, 44100U, 16U, 100U);
    write_wav(g1.path, 2U, 48000U, 16U, 200U);
    write_wav(g2.path, 1U, 22050U, 16U, 300U);

    AudioStreamer as { AudioStreamerConfig{ .use_async = true, .worker_count = 2U } };
    for (const auto& p : paths)
    {
        as.enqueue(StreamRequest{ p, 0U, 128U });
    }

    as.tick(0.016F);
    as.join_pending();

    for (const auto& p : paths)
    {
        EXPECT_TRUE(as.is_loaded(p)) << "Expected loaded: " << p;
    }
    EXPECT_EQ(as.completed_count(), paths.size());

    // Real per-file format survived the worker → owner round-trip.
    const auto f0 = as.get_format(paths[0]);
    const auto f1 = as.get_format(paths[1]);
    const auto f2 = as.get_format(paths[2]);
    ASSERT_TRUE(f0.has_value() && f1.has_value() && f2.has_value());
    EXPECT_EQ(f0->channels, 1U);  EXPECT_EQ(f0->sample_rate, 44100U);  EXPECT_EQ(f0->frame_count, 100U);
    EXPECT_EQ(f1->channels, 2U);  EXPECT_EQ(f1->sample_rate, 48000U);  EXPECT_EQ(f1->frame_count, 200U);
    EXPECT_EQ(f2->channels, 1U);  EXPECT_EQ(f2->sample_rate, 22050U);  EXPECT_EQ(f2->frame_count, 300U);
}

// ---- A6: get_loaded valid AssetId for every path after join_pending ---------

TEST(AudioStreamerAsync, GetLoadedReturnsValidAssetIdAfterJoinPending)
{
    PathGuard g0 { tmp_wav_path() };
    PathGuard g1 { tmp_wav_path() };
    PathGuard g2 { tmp_wav_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string()
    };
    for (const auto& p : paths)
    {
        write_wav(fs::path { p }, 1U, 44100U, 16U, 50U);
    }

    AudioStreamer as { AudioStreamerConfig{ .use_async = true, .worker_count = 2U } };
    for (const auto& p : paths)
    {
        as.enqueue(StreamRequest{ p, 0U, 200U });
    }

    as.tick(0.016F);
    as.join_pending();

    for (const auto& p : paths)
    {
        const auto id = as.get_loaded(p);
        ASSERT_TRUE(id.has_value()) << "Expected AssetId for: " << p;
        EXPECT_TRUE(id->is_valid()) << "AssetId should be valid for: " << p;
    }
}

// ---- A7: a path that fails to decode is NOT reported completed --------------

TEST(AudioStreamerAsync, FailedDecodeNotCompleted)
{
    AsyncAudioPool pool;
    pool.configure(2U);
    pool.submit_async(StreamRequest{ "__missing_async__.wav", 0U, 200U });
    pool.submit_async(StreamRequest{ "music/track.ogg",       0U, 150U });  // sealed
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), 0U);
    EXPECT_TRUE(pool.poll_completed().empty());
}
