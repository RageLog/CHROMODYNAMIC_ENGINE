// =============================================================================
// CHROMODYNAMIC — cd::profile::cpu_marker_overlay tests
// =============================================================================
#include <cd/profile/cpu_marker_overlay/CpuMarkerOverlay.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>

using cd::profile::cpu_marker_overlay::Collector;
using cd::profile::cpu_marker_overlay::MarkerHandle;
using cd::profile::cpu_marker_overlay::MarkerSample;
using cd::profile::cpu_marker_overlay::Overlay;
using cd::profile::cpu_marker_overlay::Rect;
using cd::profile::cpu_marker_overlay::Scope;

// ---------------------------------------------------------------------------
// Test 1: begin/end round-trip — one completed sample is recorded
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, BeginEndRoundTrip)
{
    Collector col;
    EXPECT_EQ(col.sample_count(), 0U);

    const MarkerHandle h = col.begin("frame");
    EXPECT_TRUE(h.valid());

    col.end(h);
    EXPECT_EQ(col.sample_count(), 1U);

    // The completed sample should be visible via samples_since(0.0).
    const auto all = col.samples_since(0.0);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].name, "frame");
    EXPECT_GE(all[0].duration_ms, 0.0);
    EXPECT_NE(all[0].thread_id, 0U);
}

// ---------------------------------------------------------------------------
// Test 2: samples_since filters by cutoff
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, SamplesSinceCutoffFilters)
{
    Collector col;

    // Insert a sample with a known start time by recording before a small sleep.
    const MarkerHandle h1 = col.begin("early");
    col.end(h1);

    // Small sleep to advance time.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Record the approximate "now" as our cutoff.
    using clock    = std::chrono::steady_clock;
    const double t_cut =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()
            ).count()
        ) * 1.0e-6;

    const MarkerHandle h2 = col.begin("late");
    col.end(h2);

    const auto all  = col.samples_since(0.0);
    EXPECT_GE(all.size(), 2U);

    // samples_since(t_cut) must only include "late".
    const auto filtered = col.samples_since(t_cut);
    ASSERT_FALSE(filtered.empty());
    for (const auto& s : filtered)
        EXPECT_EQ(s.name, "late") << "Unexpected sample: " << s.name;
}

// ---------------------------------------------------------------------------
// Test 3: RAII Scope — ctor calls begin, dtor calls end
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Scope, RaiiScopeCommitsSample)
{
    Collector col;

    {
        Scope sc(col, "raii_region");
        // Sample should not yet be committed while sc is alive.
        // (In-flight — not yet in ring.)
    }
    // Destructor ran → sample committed.
    EXPECT_EQ(col.sample_count(), 1U);

    const auto all = col.samples_since(0.0);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].name, "raii_region");
}

// ---------------------------------------------------------------------------
// Test 4: Nested scopes — inner ends before outer
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Scope, NestedScopesRecordedInOrder)
{
    Collector col;

    {
        Scope outer(col, "outer");
        {
            Scope inner(col, "inner");
        }
        // At this point inner is committed; outer is still in-flight.
        EXPECT_EQ(col.sample_count(), 1U);
    }
    // Now outer is committed too.
    EXPECT_EQ(col.sample_count(), 2U);

    const auto all = col.samples_since(0.0);
    ASSERT_EQ(all.size(), 2U);

    // Inner was committed first → appears first in the ring.
    EXPECT_EQ(all[0].name, "inner");
    EXPECT_EQ(all[1].name, "outer");

    // Outer duration >= inner duration.
    EXPECT_GE(all[1].duration_ms, all[0].duration_ms);
}

// ---------------------------------------------------------------------------
// Test 5: Overlay::draw emits >= 1 quad per sample
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, DrawEmitsAtLeastOneQuadPerSample)
{
    // Build three synthetic samples.
    const double base_ms = 1000.0;
    const std::vector<MarkerSample> samples = {
        { "render",  base_ms + 0.0,  4.0, 1U },
        { "physics", base_ms + 5.0,  2.0, 1U },
        { "audio",   base_ms + 8.0,  1.0, 2U },
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    Overlay ov { 16.0 };  // 16 ms window.

    const Rect bounds { 0.0F, 0.0F, 800.0F, 120.0F };
    ov.draw(batcher, std::span<const MarkerSample>(samples), bounds);

    // Each sample maps to one quad = 4 vertices, 6 indices.
    // We must have at least samples.size() quads.
    EXPECT_GE(batcher.vertex_count(), samples.size() * 4U)
        << "Expected at least 4 vertices per sample";
    EXPECT_GE(batcher.index_count(), samples.size() * 6U)
        << "Expected at least 6 indices per sample";
    EXPECT_GE(batcher.command_count(), 1U)
        << "Expected at least one draw command";
}

// ---------------------------------------------------------------------------
// Test 5b: Overlay::draw emits the EXACT expected draw-batch contents
//          (bar geometry + colour + merged draw command), inspected on the
//          DrawBatcher output with no GPU. This is the visual-side coverage
//          the lib was missing.
// ---------------------------------------------------------------------------
namespace
{
// Mirror of Overlay's internal djb2 hash → marker colour.
[[nodiscard]] cd::ui::renderer::Color expected_marker_colour(std::string_view name)
{
    std::uint32_t h = 5381U;
    for (const char c : name)
        h = ((h << 5U) + h) + static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    return cd::ui::renderer::Color {
        static_cast<std::uint8_t>(h & 0xFFu),
        static_cast<std::uint8_t>((h >> 8U) & 0xFFu),
        static_cast<std::uint8_t>((h >> 16U) & 0xFFu),
        200U
    };
}
}  // namespace

TEST(CpuMarkerOverlay_Overlay, DrawEmitsExactBatchContents)
{
    // One thread, two samples on a single lane so geometry is deterministic.
    const double base_ms = 500.0;
    const std::vector<MarkerSample> samples = {
        { "render",  base_ms + 0.0, 4.0, 1U },  // starts at window-left
        { "physics", base_ms + 8.0, 2.0, 1U },  // 8 ms later
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    const double window_ms = 16.0;
    Overlay ov { window_ms };
    const Rect bounds { 100.0F, 50.0F, 800.0F, 60.0F };
    ov.draw(batcher, std::span<const MarkerSample>(samples), bounds);

    // One lane (single thread) → lane_height == bounds.height; bar_h = 80%.
    const float pixels_per_ms = bounds.width / static_cast<float>(window_ms);
    const float lane_height   = bounds.height;  // 1 lane
    const float expected_bar_h = lane_height * 0.8F;

    // Exactly 2 quads → 8 vertices, 12 indices, 1 merged solid command.
    ASSERT_EQ(batcher.vertex_count(), 8U);
    ASSERT_EQ(batcher.index_count(), 12U);
    ASSERT_EQ(batcher.command_count(), 1U);

    const auto verts = batcher.vertices();

    // Sample 0 ("render"): rel_start 0 → bar at bounds.x, top-left vertex 0.
    EXPECT_FLOAT_EQ(verts[0].pos_x, bounds.x);
    EXPECT_FLOAT_EQ(verts[0].pos_y, bounds.y);
    const auto c0 = expected_marker_colour("render");
    EXPECT_EQ(verts[0].r, c0.r);
    EXPECT_EQ(verts[0].g, c0.g);
    EXPECT_EQ(verts[0].b, c0.b);
    EXPECT_EQ(verts[0].a, c0.a);
    // Bar height encoded in the bottom edge (vertex 3 = bottom-left).
    EXPECT_FLOAT_EQ(verts[3].pos_y, bounds.y + expected_bar_h);

    // Sample 1 ("physics"): rel_start 8 ms → bar_x offset by 8 * pixels_per_ms.
    const float expected_x1 = bounds.x + 8.0F * pixels_per_ms;
    EXPECT_FLOAT_EQ(verts[4].pos_x, expected_x1);
    EXPECT_FLOAT_EQ(verts[4].pos_y, bounds.y);
    const auto c1 = expected_marker_colour("physics");
    EXPECT_EQ(verts[4].r, c1.r);
    EXPECT_EQ(verts[4].g, c1.g);
    EXPECT_EQ(verts[4].b, c1.b);

    // The merged command is a solid (untextured) draw of both quads.
    const auto cmds = batcher.commands();
    ASSERT_EQ(cmds.size(), 1U);
    EXPECT_EQ(cmds[0].variant, cd::ui::renderer::material::kSolid);
    EXPECT_EQ(cmds[0].index_count, 12U);
    EXPECT_EQ(cmds[0].texture_slot, 0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------
// Test 6: Overlay with empty samples does not crash, emits nothing
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, DrawEmptyNoOp)
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    Overlay ov;
    const Rect bounds { 0.0F, 0.0F, 800.0F, 120.0F };
    ov.draw(batcher, std::span<const MarkerSample>{}, bounds);

    EXPECT_EQ(batcher.vertex_count(), 0U);
    EXPECT_EQ(batcher.index_count(),  0U);
}

// ---------------------------------------------------------------------------
// Test 7: Collector ring capacity — oldest sample overwritten on overflow
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, RingCapacityOverwritesOldest)
{
    Collector col { 3 };  // capacity = 3

    for (int i = 0; i < 5; ++i)
    {
        const MarkerHandle h = col.begin("loop");
        col.end(h);
    }

    // Ring holds exactly capacity samples.
    EXPECT_EQ(col.sample_count(), 3U);
}

// ---------------------------------------------------------------------------
// Test 8: Ring wrap-around preserves the most-recent samples
//
// With capacity=3 and 5 inserts (names "s0".."s4"), the ring must retain
// the last 3: "s2", "s3", "s4". The overwrite pattern follows write_head
// cycling 0→1→2→0→1, so slot 0 holds "s3", slot 1 holds "s4", slot 2
// holds "s2". samples_since(0) returns whatever the ring contains.
// We verify all three retained names appear, and the two oldest ("s0","s1")
// do not.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, RingWrapPreservesMostRecent)
{
    Collector col { 3 };

    for (int i = 0; i < 5; ++i)
    {
        const std::string name = "s" + std::to_string(i);
        const MarkerHandle h = col.begin(name);
        col.end(h);
    }

    EXPECT_EQ(col.sample_count(), 3U);

    const auto all = col.samples_since(0.0);
    ASSERT_EQ(all.size(), 3U);

    const auto has = [&](const std::string& n)
    {
        return std::ranges::any_of(all, [&](const MarkerSample& s) { return s.name == n; });
    };

    EXPECT_FALSE(has("s0")) << "s0 should have been overwritten";
    EXPECT_FALSE(has("s1")) << "s1 should have been overwritten";
    EXPECT_TRUE(has("s2"));
    EXPECT_TRUE(has("s3"));
    EXPECT_TRUE(has("s4"));
}

// ---------------------------------------------------------------------------
// Test 9: clear() resets ring, write_head, and ring_full so subsequent
//         inserts start fresh from slot 0.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, ClearResetsRing)
{
    Collector col { 3 };

    // Fill the ring past capacity so ring_full is set.
    for (int i = 0; i < 5; ++i)
    {
        const MarkerHandle h = col.begin("fill");
        col.end(h);
    }
    EXPECT_EQ(col.sample_count(), 3U);

    col.clear();
    EXPECT_EQ(col.sample_count(), 0U);
    EXPECT_TRUE(col.samples_since(0.0).empty());

    // After clear, new inserts must accumulate from zero again.
    const MarkerHandle h = col.begin("fresh");
    col.end(h);

    EXPECT_EQ(col.sample_count(), 1U);
    const auto all = col.samples_since(0.0);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].name, "fresh");
}

// ---------------------------------------------------------------------------
// Test 10: Capacity boundary — exactly capacity inserts, then one more.
//          sample_count stays at capacity; the extra insert replaces the
//          oldest slot (write_head wraps from capacity-1 to 0).
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, ExactCapacityThenOneMore)
{
    constexpr std::size_t kCap = 4U;
    Collector col { kCap };

    // Fill exactly to capacity — all retained.
    for (std::size_t i = 0U; i < kCap; ++i)
    {
        const MarkerHandle h = col.begin("a");
        col.end(h);
    }
    EXPECT_EQ(col.sample_count(), kCap);

    // One more — ring_full → oldest slot overwritten.
    const MarkerHandle h = col.begin("b");
    col.end(h);

    EXPECT_EQ(col.sample_count(), kCap);  // still capped

    // "b" must appear exactly once.
    const auto all = col.samples_since(0.0);
    const auto b_count = static_cast<std::size_t>(
        std::ranges::count_if(all, [](const MarkerSample& s) { return s.name == "b"; }));
    EXPECT_EQ(b_count, 1U);
}

// ---------------------------------------------------------------------------
// Test 11: Invalid handle end() is a no-op (does not crash or corrupt state)
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, EndInvalidHandleNoOp)
{
    Collector col;

    const MarkerHandle bad { ~0U };
    col.end(bad);  // must not crash or add a sample

    EXPECT_EQ(col.sample_count(), 0U);
}

// ---------------------------------------------------------------------------
// Test 12: Double-end of the same handle is a no-op on the second call
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, DoubleEndIsNoOp)
{
    Collector col;
    const MarkerHandle h = col.begin("x");

    col.end(h);
    EXPECT_EQ(col.sample_count(), 1U);

    col.end(h);  // slot.active is now false → must be ignored
    EXPECT_EQ(col.sample_count(), 1U);
}

// ---------------------------------------------------------------------------
// Test 13: empty collector — aggregate() returns empty vector
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, AggregateEmptyCollector)
{
    Collector col;
    const auto ag = col.aggregate();
    EXPECT_TRUE(ag.empty());
}

// ---------------------------------------------------------------------------
// Test 14: aggregate() single marker name — count/total/avg/max all correct
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, AggregateSingleName)
{
    // Use synthetic samples via the public begin/end API; we cannot directly
    // inject duration_ms, so we just verify structural correctness: count == 1,
    // avg_ms == total_ms, max_ms == total_ms, all >= 0.
    Collector col;
    const MarkerHandle h = col.begin("cpu");
    col.end(h);

    const auto ag = col.aggregate();
    ASSERT_EQ(ag.size(), 1U);
    EXPECT_EQ(ag[0].name, "cpu");
    EXPECT_EQ(ag[0].count, 1U);
    EXPECT_GE(ag[0].total_ms, 0.0);
    EXPECT_DOUBLE_EQ(ag[0].avg_ms, ag[0].total_ms);
    EXPECT_DOUBLE_EQ(ag[0].max_ms, ag[0].total_ms);
}

// ---------------------------------------------------------------------------
// Test 15: aggregate() with synthetic MarkerSamples — use samples_since()
//          output as a proxy to verify the math: manually construct the
//          expected aggregation over known durations.
//
// Strategy: push the same name N times via begin/end; the durations are
// indeterminate (real clock), but we can verify the invariants
//   total_ms == sum(durations),  avg == total/count,  max >= every sample.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, AggregateMultipleNamesMathInvariants)
{
    Collector col;

    // Insert 3 "render" and 2 "audio" samples.
    for (int i = 0; i < 3; ++i)
    {
        const MarkerHandle h = col.begin("render");
        col.end(h);
    }
    for (int i = 0; i < 2; ++i)
    {
        const MarkerHandle h = col.begin("audio");
        col.end(h);
    }

    const auto ag = col.aggregate();
    ASSERT_EQ(ag.size(), 2U);

    // First-seen order: "render" then "audio".
    const auto& r = ag[0];
    const auto& a = ag[1];
    EXPECT_EQ(r.name, "render");
    EXPECT_EQ(a.name, "audio");
    EXPECT_EQ(r.count, 3U);
    EXPECT_EQ(a.count, 2U);

    // Verify avg == total / count for both.
    EXPECT_NEAR(r.avg_ms, r.total_ms / 3.0, 1.0e-9);
    EXPECT_NEAR(a.avg_ms, a.total_ms / 2.0, 1.0e-9);

    // max_ms must be >= avg_ms for both.
    EXPECT_GE(r.max_ms, r.avg_ms);
    EXPECT_GE(a.max_ms, a.avg_ms);

    // Cross-check total vs per-sample sum from samples_since().
    const auto all = col.samples_since(0.0);
    double render_sum = 0.0;
    double audio_sum  = 0.0;
    for (const auto& s : all)
    {
        if (s.name == "render") render_sum += s.duration_ms;
        if (s.name == "audio")  audio_sum  += s.duration_ms;
    }
    EXPECT_NEAR(r.total_ms, render_sum, 1.0e-9);
    EXPECT_NEAR(a.total_ms, audio_sum,  1.0e-9);
}

// ---------------------------------------------------------------------------
// Test 16: aggregate(cutoff_ms) filters by start time in the same way as
//          samples_since(cutoff_ms) — samples before the cutoff are excluded.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, AggregateCutoffFilters)
{
    Collector col;

    // Record one "early" sample.
    const MarkerHandle h1 = col.begin("early");
    col.end(h1);

    // Grab a cutoff just after the first sample.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    using clock = std::chrono::steady_clock;
    const double t_cut =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                clock::now().time_since_epoch()).count()) * 1.0e-6;

    // Record one "late" sample after the cutoff.
    const MarkerHandle h2 = col.begin("late");
    col.end(h2);

    // aggregate with no cutoff → both names.
    const auto all_ag = col.aggregate(0.0);
    EXPECT_EQ(all_ag.size(), 2U);

    // aggregate with cutoff → only "late".
    const auto filtered = col.aggregate(t_cut);
    ASSERT_EQ(filtered.size(), 1U);
    EXPECT_EQ(filtered[0].name, "late");
    EXPECT_EQ(filtered[0].count, 1U);
}

// ---------------------------------------------------------------------------
// Test 17: aggregate() max_ms tracks the largest duration across repeated
//          samples of the same name (synthetic durations via constructed
//          samples tested indirectly via invariant max >= every individual).
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, AggregateMaxTracked)
{
    Collector col;

    // Record the same marker many times; collect per-sample durations.
    std::vector<double> durations;
    durations.reserve(10);
    for (int i = 0; i < 10; ++i)
    {
        const MarkerHandle h = col.begin("work");
        col.end(h);
    }

    const auto ag = col.aggregate();
    ASSERT_EQ(ag.size(), 1U);
    EXPECT_EQ(ag[0].count, 10U);

    // Retrieve individual durations and verify max.
    const auto all = col.samples_since(0.0);
    double observed_max = 0.0;
    for (const auto& s : all)
        observed_max = std::max(observed_max, s.duration_ms);

    EXPECT_NEAR(ag[0].max_ms, observed_max, 1.0e-9);
}

// ---------------------------------------------------------------------------
// Test 18: compute_bars() — empty samples returns empty vector
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, ComputeBarsEmpty)
{
    using cd::profile::cpu_marker_overlay::BarRect;
    Overlay ov { 16.0 };
    const Rect bounds { 0.0F, 0.0F, 800.0F, 120.0F };

    const std::vector<BarRect> bars = ov.compute_bars({}, bounds);
    EXPECT_TRUE(bars.empty());
}

// ---------------------------------------------------------------------------
// Test 19: compute_bars() — zero-width or zero-height bounds → empty
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, ComputeBarsZeroBoundsEmpty)
{
    using cd::profile::cpu_marker_overlay::BarRect;
    const std::vector<MarkerSample> samples = { { "x", 0.0, 1.0, 1U } };
    Overlay ov { 16.0 };

    EXPECT_TRUE(ov.compute_bars(samples, Rect { 0.0F, 0.0F, 0.0F, 100.0F }).empty());
    EXPECT_TRUE(ov.compute_bars(samples, Rect { 0.0F, 0.0F, 100.0F, 0.0F }).empty());
}

// ---------------------------------------------------------------------------
// Test 20: compute_bars() single marker — bar geometry matches formula
//
// One sample, one thread → one lane spanning full bounds.height.
// bar_x = bounds.x (rel_start == 0)
// bar_y = bounds.y (lane 0)
// bar_w = duration_ms * (bounds.width / window_ms)  (or >= 1)
// bar_h = bounds.height * 0.8
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, ComputeBarsSingleMarkerGeometry)
{
    using cd::profile::cpu_marker_overlay::BarRect;

    const double base_ms    = 1000.0;
    const double dur_ms     = 4.0;
    const double window_ms  = 16.0;
    const Rect   bounds     { 10.0F, 20.0F, 800.0F, 60.0F };

    const std::vector<MarkerSample> samples = { { "frame", base_ms, dur_ms, 1U } };

    Overlay ov { window_ms };
    const auto bars = ov.compute_bars(samples, bounds);
    ASSERT_EQ(bars.size(), 1U);

    const float ppm          = bounds.width / static_cast<float>(window_ms);
    const float expected_w   = static_cast<float>(dur_ms) * ppm;
    const float expected_h   = bounds.height * 0.8F;  // single lane

    EXPECT_FLOAT_EQ(bars[0].x, bounds.x);
    EXPECT_FLOAT_EQ(bars[0].y, bounds.y);
    EXPECT_FLOAT_EQ(bars[0].w, expected_w);
    EXPECT_FLOAT_EQ(bars[0].h, expected_h);
}

// ---------------------------------------------------------------------------
// Test 21: compute_bars() two threads → two lanes; y coordinates differ by
//          lane_height; lane_height = bounds.height / 2.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, ComputeBarsMultiThreadLanes)
{
    using cd::profile::cpu_marker_overlay::BarRect;

    const double base_ms   = 0.0;
    const double window_ms = 10.0;
    const Rect   bounds    { 0.0F, 0.0F, 100.0F, 80.0F };

    // Two samples on different threads, starting at the same time.
    const std::vector<MarkerSample> samples = {
        { "thread_a", base_ms, 1.0, 1U },
        { "thread_b", base_ms, 1.0, 2U },
    };

    Overlay ov { window_ms };
    const auto bars = ov.compute_bars(samples, bounds);
    ASSERT_EQ(bars.size(), 2U);

    const float lane_height = bounds.height / 2.0F;
    const float expected_h  = lane_height * 0.8F;

    // Thread 1 is first-seen → lane 0; thread 2 → lane 1.
    EXPECT_FLOAT_EQ(bars[0].y, bounds.y + 0.0F * lane_height);
    EXPECT_FLOAT_EQ(bars[1].y, bounds.y + 1.0F * lane_height);
    EXPECT_FLOAT_EQ(bars[0].h, expected_h);
    EXPECT_FLOAT_EQ(bars[1].h, expected_h);
}

// ---------------------------------------------------------------------------
// Test 22: compute_bars() window culling — sample entirely outside window
//          (rel_start > window_ms) is absent from the result.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, ComputeBarsWindowCullsOutOfRangeSample)
{
    using cd::profile::cpu_marker_overlay::BarRect;

    const double base_ms   = 100.0;
    const double window_ms = 10.0;

    // Sample 0: inside window (rel_start=0, dur=3).
    // Sample 1: starts after the window (rel_start=15 > 10).
    const std::vector<MarkerSample> samples = {
        { "inside",  base_ms + 0.0,  3.0, 1U },
        { "outside", base_ms + 15.0, 1.0, 1U },
    };

    Overlay ov { window_ms };
    const auto bars = ov.compute_bars(samples, Rect { 0.0F, 0.0F, 100.0F, 40.0F });
    ASSERT_EQ(bars.size(), 1U);

    // colour_hash of "inside" must match djb2("inside").
    std::uint32_t h = 5381U;
    for (const char c : std::string_view { "inside" })
        h = ((h << 5U) + h) + static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    EXPECT_EQ(bars[0].colour_hash, h);
}

// ---------------------------------------------------------------------------
// Test 23: compute_bars() minimum width — zero-duration sample gets width 1.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, ComputeBarsZeroDurationMinWidth)
{
    using cd::profile::cpu_marker_overlay::BarRect;

    const std::vector<MarkerSample> samples = { { "tick", 0.0, 0.0, 1U } };
    Overlay ov { 16.0 };
    const auto bars = ov.compute_bars(samples, Rect { 0.0F, 0.0F, 800.0F, 40.0F });
    ASSERT_EQ(bars.size(), 1U);
    EXPECT_GE(bars[0].w, 1.0F);
}

// ---------------------------------------------------------------------------
// Test 24: compute_bars() x-position for a sample not at time-origin
//
// Two samples on the same thread: rel_start of second = 8 ms.
// bar_x of second = bounds.x + 8 * ppm.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, ComputeBarsXPositionOffset)
{
    using cd::profile::cpu_marker_overlay::BarRect;

    const double base_ms   = 500.0;
    const double window_ms = 16.0;
    const Rect   bounds    { 100.0F, 50.0F, 800.0F, 60.0F };

    const std::vector<MarkerSample> samples = {
        { "render",  base_ms + 0.0, 4.0, 1U },
        { "physics", base_ms + 8.0, 2.0, 1U },
    };

    Overlay ov { window_ms };
    const auto bars = ov.compute_bars(samples, bounds);
    ASSERT_EQ(bars.size(), 2U);

    const float ppm        = bounds.width / static_cast<float>(window_ms);
    const float expected_x = bounds.x + 8.0F * ppm;
    EXPECT_FLOAT_EQ(bars[1].x, expected_x);
}

// ---------------------------------------------------------------------------
// Test 25: draw() and compute_bars() produce consistent bar geometry.
//
// For N samples, compute_bars() and draw() must agree on the number of
// bars produced and on the x-coordinates of the first vertex of each quad.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Overlay, DrawAndComputeBarsConsistent)
{
    const double base_ms   = 0.0;
    const double window_ms = 20.0;
    const Rect   bounds    { 5.0F, 10.0F, 400.0F, 80.0F };

    const std::vector<MarkerSample> samples = {
        { "a", base_ms + 0.0,  3.0, 1U },
        { "b", base_ms + 5.0,  2.0, 1U },
        { "c", base_ms + 12.0, 1.0, 2U },
    };

    Overlay ov { window_ms };

    // compute_bars gives host-side geometry.
    const auto bars = ov.compute_bars(samples, bounds);

    // draw() must emit exactly bars.size() quads into the batcher.
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    ov.draw(batcher, samples, bounds);

    ASSERT_EQ(bars.size(), 3U);
    EXPECT_EQ(batcher.vertex_count(), bars.size() * 4U);

    // The top-left vertex of each quad must match compute_bars() x/y.
    const auto verts = batcher.vertices();
    for (std::size_t i = 0U; i < bars.size(); ++i)
    {
        EXPECT_FLOAT_EQ(verts[i * 4U].pos_x, bars[i].x)
            << "quad " << i << " x mismatch";
        EXPECT_FLOAT_EQ(verts[i * 4U].pos_y, bars[i].y)
            << "quad " << i << " y mismatch";
    }
}

// ---------------------------------------------------------------------------
// Test 26: deeply nested scopes — 5 levels; durations are monotone
//          (each enclosing scope >= its child).
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Scope, DeeplyNestedFiveLevels)
{
    Collector col;

    {
        Scope l1(col, "L1");
        {
            Scope l2(col, "L2");
            {
                Scope l3(col, "L3");
                {
                    Scope l4(col, "L4");
                    {
                        Scope l5(col, "L5");
                    }
                }
            }
        }
    }

    EXPECT_EQ(col.sample_count(), 5U);

    const auto all = col.samples_since(0.0);
    ASSERT_EQ(all.size(), 5U);

    // Innermost "L5" committed first → index 0; "L1" last → index 4.
    EXPECT_EQ(all[0].name, "L5");
    EXPECT_EQ(all[4].name, "L1");

    // Each outer scope duration >= inner: L1 >= L2 >= ... >= L5.
    EXPECT_GE(all[4].duration_ms, all[3].duration_ms);
    EXPECT_GE(all[3].duration_ms, all[2].duration_ms);
    EXPECT_GE(all[2].duration_ms, all[1].duration_ms);
    EXPECT_GE(all[1].duration_ms, all[0].duration_ms);
}

// ---------------------------------------------------------------------------
// Test 27: reset between frames — clear() then new samples start fresh;
//          samples_since returns only the new ones.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, ResetBetweenFrames)
{
    Collector col;

    // Frame 1.
    for (int i = 0; i < 3; ++i)
    {
        const MarkerHandle h = col.begin("frame1");
        col.end(h);
    }
    EXPECT_EQ(col.sample_count(), 3U);

    // End of frame — reset.
    col.clear();
    EXPECT_EQ(col.sample_count(), 0U);

    // Frame 2.
    const MarkerHandle h = col.begin("frame2");
    col.end(h);
    EXPECT_EQ(col.sample_count(), 1U);

    const auto all = col.samples_since(0.0);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].name, "frame2");
}

// ---------------------------------------------------------------------------
// Test 28: samples_since() with cutoff beyond all sample times → empty.
// ---------------------------------------------------------------------------
TEST(CpuMarkerOverlay_Collector, SamplesSinceFutureCutoffEmpty)
{
    Collector col;
    const MarkerHandle h = col.begin("past");
    col.end(h);

    // A cutoff far in the future.
    const double far_future = 1.0e15;
    EXPECT_TRUE(col.samples_since(far_future).empty());
}
