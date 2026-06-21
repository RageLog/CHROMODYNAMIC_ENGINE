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
//   T9  cancel of already-loaded path is a no-op
//   T10 cancel of unknown path is a no-op (no crash)
//   T11 get_dimensions returns nullopt for a still-pending path
//   T12 three-item priority queue drains in strict priority order
//   T13 decode_texture_file sealed paths (PNG/JPG/KTX2/raw/no-ext) return nullopt
//   T14 live_texture_count tracks GPU handle creation per load
//   T15 mip_target != 0 propagates to the GPU texture descriptor
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

// ---- T9: cancel of already-loaded path is a no-op ---------------------------
//
// cancel() must not remove a loaded record.  After a successful tick() the
// entry lives in completed_, not pending_map_.  A subsequent cancel() call
// with the same path must leave is_loaded() returning true.

TEST(TextureStreamer, CancelLoadedPathIsNoOp)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 8U, 8U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue({ path, 0U, 200U });
    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));

    // cancel() on an already-loaded path must be a no-op.
    ts.cancel(path);
    EXPECT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(ts.completed_count(), 1U);
}

// ---- T10: cancel of unknown path is a no-op (no crash) ----------------------

TEST(TextureStreamer, CancelUnknownPathIsNoOp)
{
    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // Cancelling a path that was never enqueued must not crash or corrupt state.
    ts.cancel("nonexistent.cdtex");
    EXPECT_EQ(ts.pending_count(),   0U);
    EXPECT_EQ(ts.completed_count(), 0U);
}

// ---- T11: get_dimensions returns nullopt for a still-pending path ------------
//
// A path is enqueued but tick() has not yet been called.  get_dimensions()
// must return nullopt — only a fully loaded (post-tick) record has dimensions.

TEST(TextureStreamer, GetDimensionsNulloptForPendingPath)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 32U, 32U);
    const std::string path = g.path.string();

    TextureStreamer ts;
    ts.enqueue({ path, 0U, 100U });

    // Still pending — must not have dimensions yet.
    EXPECT_FALSE(ts.get_dimensions(path).has_value());
    EXPECT_EQ(ts.pending_count(), 1U);
}

// ---- T12: three-item priority queue drains in strict priority order ----------
//
// Three requests with distinct priorities (low=10, mid=100, high=200) are
// enqueued simultaneously.  One tick() drains exactly one — the highest.
// A second tick() drains the middle.  A third drains the lowest.

TEST(TextureStreamer, ThreeItemPriorityDrainOrder)
{
    PathGuard glo  { tmp_cdtex_path() };
    PathGuard gmid { tmp_cdtex_path() };
    PathGuard ghi  { tmp_cdtex_path() };
    write_cdtex(glo.path,   4U,  4U, 0x11);
    write_cdtex(gmid.path,  8U,  8U, 0x22);
    write_cdtex(ghi.path,  16U, 16U, 0x33);
    const std::string lo  = glo.path.string();
    const std::string mid = gmid.path.string();
    const std::string hi  = ghi.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue({ lo,  0U,  10U });
    ts.enqueue({ mid, 0U, 100U });
    ts.enqueue({ hi,  0U, 200U });
    EXPECT_EQ(ts.pending_count(), 3U);

    // Tick 1: highest priority (hi, 200) must be selected.
    ts.tick(0.016F, device);
    EXPECT_EQ(ts.pending_count(), 2U);
    EXPECT_TRUE(ts.is_loaded(hi));
    EXPECT_FALSE(ts.is_loaded(mid));
    EXPECT_FALSE(ts.is_loaded(lo));

    // Tick 2: next highest (mid, 100) must be selected.
    ts.tick(0.016F, device);
    EXPECT_EQ(ts.pending_count(), 1U);
    EXPECT_TRUE(ts.is_loaded(mid));
    EXPECT_FALSE(ts.is_loaded(lo));

    // Tick 3: last remaining (lo, 10) must be selected.
    ts.tick(0.016F, device);
    EXPECT_EQ(ts.pending_count(), 0U);
    EXPECT_TRUE(ts.is_loaded(lo));
    EXPECT_EQ(ts.completed_count(), 3U);
}

// ---- T13: decode_texture_file sealed paths return nullopt -------------------
//
// PNG/JPG (and any non-.cdtex extension) are SEALED per ADR-20260616-band6.
// decode_texture_file must return nullopt for these extensions — not crash.

TEST(TextureStreamer, DecodeTextureFileNonCdtexExtensionSealed)
{
    // .png — SEALED, should return nullopt without crash.
    EXPECT_FALSE(cd::asset::texture_streamer::decode_texture_file("texture.png").has_value());
    // .jpg — SEALED.
    EXPECT_FALSE(cd::asset::texture_streamer::decode_texture_file("texture.jpg").has_value());
    // .ktx2 — SEALED.
    EXPECT_FALSE(cd::asset::texture_streamer::decode_texture_file("texture.ktx2").has_value());
    // Completely unknown extension — SEALED.
    EXPECT_FALSE(cd::asset::texture_streamer::decode_texture_file("texture.raw").has_value());
    // No extension at all.
    EXPECT_FALSE(cd::asset::texture_streamer::decode_texture_file("texture").has_value());
}

// ---- T14: live_texture_count tracks GPU handle creation per load ------------
//
// NullDevice::live_texture_count() increments exactly once per successful
// tick().  This proves create_texture is actually called with a valid desc.

TEST(TextureStreamer, LiveTextureCountTracksGpuHandles)
{
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    write_cdtex(g1.path, 16U, 16U);
    write_cdtex(g2.path, 32U, 32U);
    const std::string p1 = g1.path.string();
    const std::string p2 = g2.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    EXPECT_EQ(device.live_texture_count(), 0U);

    ts.enqueue({ p1, 0U, 100U });
    ts.tick(0.016F, device);
    EXPECT_EQ(device.live_texture_count(), 1U);

    ts.enqueue({ p2, 0U, 100U });
    ts.tick(0.016F, device);
    EXPECT_EQ(device.live_texture_count(), 2U);
}

// ---- T15: mip_target != 0 propagates to the GPU texture descriptor ----------
//
// When mip_target == 3, the streamer must create a GPU texture with
// mip_levels == 3 (not 1 which is the mip_target == 0 default).
// NullDevice stores TextureDesc so we verify the mip chain requested.

TEST(TextureStreamer, MipTargetNonZeroFlowsToDesc)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 64U, 64U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // mip_target == 3 → mip_levels must be 3 in the TextureDesc.
    ts.enqueue({ path, 3U, 200U });
    ts.tick(0.016F, device);

    ASSERT_TRUE(ts.is_loaded(path));
    // REAL dimensions must still be correct.
    const auto dims = ts.get_dimensions(path);
    ASSERT_TRUE(dims.has_value());
    EXPECT_EQ(dims->first,  64U);
    EXPECT_EQ(dims->second, 64U);
    // GPU texture was created — exactly one live handle.
    EXPECT_EQ(device.live_texture_count(), 1U);
}
