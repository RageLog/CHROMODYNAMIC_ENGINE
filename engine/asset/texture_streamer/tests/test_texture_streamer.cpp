// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_streamer/tests/test_texture_streamer.cpp
// Phase 599 — cd::asset::texture_streamer unit tests (sync path)
// Band 6   — real cdtex decode wired: tests assert REAL dimensions, not 1x1.
//
// Tests use cd::rhi::NullDevice (headless GPU backend) so they run without
// a real Vulkan ICD. Real .cdtex fixtures are written to a temp dir so the
// decode dispatch produces actual texels + dimensions end-to-end.
//
// Tests:
//   T1  enqueue + tick + is_loaded round-trip — REAL dimensions match fixture
//   T2  cancel removes pending request
//   T3  priority ordering: higher priority served first
//   T4  decode-failure path: completed_count stays zero, is_loaded false
//   T5  completed_count grows on successful round-trip
//   T6  enqueue is idempotent (duplicate enqueue does not double-count)
//   T7  get_loaded returns nullopt for unknown path
//   T8  decode_texture_file: cdtex fixture yields real BC7 blocks + dims
// =============================================================================

#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#include <cd/rhi/NullDevice.hpp>

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

using cd::asset::texture_streamer::TextureStreamer;

// ---- Fixture: write a synthetic .cdtex (matches cook_texture v1 layout) -----

[[nodiscard]] fs::path tmp_cdtex_path()
{
    static std::atomic<std::uint64_t> seq { 0 };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() /
           ("cd_ts_" + std::to_string(static_cast<std::uint64_t>(stamp)) + "_" +
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

/// Write a valid single-mip .cdtex with dimensions w×h filled with `fill`.
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

// ---- T1: enqueue + tick + is_loaded round-trip with REAL dimensions ---------
//
// A 64×32 .cdtex fixture is decoded; the loaded record must carry the REAL
// 64×32 dimensions — proving real decode, not a 1×1 placeholder.

TEST(TextureStreamer, EnqueueTickIsLoadedRealDimensions)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 64U, 32U, 0xCC);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue({ path, 0U, 200U });
    EXPECT_EQ(ts.pending_count(), 1U);
    EXPECT_FALSE(ts.is_loaded(path));

    ts.tick(0.016F, device);

    EXPECT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(ts.pending_count(),   0U);
    EXPECT_EQ(ts.completed_count(), 1U);

    const auto handle = ts.get_loaded(path);
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->is_valid());

    // The load-bearing assertion: REAL dimensions from the file, not 1×1.
    const auto dims = ts.get_dimensions(path);
    ASSERT_TRUE(dims.has_value());
    EXPECT_EQ(dims->first,  64U);
    EXPECT_EQ(dims->second, 32U);
}

// ---- T2: cancel removes pending request -------------------------------------

TEST(TextureStreamer, CancelRemovesPending)
{
    PathGuard ga { tmp_cdtex_path() };
    PathGuard gb { tmp_cdtex_path() };
    write_cdtex(ga.path, 8U, 8U);
    write_cdtex(gb.path, 16U, 16U);
    const std::string pa = ga.path.string();
    const std::string pb = gb.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue({ pa, 0U, 50U });
    ts.enqueue({ pb, 0U, 80U });
    EXPECT_EQ(ts.pending_count(), 2U);

    ts.cancel(pa);
    EXPECT_EQ(ts.pending_count(), 1U);

    ts.tick(0.016F, device);  // processes pb (only remaining request)

    EXPECT_EQ(ts.pending_count(), 0U);
    EXPECT_FALSE(ts.is_loaded(pa));  // Was cancelled, never loaded.
    EXPECT_TRUE(ts.is_loaded(pb));
}

// ---- T3: priority ordering: higher priority served first --------------------

TEST(TextureStreamer, HigherPriorityServedFirst)
{
    PathGuard glo { tmp_cdtex_path() };
    PathGuard ghi { tmp_cdtex_path() };
    write_cdtex(glo.path, 4U, 4U);
    write_cdtex(ghi.path, 128U, 64U);
    const std::string lo = glo.path.string();
    const std::string hi = ghi.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue({ lo, 0U,  10U });
    ts.enqueue({ hi, 0U, 200U });
    EXPECT_EQ(ts.pending_count(), 2U);

    ts.tick(0.016F, device);
    EXPECT_EQ(ts.pending_count(), 1U);

    ts.cancel(lo);
    EXPECT_EQ(ts.pending_count(), 0U);

    EXPECT_TRUE(ts.is_loaded(hi));
    EXPECT_FALSE(ts.is_loaded(lo));

    // High-priority load decoded to its real 128×64 size.
    const auto dims = ts.get_dimensions(hi);
    ASSERT_TRUE(dims.has_value());
    EXPECT_EQ(dims->first,  128U);
    EXPECT_EQ(dims->second, 64U);
}

// ---- T4: decode-failure path: completed_count stays zero --------------------
//
// A path that does not exist on disk fails to decode and is silently dropped —
// completed_count stays zero and is_loaded reports false.

TEST(TextureStreamer, DecodeFailureLeavesCompletedZero)
{
    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue({ "__no_such_texture__.cdtex", 0U, 200U });
    EXPECT_EQ(ts.pending_count(), 1U);

    ts.tick(0.016F, device);

    EXPECT_EQ(ts.pending_count(),   0U);  // consumed from pending
    EXPECT_EQ(ts.completed_count(), 0U);  // decode failed → not completed
    EXPECT_FALSE(ts.is_loaded("__no_such_texture__.cdtex"));
}

// ---- T5: completed_count grows on each successful load ----------------------

TEST(TextureStreamer, CompletedCountGrowsOnSuccess)
{
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    write_cdtex(g1.path, 32U, 32U);
    write_cdtex(g2.path, 32U, 32U);
    const std::string p1 = g1.path.string();
    const std::string p2 = g2.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue({ p1, 0U, 100U });
    ts.enqueue({ p2, 0U, 100U });

    EXPECT_EQ(ts.completed_count(), 0U);

    ts.tick(0.016F, device);
    EXPECT_EQ(ts.completed_count(), 1U);

    ts.tick(0.016F, device);
    EXPECT_EQ(ts.completed_count(), 2U);

    EXPECT_TRUE(ts.is_loaded(p1));
    EXPECT_TRUE(ts.is_loaded(p2));
}

// ---- T6: enqueue is idempotent (duplicate enqueue does not double-count) ----

TEST(TextureStreamer, EnqueueIdempotent)
{
    TextureStreamer ts;

    ts.enqueue({ "texture.cdtex", 0U, 100U });
    ts.enqueue({ "texture.cdtex", 0U, 200U });  // Duplicate — must be ignored.
    ts.enqueue({ "texture.cdtex", 2U,  50U });  // Another duplicate.

    EXPECT_EQ(ts.pending_count(), 1U);
}

// ---- T7: get_loaded returns nullopt for unknown path -------------------------

TEST(TextureStreamer, GetLoadedUnknownReturnsNullopt)
{
    const TextureStreamer ts;

    EXPECT_EQ(ts.get_loaded("unknown.cdtex"), std::nullopt);
    EXPECT_EQ(ts.get_dimensions("unknown.cdtex"), std::nullopt);
    EXPECT_FALSE(ts.is_loaded("unknown.cdtex"));
    EXPECT_EQ(ts.pending_count(),   0U);
    EXPECT_EQ(ts.completed_count(), 0U);
}

// ---- T8: decode_texture_file directly returns real BC7 blocks + dims --------

TEST(TextureStreamer, DecodeTextureFileReturnsRealCdtex)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 256U, 128U, 0x55);

    const auto decoded = cd::asset::texture_streamer::decode_texture_file(g.path.string());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->width,  256U);
    EXPECT_EQ(decoded->height, 128U);
    EXPECT_TRUE(decoded->is_block_compressed);
    EXPECT_TRUE(decoded->has_pixels());
    // 256/4 × 128/4 × 16 bytes per BC7 block.
    EXPECT_EQ(decoded->blocks.size(), 64U * 32U * 16U);
}
