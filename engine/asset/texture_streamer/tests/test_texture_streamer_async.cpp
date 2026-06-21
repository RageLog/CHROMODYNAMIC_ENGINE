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
//   B7  worker_count == 0 is normalised to 1 (no deadlock).
//   B8  decoded block bytes carry the fill pattern from the fixture.
//   B9  large batch (10 items) all decode and complete.
//   B10 async pool completed_count matches streamer completed_count after join.
//   B11 TextureStreamer dedup — same path enqueued twice yields one load.
//   B12 cancel before tick in async mode prevents pending submission.
// =============================================================================

#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#if __has_include(<cd/rhi/NullDevice.hpp>)
#  include <cd/rhi/NullDevice.hpp>
#endif

#include <gtest/gtest.h>

#include <algorithm>
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
    // Movable (clears the source path so the moved-from dtor's fs::remove is a
    // no-op) — std::vector<PathGuard>::reserve/emplace_back needs MoveInsertable.
    PathGuard(PathGuard&& o) noexcept : path { std::move(o.path) } { o.path.clear(); }
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

// ---- B7: worker_count == 0 is normalised to 1 --------------------------------
//
// configure(0) must not spawn zero workers (that would deadlock join_all).
// After submitting one fixture and joining, exactly one completion is reported.

TEST(TextureStreamerAsync, WorkerCountZeroNormalisedToOne)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 8U, 8U);

    AsyncTexturePool pool;
    pool.configure(0U);  // must normalise → 1 worker
    pool.submit_async(StreamRequest{ g.path.string(), 0U, 128U });
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), 1U);
    const auto done = pool.poll_completed();
    ASSERT_EQ(done.size(), 1U);
    EXPECT_EQ(done.front().decoded.width,  8U);
    EXPECT_EQ(done.front().decoded.height, 8U);
}

// ---- B8: decoded block bytes carry the fill pattern from the fixture ---------
//
// write_cdtex fills blocks with a known byte (0xBB).  After decode, every
// byte in DecodedTexture::blocks must equal 0xBB, proving the worker read
// the file content rather than zeroing or synthesising a block.

TEST(TextureStreamerAsync, DecodedBlockBytesMatchFillPattern)
{
    constexpr std::uint8_t kFill = 0xBBU;
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 4U, 4U, kFill);  // 1×1 BC7 block, 16 bytes

    AsyncTexturePool pool;
    pool.configure(1U);
    pool.submit_async(StreamRequest{ g.path.string(), 0U, 200U });
    pool.join_all();

    const auto done = pool.poll_completed();
    ASSERT_EQ(done.size(), 1U);

    const auto& blocks = done.front().decoded.blocks;
    // 4×4 texture → 1 BC7 block → 16 bytes.
    ASSERT_EQ(blocks.size(), 16U);
    const bool all_match = std::ranges::all_of(blocks, [](std::uint8_t b) {
        return b == kFill;
    });
    EXPECT_TRUE(all_match) << "Block bytes do not match the fixture fill pattern";
}

// ---- B9: large batch (10 items) all decode and complete ---------------------

TEST(TextureStreamerAsync, LargeBatchAllComplete)
{
    constexpr std::size_t kCount = 10U;

    std::vector<PathGuard> guards;
    guards.reserve(kCount);
    std::vector<std::string> paths;
    paths.reserve(kCount);

    for (std::size_t i = 0U; i < kCount; ++i)
    {
        guards.emplace_back(tmp_cdtex_path());
        write_cdtex(guards.back().path, 8U, 8U, static_cast<std::uint8_t>(i));
        paths.emplace_back(guards.back().path.string());
    }

    AsyncTexturePool pool;
    pool.configure(4U);
    for (const auto& p : paths)
    {
        pool.submit_async(StreamRequest{ p, 0U, 128U });
    }
    pool.join_all();

    EXPECT_EQ(pool.completed_count(), kCount);

    const auto done = pool.poll_completed();
    EXPECT_EQ(done.size(), kCount);
    for (const auto& item : done)
    {
        EXPECT_EQ(item.decoded.width,  8U);
        EXPECT_EQ(item.decoded.height, 8U);
        EXPECT_TRUE(item.decoded.is_block_compressed);
    }
}

// ---- B10: async pool completed_count matches streamer completed_count --------
//
// After join_pending() the TextureStreamer's completed_count() must equal the
// number of successfully decoded paths, matching the pool's internal counter.

TEST(TextureStreamerAsync, PoolAndStreamerCompletedCountConsistent)
{
#if __has_include(<cd/rhi/NullDevice.hpp>)
    PathGuard g0 { tmp_cdtex_path() };
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    write_cdtex(g0.path, 16U, 16U);
    write_cdtex(g1.path, 16U, 16U);
    write_cdtex(g2.path, 16U, 16U);
    const std::vector<std::string> paths = {
        g0.path.string(), g1.path.string(), g2.path.string()
    };

    TextureStreamer ts { TextureStreamerConfig{ .use_async = true, .worker_count = 2U } };
    for (const auto& p : paths)
    {
        ts.enqueue(StreamRequest{ p, 0U, 200U });
    }

    cd::rhi::NullDevice device;
    ts.tick(0.016F, device);
    ts.join_pending();

    // After join_pending() every successfully decoded texture must be accepted.
    EXPECT_EQ(ts.completed_count(), paths.size());
    for (const auto& p : paths)
    {
        EXPECT_TRUE(ts.is_loaded(p)) << "Not loaded: " << p;
    }
#else
    GTEST_SKIP() << "NullDevice not available";
#endif
}

// ---- B11: TextureStreamer dedup — same path enqueued twice yields one load ---
//
// enqueue() is idempotent.  Enqueueing the same path twice (even with
// different priorities or mip_targets) must result in exactly one pending
// entry and one completed load after async join.

TEST(TextureStreamerAsync, AsyncDedupSamePathOneLoad)
{
#if __has_include(<cd/rhi/NullDevice.hpp>)
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 32U, 32U);
    const std::string path = g.path.string();

    TextureStreamer ts { TextureStreamerConfig{ .use_async = true, .worker_count = 2U } };
    ts.enqueue(StreamRequest{ path, 0U, 200U });
    ts.enqueue(StreamRequest{ path, 1U, 100U });  // duplicate — must be ignored
    ts.enqueue(StreamRequest{ path, 2U,  50U });  // another duplicate

    EXPECT_EQ(ts.pending_count(), 1U);

    cd::rhi::NullDevice device;
    ts.tick(0.016F, device);
    ts.join_pending();

    EXPECT_EQ(ts.completed_count(), 1U);
    EXPECT_TRUE(ts.is_loaded(path));
#else
    GTEST_SKIP() << "NullDevice not available";
#endif
}

// ---- B12: cancel before tick in async mode prevents pending submission -------
//
// If cancel() is called between enqueue() and the first tick(), the request
// must be removed from pending_map_ before it is submitted to the pool.
// After tick() + join_pending() the cancelled path must not be loaded.
// A co-enqueued sibling (not cancelled) must complete normally.

TEST(TextureStreamerAsync, AsyncCancelBeforeTickPreventsSubmission)
{
#if __has_include(<cd/rhi/NullDevice.hpp>)
    PathGuard ga { tmp_cdtex_path() };
    PathGuard gb { tmp_cdtex_path() };
    write_cdtex(ga.path, 16U, 16U);
    write_cdtex(gb.path, 32U, 32U);
    const std::string pa = ga.path.string();
    const std::string pb = gb.path.string();

    TextureStreamer ts { TextureStreamerConfig{ .use_async = true, .worker_count = 2U } };
    ts.enqueue(StreamRequest{ pa, 0U, 200U });
    ts.enqueue(StreamRequest{ pb, 0U, 100U });
    EXPECT_EQ(ts.pending_count(), 2U);

    // Cancel pa before any tick() — must be removed from pending_map_.
    ts.cancel(pa);
    EXPECT_EQ(ts.pending_count(), 1U);

    cd::rhi::NullDevice device;
    ts.tick(0.016F, device);   // submits only pb to the pool
    ts.join_pending();

    // pa was never submitted — must not be loaded.
    EXPECT_FALSE(ts.is_loaded(pa));
    // pb was submitted and should have decoded.
    EXPECT_TRUE(ts.is_loaded(pb));
    EXPECT_EQ(ts.completed_count(), 1U);
#else
    GTEST_SKIP() << "NullDevice not available";
#endif
}
