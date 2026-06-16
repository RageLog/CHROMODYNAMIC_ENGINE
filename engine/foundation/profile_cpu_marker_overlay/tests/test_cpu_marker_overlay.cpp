// =============================================================================
// CHROMODYNAMIC — cd::profile::cpu_marker_overlay tests
// =============================================================================
#include <cd/profile/cpu_marker_overlay/CpuMarkerOverlay.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <gtest/gtest.h>

#include <chrono>
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
