// =============================================================================
// CHROMODYNAMIC — engine/asset/texture_streamer/tests/test_texture_streamer.cpp
// Phase 599 — cd::asset::texture_streamer unit tests (sync path)
// Band 6   — real cdtex decode wired: tests assert REAL dimensions, not 1x1.
// Phase 1268 — CPU charter complete: mip residency, LRU/budget eviction,
//              screen-coverage priority, mip-upgrade, PathGuard move, dedup ext.
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
//   T16 screen_coverage biases priority within same priority bucket
//   T17 screen_coverage = 0 does not change priority relative ordering
//   T18 is_mip_loaded: base mip always resident for mip_target == 0
//   T19 is_mip_loaded: levels 0..N-1 resident when mip_target == N
//   T20 is_mip_loaded: higher levels NOT resident when mip_target < level
//   T21 is_mip_loaded: returns false for unknown/unloaded path
//   T22 resident_mip_target: returns the stored mip_target on success
//   T23 resident_mip_target: returns nullopt for unloaded path
//   T24 set_texture_budget + eviction on tick: loaded_count stays at budget
//   T25 evict_lru: least-recently-used record evicted, handle destroyed
//   T26 touch: promotes a texture to MRU, protecting it from LRU eviction
//   T27 budget == 0 means unlimited: no eviction occurs regardless of count
//   T28 loaded_count is 0 after all records evicted
//   T29 completed_count never decrements after eviction
//   T30 enqueue_mip_upgrade: no-op when mip_target not strictly higher
//   T31 enqueue_mip_upgrade: evicts stale record + re-queues at higher mip
//   T32 enqueue_mip_upgrade: not-loaded path falls through to plain enqueue
//   T33 PathGuard move ctor: moved-from dtor does not remove the temp file
//   T34 PathGuard move ctor: moved-into guard still cleans up on dtor
//   T35 dedup: re-enqueue after cancel with same path updates correctly
//   T36 enqueue_mip_upgrade does NOT leak old GPU handle (live == loaded)
// =============================================================================

#include <cd/asset/texture_streamer/TextureStreamer.hpp>

#include <cd/rhi/NullDevice.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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

using cd::asset::texture_streamer::StreamRequest;
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

// ---- Movable PathGuard for tests that need std::vector<PathGuard> -----------
// (The anonymous-namespace PathGuard above is intentionally non-movable; the
//  movable variant here is used exclusively in T33/T34 and vector-reserve tests.)

namespace
{

struct MovablePathGuard
{
    fs::path path;

    explicit MovablePathGuard(fs::path p)
        : path { std::move(p) }
    {
    }

    ~MovablePathGuard()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }

    MovablePathGuard(const MovablePathGuard&)            = delete;
    MovablePathGuard& operator=(const MovablePathGuard&) = delete;

    // Move ctor: clear source so the moved-from dtor's fs::remove is a no-op.
    MovablePathGuard(MovablePathGuard&& o) noexcept
        : path { std::move(o.path) }
    {
        o.path.clear();
    }

    MovablePathGuard& operator=(MovablePathGuard&&) = delete;
};

}  // namespace

// ---- T16: screen_coverage biases priority within same priority bucket --------
//
// Two requests share the same priority byte (100). One has screen_coverage=0.0
// (small texture) and the other screen_coverage=1.0 (full-screen texture). The
// full-screen texture must be selected first by tick().

TEST(TextureStreamer, ScreenCoverageBreaksTieInSamePriorityBucket)
{
    PathGuard gsmall { tmp_cdtex_path() };
    PathGuard gbig   { tmp_cdtex_path() };
    write_cdtex(gsmall.path, 4U, 4U, 0x11);
    write_cdtex(gbig.path,   64U, 64U, 0x22);
    const std::string small_path = gsmall.path.string();
    const std::string big_path   = gbig.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // Same priority byte (100). small has coverage 0.0, big has coverage 1.0.
    ts.enqueue(StreamRequest{ small_path, 0U, 100U, 0.0F });
    ts.enqueue(StreamRequest{ big_path,   0U, 100U, 1.0F });
    EXPECT_EQ(ts.pending_count(), 2U);

    // First tick must pick big_path (higher effective priority via coverage).
    ts.tick(0.016F, device);
    EXPECT_TRUE(ts.is_loaded(big_path));
    EXPECT_FALSE(ts.is_loaded(small_path));

    // Second tick drains the remaining entry.
    ts.tick(0.016F, device);
    EXPECT_TRUE(ts.is_loaded(small_path));
    EXPECT_EQ(ts.completed_count(), 2U);
}

// ---- T17: screen_coverage = 0 does not disturb existing priority ordering ---
//
// When screen_coverage is 0 for all requests, the ordering must be identical
// to the pure-priority case (priority byte only, as before Phase 1268).

TEST(TextureStreamer, ScreenCoverageZeroPreservesOriginalPriorityOrdering)
{
    PathGuard glo  { tmp_cdtex_path() };
    PathGuard gmid { tmp_cdtex_path() };
    PathGuard ghi  { tmp_cdtex_path() };
    write_cdtex(glo.path,  4U,  4U, 0xAA);
    write_cdtex(gmid.path, 8U,  8U, 0xBB);
    write_cdtex(ghi.path, 16U, 16U, 0xCC);
    const std::string lo  = glo.path.string();
    const std::string mid = gmid.path.string();
    const std::string hi  = ghi.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // All screen_coverage == 0 → ordering by priority byte only.
    ts.enqueue(StreamRequest{ lo,   0U,  10U, 0.0F });
    ts.enqueue(StreamRequest{ mid,  0U, 100U, 0.0F });
    ts.enqueue(StreamRequest{ hi,   0U, 200U, 0.0F });

    ts.tick(0.016F, device);
    EXPECT_TRUE(ts.is_loaded(hi));
    EXPECT_FALSE(ts.is_loaded(mid));
    EXPECT_FALSE(ts.is_loaded(lo));

    ts.tick(0.016F, device);
    EXPECT_TRUE(ts.is_loaded(mid));

    ts.tick(0.016F, device);
    EXPECT_TRUE(ts.is_loaded(lo));
}

// ---- T18: is_mip_loaded — base mip always resident for mip_target == 0 ------

TEST(TextureStreamer, IsMipLoadedBaseAlwaysResidentForMipTargetZero)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 16U, 16U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue(StreamRequest{ path, 0U, 200U });
    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));

    // mip_target == 0 → only mip level 0 is resident.
    EXPECT_TRUE(ts.is_mip_loaded(path, 0U));
    EXPECT_FALSE(ts.is_mip_loaded(path, 1U));
    EXPECT_FALSE(ts.is_mip_loaded(path, 7U));
}

// ---- T19: is_mip_loaded — levels 0..N-1 resident when mip_target == N -------

TEST(TextureStreamer, IsMipLoadedLevelsZeroToNMinusOneResidentWhenTargetIsN)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 32U, 32U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // mip_target == 4 → levels 0, 1, 2, 3 are resident; level 4 is not.
    ts.enqueue(StreamRequest{ path, 4U, 200U });
    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));

    EXPECT_TRUE(ts.is_mip_loaded(path, 0U));
    EXPECT_TRUE(ts.is_mip_loaded(path, 1U));
    EXPECT_TRUE(ts.is_mip_loaded(path, 2U));
    EXPECT_TRUE(ts.is_mip_loaded(path, 3U));
    EXPECT_FALSE(ts.is_mip_loaded(path, 4U));
    EXPECT_FALSE(ts.is_mip_loaded(path, 255U));
}

// ---- T20: is_mip_loaded — higher levels NOT resident when target too small ---

TEST(TextureStreamer, IsMipLoadedHigherLevelsNotResidentWhenTargetSmall)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 8U, 8U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // mip_target == 2 → only levels 0 and 1 resident.
    ts.enqueue(StreamRequest{ path, 2U, 200U });
    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));

    EXPECT_TRUE(ts.is_mip_loaded(path, 0U));
    EXPECT_TRUE(ts.is_mip_loaded(path, 1U));
    EXPECT_FALSE(ts.is_mip_loaded(path, 2U));
}

// ---- T21: is_mip_loaded — returns false for unknown/unloaded path -----------

TEST(TextureStreamer, IsMipLoadedFalseForUnknownPath)
{
    const TextureStreamer ts;

    EXPECT_FALSE(ts.is_mip_loaded("nonexistent.cdtex", 0U));
    EXPECT_FALSE(ts.is_mip_loaded("nonexistent.cdtex", 1U));
}

// ---- T22: resident_mip_target returns the stored mip_target on success ------

TEST(TextureStreamer, ResidentMipTargetReturnsStoredValue)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 16U, 16U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue(StreamRequest{ path, 3U, 200U });
    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));

    const auto mt = ts.resident_mip_target(path);
    ASSERT_TRUE(mt.has_value());
    EXPECT_EQ(*mt, 3U);
}

// ---- T23: resident_mip_target returns nullopt for unloaded path -------------

TEST(TextureStreamer, ResidentMipTargetNulloptForUnloaded)
{
    const TextureStreamer ts;
    EXPECT_FALSE(ts.resident_mip_target("missing.cdtex").has_value());
}

// ---- T24: set_texture_budget + eviction on tick: loaded_count <= budget -----
//
// Budget is set to 2. Three textures are loaded one per tick. After the
// third tick, loaded_count must still be 2 (the oldest was evicted).
// completed_count must still be 3 (cumulative, never decremented).

TEST(TextureStreamer, BudgetEvictsLRUOnTick)
{
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    PathGuard g3 { tmp_cdtex_path() };
    write_cdtex(g1.path, 8U,  8U, 0x11);
    write_cdtex(g2.path, 16U, 16U, 0x22);
    write_cdtex(g3.path, 32U, 32U, 0x33);
    const std::string p1 = g1.path.string();
    const std::string p2 = g2.path.string();
    const std::string p3 = g3.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;
    ts.set_texture_budget(2U);

    EXPECT_EQ(ts.texture_budget(), 2U);

    ts.enqueue(StreamRequest{ p1, 0U, 100U });
    ts.tick(0.016F, device);
    EXPECT_EQ(ts.loaded_count(), 1U);
    EXPECT_EQ(ts.completed_count(), 1U);

    ts.enqueue(StreamRequest{ p2, 0U, 100U });
    ts.tick(0.016F, device);
    EXPECT_EQ(ts.loaded_count(), 2U);
    EXPECT_EQ(ts.completed_count(), 2U);

    // Third load — budget is 2; p1 (LRU) must be evicted before p3 is added.
    ts.enqueue(StreamRequest{ p3, 0U, 100U });
    ts.tick(0.016F, device);

    EXPECT_EQ(ts.loaded_count(),   2U);   // budget enforced
    EXPECT_EQ(ts.completed_count(), 3U);  // cumulative, not decremented

    // p3 must be loaded (just added).
    EXPECT_TRUE(ts.is_loaded(p3));
    // p2 must still be loaded (was loaded second — older than p3 but newer than p1).
    EXPECT_TRUE(ts.is_loaded(p2));
    // p1 is the LRU — it must have been evicted.
    EXPECT_FALSE(ts.is_loaded(p1));

    // NullDevice: destroy_texture was called for p1 → live count == 2.
    EXPECT_EQ(device.live_texture_count(), 2U);
}

// ---- T25: evict_lru: least-recently-used record evicted, handle destroyed ---
//
// Two textures loaded. evict_lru() is called once and must evict the older
// (LRU) texture, destroying its GPU handle and returning its path.

TEST(TextureStreamer, EvictLruRemovesOldestEntry)
{
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    write_cdtex(g1.path, 8U, 8U, 0xAA);
    write_cdtex(g2.path, 16U, 16U, 0xBB);
    const std::string p1 = g1.path.string();
    const std::string p2 = g2.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue(StreamRequest{ p1, 0U, 100U });
    ts.tick(0.016F, device);   // p1 loaded first → LRU after p2 arrives

    ts.enqueue(StreamRequest{ p2, 0U, 100U });
    ts.tick(0.016F, device);   // p2 loaded second → MRU

    EXPECT_EQ(ts.loaded_count(), 2U);
    EXPECT_EQ(device.live_texture_count(), 2U);

    // Evict manually — must evict p1 (the LRU).
    const std::string evicted = ts.evict_lru(device);
    EXPECT_EQ(evicted, p1);

    EXPECT_EQ(ts.loaded_count(), 1U);
    EXPECT_FALSE(ts.is_loaded(p1));
    EXPECT_TRUE(ts.is_loaded(p2));
    EXPECT_EQ(device.live_texture_count(), 1U);

    // completed_count must NOT have changed (cumulative).
    EXPECT_EQ(ts.completed_count(), 2U);
}

// ---- T26: touch promotes a texture to MRU, protecting it from LRU eviction --
//
// p1 is loaded first (LRU). After touch(p1), it becomes MRU. A subsequent
// evict_lru() must evict p2 (the new LRU) instead.

TEST(TextureStreamer, TouchPromotesToMruProtectsFromEviction)
{
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    write_cdtex(g1.path, 8U, 8U, 0xAA);
    write_cdtex(g2.path, 16U, 16U, 0xBB);
    const std::string p1 = g1.path.string();
    const std::string p2 = g2.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue(StreamRequest{ p1, 0U, 100U });
    ts.tick(0.016F, device);  // p1 = LRU

    ts.enqueue(StreamRequest{ p2, 0U, 100U });
    ts.tick(0.016F, device);  // p2 = MRU, p1 = LRU

    // Touch p1 → p1 becomes MRU, p2 becomes LRU.
    ts.touch(p1);

    // Evict should now remove p2 (the new LRU).
    const std::string evicted = ts.evict_lru(device);
    EXPECT_EQ(evicted, p2);

    EXPECT_TRUE(ts.is_loaded(p1));
    EXPECT_FALSE(ts.is_loaded(p2));
}

// ---- T27: budget == 0 means unlimited: no eviction regardless of count ------

TEST(TextureStreamer, BudgetZeroIsUnlimitedNoEviction)
{
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    PathGuard g3 { tmp_cdtex_path() };
    write_cdtex(g1.path, 4U, 4U);
    write_cdtex(g2.path, 4U, 4U);
    write_cdtex(g3.path, 4U, 4U);

    cd::rhi::NullDevice device;
    TextureStreamer      ts;
    ts.set_texture_budget(0U);  // explicitly unlimited

    ts.enqueue(StreamRequest{ g1.path.string(), 0U, 100U });
    ts.tick(0.016F, device);
    ts.enqueue(StreamRequest{ g2.path.string(), 0U, 100U });
    ts.tick(0.016F, device);
    ts.enqueue(StreamRequest{ g3.path.string(), 0U, 100U });
    ts.tick(0.016F, device);

    // All three must remain loaded — no eviction at budget == 0.
    EXPECT_EQ(ts.loaded_count(),   3U);
    EXPECT_EQ(ts.completed_count(), 3U);
    EXPECT_EQ(device.live_texture_count(), 3U);
}

// ---- T28: loaded_count is 0 after all records evicted -----------------------

TEST(TextureStreamer, LoadedCountZeroAfterAllEvicted)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 8U, 8U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue(StreamRequest{ path, 0U, 100U });
    ts.tick(0.016F, device);
    EXPECT_EQ(ts.loaded_count(), 1U);

    ts.evict_lru(device);
    EXPECT_EQ(ts.loaded_count(), 0U);

    // Second evict on empty pool — must be a no-op (not crash).
    const std::string second = ts.evict_lru(device);
    EXPECT_TRUE(second.empty());
    EXPECT_EQ(ts.loaded_count(), 0U);
}

// ---- T29: completed_count never decrements after eviction -------------------

TEST(TextureStreamer, CompletedCountNeverDecrementsAfterEviction)
{
    PathGuard g1 { tmp_cdtex_path() };
    PathGuard g2 { tmp_cdtex_path() };
    write_cdtex(g1.path, 8U, 8U);
    write_cdtex(g2.path, 8U, 8U);

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue(StreamRequest{ g1.path.string(), 0U, 100U });
    ts.tick(0.016F, device);
    ts.enqueue(StreamRequest{ g2.path.string(), 0U, 100U });
    ts.tick(0.016F, device);
    EXPECT_EQ(ts.completed_count(), 2U);

    // Evict both.
    ts.evict_lru(device);
    ts.evict_lru(device);

    // completed_count must still be 2 — eviction never decrements it.
    EXPECT_EQ(ts.completed_count(), 2U);
    EXPECT_EQ(ts.loaded_count(), 0U);
}

// ---- T30: enqueue_mip_upgrade — no-op when mip_target not strictly higher ---
//
// A texture is loaded with mip_target == 3. Calling enqueue_mip_upgrade with
// mip_target == 2 (lower) or mip_target == 3 (equal) must be a no-op: the
// texture stays loaded, pending_count stays 0.

TEST(TextureStreamer, EnqueueMipUpgradeNoOpWhenNotHigher)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 32U, 32U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue(StreamRequest{ path, 3U, 200U });
    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(*ts.resident_mip_target(path), 3U);

    // Downgrade attempt — must be no-op.
    ts.enqueue_mip_upgrade(StreamRequest{ path, 2U, 200U });
    EXPECT_EQ(ts.pending_count(), 0U);
    EXPECT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(*ts.resident_mip_target(path), 3U);

    // Equal attempt — must also be no-op.
    ts.enqueue_mip_upgrade(StreamRequest{ path, 3U, 200U });
    EXPECT_EQ(ts.pending_count(), 0U);
    EXPECT_TRUE(ts.is_loaded(path));
}

// ---- T31: enqueue_mip_upgrade — evicts stale record + re-queues at higher ---
//
// Texture is loaded at mip_target == 1. Upgrade to mip_target == 4 must:
//   1. Remove the old loaded record.
//   2. Put path back in pending_map_ at mip_target == 4.
//   3. After a fresh tick(), the resident_mip_target == 4.

TEST(TextureStreamer, EnqueueMipUpgradeEvictsAndRequeues)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 64U, 64U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // Load at mip_target == 1.
    ts.enqueue(StreamRequest{ path, 1U, 200U });
    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(*ts.resident_mip_target(path), 1U);
    EXPECT_EQ(ts.completed_count(), 1U);

    // Request upgrade to mip_target == 4.
    ts.enqueue_mip_upgrade(StreamRequest{ path, 4U, 200U });

    // Old record must be gone, new request must be pending.
    EXPECT_FALSE(ts.is_loaded(path));
    EXPECT_EQ(ts.pending_count(), 1U);

    // Decode + upload the upgraded texture.
    ts.tick(0.016F, device);

    ASSERT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(*ts.resident_mip_target(path), 4U);
    EXPECT_EQ(ts.completed_count(), 2U);  // cumulative: first + upgraded load

    // is_mip_loaded: levels 0..3 resident, level 4 not.
    EXPECT_TRUE(ts.is_mip_loaded(path, 0U));
    EXPECT_TRUE(ts.is_mip_loaded(path, 3U));
    EXPECT_FALSE(ts.is_mip_loaded(path, 4U));
}

// ---- T32: enqueue_mip_upgrade — not-loaded path falls through to enqueue ----
//
// A path that has never been loaded is passed to enqueue_mip_upgrade. The
// behaviour must be identical to a plain enqueue: the path enters pending_map_
// and loads on the next tick.

TEST(TextureStreamer, EnqueueMipUpgradeNotLoadedFallsThroughToEnqueue)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 16U, 16U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // Path not yet loaded — enqueue_mip_upgrade must behave like enqueue.
    ts.enqueue_mip_upgrade(StreamRequest{ path, 2U, 150U });
    EXPECT_EQ(ts.pending_count(), 1U);

    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(*ts.resident_mip_target(path), 2U);
}

// ---- T33: PathGuard move ctor clears source path (no double-remove) ---------
//
// After a move, the moved-from PathGuard's path must be empty so its dtor does
// not attempt fs::remove on a path that the destination now owns.
// (Tests the movable PathGuard used in async tests / vector::reserve paths.)

TEST(TextureStreamer, MovablePathGuardMovedFromHasEmptyPath)
{
    const fs::path p = tmp_cdtex_path();
    write_cdtex(p, 4U, 4U);

    MovablePathGuard src { p };
    EXPECT_FALSE(src.path.empty());

    MovablePathGuard dst { std::move(src) };

    // Source must be cleared. Intentionally inspecting the moved-from object:
    // the whole point of this test is that the move ctor empties the source.
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    EXPECT_TRUE(src.path.empty());

    // Destination must own the original path.
    EXPECT_EQ(dst.path, p);

    // File must still exist (only dst's dtor will remove it).
    EXPECT_TRUE(fs::exists(p));
}  // dst dtor removes the file here.

// ---- T34: MovablePathGuard destination dtor removes the file ----------------

TEST(TextureStreamer, MovablePathGuardDestinationDtorCleansUp)
{
    const fs::path p = tmp_cdtex_path();
    write_cdtex(p, 4U, 4U);
    ASSERT_TRUE(fs::exists(p));

    {
        MovablePathGuard src { p };
        MovablePathGuard dst { std::move(src) };
        // File exists inside the scope.
        EXPECT_TRUE(fs::exists(p));
    }  // dst dtor runs here — file must be removed.

    EXPECT_FALSE(fs::exists(p));
}

// ---- T35: dedup — re-enqueue after cancel with same path picks up cleanly ---
//
// enqueue(path) → cancel(path) → enqueue(path) again must work: the second
// enqueue must put the path back in pending_map_, and a subsequent tick() must
// load it successfully.

TEST(TextureStreamer, ReEnqueueAfterCancelLoadsCorrectly)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 32U, 32U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    ts.enqueue(StreamRequest{ path, 0U, 100U });
    EXPECT_EQ(ts.pending_count(), 1U);

    ts.cancel(path);
    EXPECT_EQ(ts.pending_count(), 0U);
    EXPECT_FALSE(ts.is_loaded(path));

    // Re-enqueue: must succeed (path is neither pending nor loaded now).
    ts.enqueue(StreamRequest{ path, 0U, 200U });
    EXPECT_EQ(ts.pending_count(), 1U);

    ts.tick(0.016F, device);
    EXPECT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(ts.completed_count(), 1U);
}

// ---- T36: enqueue_mip_upgrade does NOT leak the old GPU handle ---------------
//
// Regression for the deferred-destroy fix: enqueue_mip_upgrade() has no device
// parameter, so the orphaned old handle is queued and destroyed on the next
// tick(). After an upgrade round-trip live_texture_count() (GPU handles still
// alive on the device) must equal loaded_count() — exactly one resident
// texture, not the old leaked handle + the new one.

TEST(TextureStreamer, EnqueueMipUpgradeDoesNotLeakGpuHandle)
{
    PathGuard g { tmp_cdtex_path() };
    write_cdtex(g.path, 64U, 64U);
    const std::string path = g.path.string();

    cd::rhi::NullDevice device;
    TextureStreamer      ts;

    // Load at mip_target == 1 → one live GPU handle.
    ts.enqueue(StreamRequest{ path, 1U, 200U });
    ts.tick(0.016F, device);
    ASSERT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(device.live_texture_count(), 1U);
    EXPECT_EQ(ts.loaded_count(), 1U);

    // Upgrade to mip_target == 4. The old handle is orphaned (device-less path)
    // but must be destroyed on the next tick(), not leaked.
    ts.enqueue_mip_upgrade(StreamRequest{ path, 4U, 200U });
    EXPECT_FALSE(ts.is_loaded(path));
    EXPECT_EQ(ts.pending_count(), 1U);

    // Re-decode + upload the upgraded texture. tick() drains the deferred-destroy
    // queue first, then creates the new handle.
    ts.tick(0.016F, device);

    ASSERT_TRUE(ts.is_loaded(path));
    EXPECT_EQ(*ts.resident_mip_target(path), 4U);

    // The load-bearing assertion: no leak. live (device) == loaded (streamer).
    EXPECT_EQ(ts.loaded_count(), 1U);
    EXPECT_EQ(device.live_texture_count(), 1U);
    EXPECT_EQ(device.live_texture_count(), ts.loaded_count());
}
