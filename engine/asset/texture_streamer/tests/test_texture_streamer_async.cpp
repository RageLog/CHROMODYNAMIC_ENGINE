// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_streamer/tests/test_texture_streamer_async.cpp
// Phase 714 — cd::asset::texture_streamer async unit tests
// Band 6   — workers run the REAL cdtex decode; completions carry real texels.
//
// Tests run headless — no Vulkan ICD required (NullDevice). Real .cdtex
// fixtures are written to a temp dir so the worker decode produces actual
// dimensions, proving real data flows end-to-end through the async pool.
//
// Anti-flakiness: NO sleep_for anywhere. All waiting uses condition_variable
// via join_all() or join_pending() which block on a predicate.
//
// Tests:
//   A1  AsyncTexturePool: configure + submit_async + join_all decode 5 fixtures.
//   A2  AsyncTexturePool: completed_count accumulates; second poll empty.
//   A3  AsyncTexturePool: poll_completed non-blocking on un-configured pool.
//   A4  TextureStreamer async: tick + join_pending → completed_count == 5.
//   A5  TextureStreamer async: is_loaded true for every path; REAL dimensions.
//   A6  AsyncTexturePool: a path that fails to decode is NOT completed.
// =============================================================================

#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#if __has_include(<cd/rhi/NullDevice.hpp>)
#  include <cd/rhi/NullDevice.hpp>
#endif

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

using cd::asset::texture_streamer::AsyncTexturePool;
using cd::asset::texture_streamer::StreamRequest;
using cd::asset::texture_streamer::TextureStreamer;
using cd::asset::texture_streamer::TextureStreamerConfig;

[[nodiscard]] fs::path tmp_cdtex_path()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("cd_tsa_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
            std::to_string(seq.fetch_add(1)) + ".cdtex");
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

void write_u32(std::ofstream& f, std::uint32_t v)
{
    const std::uint8_t b[4] { static_cast<std::uint8_t>(v),
                              static_cast<std::uint8_t>(v >> 8),
                              static_cast<std::uint8_t>(v >> 16),
                              static_cast<std::uint8_t>(v >> 24) };
    f.write(reinterpret_cast<const char*>(b), 4);
}

void write_u16(std::ofstream& f, std::uint16_t v)
{
    const std::uint8_t b[2] { static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8) };
    f.write(reinterpret_cast<const char*>(b), 2);
}

void write_cdtex(const fs::path& p, std::uint32_t w, std::uint32_t h, std::uint8_t fill = 0xAA)
{
    const auto bw = static_cast<std::uint16_t>((w + 3) / 4);
    const auto bh = static_cast<std::uint16_t>((h + 3) / 4);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write("CDBC7", 5);
    const std::uint8_t version = 1;
    f.write(reinterpret_cast<const char*>(&version), 1);
    write_u32(f, w);
    write_u32(f, h);
    write_u16(f, bw);
    write_u16(f, bh);
    const std::size_t payload = static_cast<std::size_t>(bw) * bh * 16U;
    const std::vector<std::uint8_t> blocks(payload, fill);
    f.write(reinterpret_cast<const char*>(blocks.data()), static_cast<std::streamsize>(payload));
}

}  // namespace

// ---- A1: pool decodes 5 fixtures; completed_count + poll match --------------

TEST(TextureStreamerAsync, PoolDecodesAllSubmittedFixtures)
{
    PathGuard g0 { tmp_cdtex_path() };
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    PathGuard g3 { tmp_cdtex_path() };
    PathGuard g4 { tmp_cdtex_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string(), g3.path.string(), g4.path.string()
    };
    for (const auto& p : paths)
    {
        write_cdtex(fs::path { p }, 16U, 16U);
    }

    AsyncTexturePool pool;
    pool.configure(2U);
    for (const auto& p : paths)
    {
        pool.submit_async(StreamRequest{ p, 0U, 128U });
    }
    pool.join_all();  // blocks until all 5 decoded

    EXPECT_EQ(pool.completed_count(), paths.size());

    const auto done = pool.poll_completed();
    EXPECT_EQ(done.size(), paths.size());
    for (const auto& item : done)
    {
        EXPECT_EQ(item.decoded.width,  16U);
        EXPECT_EQ(item.decoded.height, 16U);
        EXPECT_TRUE(item.decoded.is_block_compressed);
        EXPECT_TRUE(item.decoded.has_pixels());
    }
}

// ---- A2: completed_count accumulates; second poll empty ----------------------

TEST(TextureStreamerAsync, CompletedCountNeverDecrementsAcrossPolls)
{
    PathGuard g0 { tmp_cdtex_path() };
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    write_cdtex(g0.path, 8U, 8U);
    write_cdtex(g1.path, 8U, 8U);
    write_cdtex(g2.path, 8U, 8U);

    AsyncTexturePool pool;
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

    EXPECT_EQ(pool.completed_count(), 3U);  // cumulative atomic never decrements
}

// ---- A3: poll_completed non-blocking on un-configured pool ------------------

TEST(TextureStreamerAsync, PollCompletedNonBlockingOnEmptyPool)
{
    AsyncTexturePool pool;
    // Intentionally NOT calling configure() — no workers started.

    const auto result = pool.poll_completed();
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(pool.completed_count(), 0U);
}

// ---- A4: TextureStreamer async tick + join_pending grows completed_count ----

TEST(TextureStreamerAsync, TextureStreamerAsyncTickGrowsCompletedCount)
{
#if __has_include(<cd/rhi/NullDevice.hpp>)
    PathGuard g0 { tmp_cdtex_path() };
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    PathGuard g3 { tmp_cdtex_path() };
    PathGuard g4 { tmp_cdtex_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string(), g3.path.string(), g4.path.string()
    };
    for (const auto& p : paths)
    {
        write_cdtex(fs::path { p }, 32U, 16U);
    }

    TextureStreamer ts { TextureStreamerConfig{ .use_async = true, .worker_count = 2U } };
    for (const auto& p : paths)
    {
        ts.enqueue(StreamRequest{ p, 0U, 180U });
    }
    EXPECT_EQ(ts.pending_count(), 5U);

    cd::rhi::NullDevice device;
    ts.tick(0.016F, device);          // submits all 5 to the pool
    EXPECT_EQ(ts.pending_count(), 0U);

    ts.tick(0.016F, device);          // drain (no sleep — just polling)
    ts.tick(0.016F, device);
    ts.join_pending();                // blocks until all decoded + accepted

    EXPECT_EQ(ts.completed_count(), 5U);
#else
    GTEST_SKIP() << "NullDevice not available — async tick test skipped";
#endif
}

// ---- A5: is_loaded true + REAL dimensions for every path after join ---------

TEST(TextureStreamerAsync, IsLoadedRealDimensionsAfterJoinPending)
{
#if __has_include(<cd/rhi/NullDevice.hpp>)
    PathGuard g0 { tmp_cdtex_path() };
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string()
    };
    write_cdtex(g0.path, 64U, 64U);
    write_cdtex(g1.path, 128U, 32U);
    write_cdtex(g2.path, 256U, 256U);

    TextureStreamer ts { TextureStreamerConfig{ .use_async = true, .worker_count = 2U } };
    for (const auto& p : paths)
    {
        ts.enqueue(StreamRequest{ p, 0U, 128U });
    }

    cd::rhi::NullDevice device;
    ts.tick(0.016F, device);
    ts.join_pending();

    for (const auto& p : paths)
    {
        EXPECT_TRUE(ts.is_loaded(p)) << "Expected loaded: " << p;
    }
    EXPECT_EQ(ts.completed_count(), paths.size());

    // Real per-file dimensions survived the worker → owner round-trip.
    const auto d0 = ts.get_dimensions(paths[0]);
    const auto d1 = ts.get_dimensions(paths[1]);
    const auto d2 = ts.get_dimensions(paths[2]);
    ASSERT_TRUE(d0.has_value() && d1.has_value() && d2.has_value());
    EXPECT_EQ(d0->first,  64U);  EXPECT_EQ(d0->second,  64U);
    EXPECT_EQ(d1->first, 128U);  EXPECT_EQ(d1->second,  32U);
    EXPECT_EQ(d2->first, 256U);  EXPECT_EQ(d2->second, 256U);
#else
    GTEST_SKIP() << "NullDevice not available — async is_loaded test skipped";
#endif
}

// ---- A6: a path that fails to decode is NOT reported completed ---------------

TEST(TextureStreamerAsync, FailedDecodeNotCompleted)
{
    AsyncTexturePool pool;
    pool.configure(2U);
    pool.submit_async(StreamRequest{ "__missing_async__.cdtex", 0U, 200U });
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), 0U);  // decode failed → not completed
    EXPECT_TRUE(pool.poll_completed().empty());
}
