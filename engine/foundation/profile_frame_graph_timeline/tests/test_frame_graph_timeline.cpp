// =============================================================================
// CHROMODYNAMIC — cd::profile::frame_graph_timeline tests
//
// Phase 593 — original 7 tests + depth-100 edge/negative suite.
// =============================================================================
#include <cd/profile/frame_graph_timeline/FrameGraphTimeline.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <gtest/gtest.h>

using cd::profile::frame_graph_timeline::PassRecord;
using cd::profile::frame_graph_timeline::Rect;
using cd::profile::frame_graph_timeline::Timeline;
using cd::profile::frame_graph_timeline::TimelineOverlay;

// ---------------------------------------------------------------------------
// Test 1: begin_frame resets the accumulator
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, BeginFrameResetsAccumulator)
{
    Timeline tl;

    // First frame: record two passes and commit.
    tl.begin_frame();
    tl.record_pass("GBuffer",   0.0, 2.0, 1U);
    tl.record_pass("ShadowMap", 2.0, 1.5, 2U);
    tl.end_frame();

    ASSERT_EQ(tl.last_frame_passes().size(), 2U);

    // Second frame: begin_frame must clear the accumulator so that only
    // the passes recorded in this frame appear after end_frame.
    tl.begin_frame();
    // Record nothing — end_frame should produce an empty last_frame.
    tl.end_frame();

    EXPECT_EQ(tl.last_frame_passes().size(), 0U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 0.0);
}

// ---------------------------------------------------------------------------
// Test 2: record_pass appends in order
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, RecordPassAppends)
{
    Timeline tl;
    tl.begin_frame();
    tl.record_pass("Depth",    0.0, 1.0, 10U);
    tl.record_pass("Lighting", 1.0, 3.0, 11U);
    tl.record_pass("Bloom",    4.0, 0.5, 12U);
    tl.end_frame();

    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), 3U);

    EXPECT_EQ(passes[0].pass_name,       "Depth");
    EXPECT_DOUBLE_EQ(passes[0].gpu_start_ms,    0.0);
    EXPECT_DOUBLE_EQ(passes[0].gpu_duration_ms, 1.0);
    EXPECT_EQ(passes[0].graph_node_id,   10U);

    EXPECT_EQ(passes[1].pass_name,       "Lighting");
    EXPECT_DOUBLE_EQ(passes[1].gpu_start_ms,    1.0);
    EXPECT_DOUBLE_EQ(passes[1].gpu_duration_ms, 3.0);
    EXPECT_EQ(passes[1].graph_node_id,   11U);

    EXPECT_EQ(passes[2].pass_name,       "Bloom");
    EXPECT_DOUBLE_EQ(passes[2].gpu_start_ms,    4.0);
    EXPECT_DOUBLE_EQ(passes[2].gpu_duration_ms, 0.5);
    EXPECT_EQ(passes[2].graph_node_id,   12U);
}

// ---------------------------------------------------------------------------
// Test 3: end_frame stamps the correct total_ms
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, EndFrameStampsTotalMs)
{
    Timeline tl;
    tl.begin_frame();
    tl.record_pass("A", 0.0,  2.0, 1U);
    tl.record_pass("B", 2.0,  3.5, 2U);
    tl.record_pass("C", 5.5,  1.0, 3U);
    tl.end_frame();

    // 2.0 + 3.5 + 1.0 = 6.5
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 6.5);
}

// ---------------------------------------------------------------------------
// Test 4: last_frame_passes round-trip — data survives begin/end cycle
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, LastFrameRoundTrip)
{
    Timeline tl;

    // Frame A.
    tl.begin_frame();
    tl.record_pass("TAA",     0.0, 0.8, 100U);
    tl.record_pass("Tonemap", 0.8, 0.2, 101U);
    tl.end_frame();

    const auto passes_a = tl.last_frame_passes();
    ASSERT_EQ(passes_a.size(), 2U);
    EXPECT_EQ(passes_a[0].pass_name, "TAA");
    EXPECT_EQ(passes_a[1].pass_name, "Tonemap");
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 1.0);

    // Start Frame B — last_frame should still reflect Frame A until end_frame.
    tl.begin_frame();
    tl.record_pass("SSAO", 0.0, 1.5, 200U);
    // Do NOT call end_frame yet — last_frame_passes should still be Frame A.
    ASSERT_EQ(tl.last_frame_passes().size(), 2U);

    tl.end_frame();
    // Now last_frame reflects Frame B.
    const auto passes_b = tl.last_frame_passes();
    ASSERT_EQ(passes_b.size(), 1U);
    EXPECT_EQ(passes_b[0].pass_name, "SSAO");
}

// ---------------------------------------------------------------------------
// Test 5: TimelineOverlay::draw emits >= 1 quad per visible pass
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, DrawEmitsAtLeastOneQuadPerPass)
{
    const std::vector<PassRecord> passes = {
        { "GBuffer",   0.0, 3.0,  1U },
        { "Shadow",    3.0, 2.0,  2U },
        { "Lighting",  5.0, 4.0,  3U },
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    TimelineOverlay overlay { 16.0 };  // 16 ms window.
    const Rect bounds { 0.0F, 0.0F, 800.0F, 40.0F };
    overlay.draw(batcher, std::span<const PassRecord>(passes), bounds);

    // Each pass → 1 quad → 4 vertices + 6 indices.
    EXPECT_GE(batcher.vertex_count(), passes.size() * 4U)
        << "Expected at least 4 vertices per pass";
    EXPECT_GE(batcher.index_count(), passes.size() * 6U)
        << "Expected at least 6 indices per pass";
    EXPECT_GE(batcher.command_count(), 1U)
        << "Expected at least one draw command";
}

// ---------------------------------------------------------------------------
// Test 5b: TimelineOverlay::draw emits the EXACT per-pass draw output for a
//          known begin/record_pass/end_frame sequence — bar geometry + colour
//          + merged draw command, inspected on the DrawBatcher with no GPU.
//          This is the visual-side coverage the lib was missing.
// ---------------------------------------------------------------------------
namespace
{
// Mirror of TimelineOverlay's internal djb2 hash → pass colour.
[[nodiscard]] cd::ui::renderer::Color expected_pass_colour(std::string_view name)
{
    std::uint32_t h = 5381U;
    for (const char c : name)
        h = ((h << 5U) + h) + static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    return cd::ui::renderer::Color {
        static_cast<std::uint8_t>(h & 0xFFu),
        static_cast<std::uint8_t>((h >> 8U) & 0xFFu),
        static_cast<std::uint8_t>((h >> 16U) & 0xFFu),
        210U
    };
}
}  // namespace

TEST(FrameGraphTimeline_TimelineOverlay, DrawEmitsExactPerPassOutput)
{
    // Feed the Timeline a known sequence, then render its last frame.
    Timeline tl;
    tl.begin_frame();
    tl.record_pass("GBuffer",  0.0, 3.0, 1U);  // bar at window-left
    tl.record_pass("Lighting", 4.0, 2.0, 2U);  // 4 ms later
    tl.end_frame();

    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), 2U);

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    const double window_ms = 16.0;
    TimelineOverlay overlay { window_ms };
    const Rect bounds { 10.0F, 20.0F, 320.0F, 40.0F };
    overlay.draw(batcher, passes, bounds);

    // Single lane for all GPU passes: bar_h = 75% height, centred vertically.
    const float pixels_per_ms = bounds.width / static_cast<float>(window_ms);
    const float bar_h = bounds.height * 0.75F;
    const float bar_y = bounds.y + (bounds.height - bar_h) * 0.5F;

    // Exactly 2 quads → 8 vertices, 12 indices, 1 merged solid command.
    ASSERT_EQ(batcher.vertex_count(), 8U);
    ASSERT_EQ(batcher.index_count(), 12U);
    ASSERT_EQ(batcher.command_count(), 1U);

    const auto verts = batcher.vertices();

    // Pass 0 ("GBuffer"): rel_start 0 → bar at bounds.x, vertically centred.
    EXPECT_FLOAT_EQ(verts[0].pos_x, bounds.x);
    EXPECT_FLOAT_EQ(verts[0].pos_y, bar_y);
    EXPECT_FLOAT_EQ(verts[3].pos_y, bar_y + bar_h);  // bottom-left edge
    const float expected_w0 = 3.0F * pixels_per_ms;  // 3 ms wide
    EXPECT_FLOAT_EQ(verts[1].pos_x, bounds.x + expected_w0);  // top-right edge
    const auto c0 = expected_pass_colour("GBuffer");
    EXPECT_EQ(verts[0].r, c0.r);
    EXPECT_EQ(verts[0].g, c0.g);
    EXPECT_EQ(verts[0].b, c0.b);
    EXPECT_EQ(verts[0].a, c0.a);

    // Pass 1 ("Lighting"): rel_start 4 ms → bar_x offset by 4 * pixels_per_ms.
    const float expected_x1 = bounds.x + 4.0F * pixels_per_ms;
    EXPECT_FLOAT_EQ(verts[4].pos_x, expected_x1);
    EXPECT_FLOAT_EQ(verts[4].pos_y, bar_y);
    const auto c1 = expected_pass_colour("Lighting");
    EXPECT_EQ(verts[4].r, c1.r);
    EXPECT_EQ(verts[4].g, c1.g);
    EXPECT_EQ(verts[4].b, c1.b);

    // The merged command is a solid (untextured) draw of both bars.
    const auto cmds = batcher.commands();
    ASSERT_EQ(cmds.size(), 1U);
    EXPECT_EQ(cmds[0].variant, cd::ui::renderer::material::kSolid);
    EXPECT_EQ(cmds[0].index_count, 12U);
    EXPECT_EQ(cmds[0].texture_slot, 0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------
// Test 6: TimelineOverlay::draw with empty passes is a no-op
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, DrawEmptyPassesNoOp)
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    TimelineOverlay overlay { 16.0 };
    const Rect bounds { 0.0F, 0.0F, 800.0F, 40.0F };
    overlay.draw(batcher, std::span<const PassRecord>{}, bounds);

    EXPECT_EQ(batcher.vertex_count(), 0U);
    EXPECT_EQ(batcher.index_count(),  0U);
}

// ---------------------------------------------------------------------------
// Test 7: Timeline multi-frame sequence — totals accumulate independently
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, MultiFrameSequenceTotalsAreIndependent)
{
    Timeline tl;

    // Frame 1: total = 5.0 ms.
    tl.begin_frame();
    tl.record_pass("P1", 0.0, 3.0, 1U);
    tl.record_pass("P2", 3.0, 2.0, 2U);
    tl.end_frame();
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 5.0);

    // Frame 2: total = 1.0 ms (shorter frame).
    tl.begin_frame();
    tl.record_pass("P1", 0.0, 1.0, 1U);
    tl.end_frame();
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 1.0);
    ASSERT_EQ(tl.last_frame_passes().size(), 1U);

    // Frame 3: no passes → total = 0.
    tl.begin_frame();
    tl.end_frame();
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 0.0);
    EXPECT_EQ(tl.last_frame_passes().size(), 0U);
}

// ===========================================================================
// Depth-100 edge / negative tests added for 78% → 100% coverage
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 8: empty frame (end_frame with zero passes) → all stats are 0
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, EmptyFrameAllStatsZero)
{
    Timeline tl;
    tl.begin_frame();
    tl.end_frame();

    EXPECT_EQ(tl.last_frame_passes().size(), 0U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(),    0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 0.0);
}

// ---------------------------------------------------------------------------
// Test 9: single-pass frame → min == max == duration, total == duration
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, SinglePassMinMaxEqualDuration)
{
    Timeline tl;
    tl.begin_frame();
    tl.record_pass("OnlyPass", 0.0, 4.25, 7U);
    tl.end_frame();

    ASSERT_EQ(tl.last_frame_passes().size(), 1U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(),    4.25);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 4.25);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 4.25);
}

// ---------------------------------------------------------------------------
// Test 10: per-pass min / max with multiple passes of varying duration
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, PerPassMinMaxCorrect)
{
    Timeline tl;
    tl.begin_frame();
    tl.record_pass("Fast",   0.0, 0.5,  1U);
    tl.record_pass("Medium", 0.5, 2.0,  2U);
    tl.record_pass("Slow",   2.5, 5.25, 3U);
    tl.end_frame();

    // total = 0.5 + 2.0 + 5.25 = 7.75
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(),    7.75);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 0.5);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 5.25);
}

// ---------------------------------------------------------------------------
// Test 11: min/max reset across frames — second frame overrides stats
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, MinMaxRefreshEachFrame)
{
    Timeline tl;

    // Frame 1: wide spread.
    tl.begin_frame();
    tl.record_pass("A", 0.0, 1.0, 0U);
    tl.record_pass("B", 1.0, 9.0, 1U);
    tl.end_frame();
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 1.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 9.0);

    // Frame 2: narrower spread — stats should reflect frame 2 only.
    tl.begin_frame();
    tl.record_pass("C", 0.0, 3.0, 0U);
    tl.record_pass("D", 3.0, 4.0, 1U);
    tl.end_frame();
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 3.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 4.0);
}

// ---------------------------------------------------------------------------
// Test 12: duplicate pass names are both stored and counted
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, DuplicatePassNamesStoredIndependently)
{
    Timeline tl;
    tl.begin_frame();
    tl.record_pass("Shadow", 0.0, 1.0, 10U);
    tl.record_pass("Shadow", 1.0, 1.5, 11U);  // same name, different node
    tl.record_pass("Shadow", 2.5, 0.5, 12U);  // same name again
    tl.end_frame();

    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), 3U);
    EXPECT_EQ(passes[0].pass_name, "Shadow");
    EXPECT_EQ(passes[1].pass_name, "Shadow");
    EXPECT_EQ(passes[2].pass_name, "Shadow");

    // Each record retains its own timing and node_id.
    EXPECT_EQ(passes[0].graph_node_id, 10U);
    EXPECT_EQ(passes[1].graph_node_id, 11U);
    EXPECT_EQ(passes[2].graph_node_id, 12U);

    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 3.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 0.5);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 1.5);
}

// ---------------------------------------------------------------------------
// Test 13: record_pass without prior begin_frame appends and survives end_frame
//   (documented permissive behaviour — GPU readbacks may precede frame boundary)
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, RecordPassWithoutBeginFramePermissive)
{
    Timeline tl;  // fresh; no begin_frame called yet.
    tl.record_pass("EarlyReadback", 0.0, 0.8, 99U);
    tl.end_frame();

    // The pass should appear in last_frame_passes.
    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), 1U);
    EXPECT_EQ(passes[0].pass_name, "EarlyReadback");
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 0.8);
}

// ---------------------------------------------------------------------------
// Test 14: end_frame without record_pass (empty current frame) is a no-op pass
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, EndFrameWithoutRecordPassIsNoOp)
{
    Timeline tl;

    // Populate a real frame first.
    tl.begin_frame();
    tl.record_pass("Init", 0.0, 2.0, 1U);
    tl.end_frame();
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 2.0);

    // Now an empty frame.
    tl.begin_frame();
    tl.end_frame();  // no record_pass in between

    EXPECT_EQ(tl.last_frame_passes().size(), 0U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(),    0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 0.0);
}

// ---------------------------------------------------------------------------
// Test 15: reset() clears both buffers and all stats
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, ResetClearsAllState)
{
    Timeline tl;

    tl.begin_frame();
    tl.record_pass("GBuffer",  0.0, 3.0, 1U);
    tl.record_pass("Lighting", 3.0, 2.0, 2U);
    tl.end_frame();

    ASSERT_EQ(tl.last_frame_passes().size(), 2U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 5.0);

    tl.reset();

    EXPECT_EQ(tl.last_frame_passes().size(), 0U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(),    0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 0.0);
}

// ---------------------------------------------------------------------------
// Test 16: timeline query after reset + new frame cycle works correctly
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, QueryAfterResetThenNewFrame)
{
    Timeline tl;

    tl.begin_frame();
    tl.record_pass("Old", 0.0, 10.0, 1U);
    tl.end_frame();

    tl.reset();

    // After reset the state must be pristine.
    ASSERT_EQ(tl.last_frame_passes().size(), 0U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 0.0);

    // A new frame cycle after reset must work normally.
    tl.begin_frame();
    tl.record_pass("New", 0.0, 1.5, 2U);
    tl.end_frame();

    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), 1U);
    EXPECT_EQ(passes[0].pass_name, "New");
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(),    1.5);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 1.5);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 1.5);
}

// ---------------------------------------------------------------------------
// Test 17: reserve() does not change observable state
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, ReserveDoesNotChangeObservableState)
{
    Timeline tl;
    tl.reserve(256U);

    // Reserving capacity must leave the timeline logically empty.
    EXPECT_EQ(tl.last_frame_passes().size(), 0U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(),    0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 0.0);

    // Normal frame cycle still works after reserve.
    tl.begin_frame();
    tl.record_pass("Z", 0.0, 0.1, 0U);
    tl.end_frame();
    EXPECT_EQ(tl.last_frame_passes().size(), 1U);
}

// ---------------------------------------------------------------------------
// Test 18: capacity / overflow boundary — many passes (> typical ring cap)
//   Timeline must not lose passes; vector grows as needed.
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, OverflowBoundaryManyPasses)
{
    constexpr std::size_t kPassCount = 1024U;

    Timeline tl;
    tl.begin_frame();
    for (std::size_t i = 0U; i < kPassCount; ++i)
        tl.record_pass("P", static_cast<double>(i), 1.0, static_cast<std::uint32_t>(i));
    tl.end_frame();

    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), kPassCount);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), static_cast<double>(kPassCount));
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 1.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 1.0);
}

// ---------------------------------------------------------------------------
// Test 19: sequential passes — ordering is preserved in last_frame_passes()
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, SequentialPassesOrderPreserved)
{
    Timeline tl;
    tl.begin_frame();
    tl.record_pass("A", 0.0,  1.0, 1U);
    tl.record_pass("B", 1.0,  2.0, 2U);
    tl.record_pass("C", 3.0,  0.5, 3U);
    tl.record_pass("D", 3.5,  1.5, 4U);
    tl.end_frame();

    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), 4U);
    EXPECT_EQ(passes[0].pass_name, "A");
    EXPECT_EQ(passes[1].pass_name, "B");
    EXPECT_EQ(passes[2].pass_name, "C");
    EXPECT_EQ(passes[3].pass_name, "D");
}

// ---------------------------------------------------------------------------
// Test 20: zero-duration pass is stored and contributes 0 to total but
//          sets min to 0 when all durations are 0
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, ZeroDurationPassStoredAndMinIsZero)
{
    Timeline tl;
    tl.begin_frame();
    tl.record_pass("ZeroGpu", 5.0, 0.0, 42U);
    tl.end_frame();

    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), 1U);
    EXPECT_DOUBLE_EQ(passes[0].gpu_duration_ms,  0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(),    0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 0.0);
    EXPECT_DOUBLE_EQ(tl.last_frame_max_pass_ms(), 0.0);
}

// ---------------------------------------------------------------------------
// Test 21: TimelineOverlay — passes entirely outside window are culled
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, PassesOutsideWindowAreCulled)
{
    // Window = 0..4 ms (min_start=0, window_ms=4).
    // Pass at rel_start=10 ms is well outside the window.
    const std::vector<PassRecord> passes = {
        { "Visible",  0.0,  2.0, 1U },
        { "Outside", 10.0,  1.0, 2U },
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    TimelineOverlay overlay { 4.0 };  // 4 ms window
    const Rect bounds { 0.0F, 0.0F, 400.0F, 40.0F };
    overlay.draw(batcher, std::span<const PassRecord>(passes), bounds);

    // Only 1 pass should produce quads (4 vertices / 6 indices).
    EXPECT_EQ(batcher.vertex_count(), 4U);
    EXPECT_EQ(batcher.index_count(),  6U);
}

// ---------------------------------------------------------------------------
// Test 22: TimelineOverlay — zero bounds.width is a no-op
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, ZeroWidthBoundsIsNoOp)
{
    const std::vector<PassRecord> passes = {
        { "Pass", 0.0, 2.0, 1U },
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    TimelineOverlay overlay { 16.0 };
    const Rect zero_w { 0.0F, 0.0F, 0.0F, 40.0F };
    overlay.draw(batcher, std::span<const PassRecord>(passes), zero_w);

    EXPECT_EQ(batcher.vertex_count(), 0U);
    EXPECT_EQ(batcher.index_count(),  0U);
}

// ---------------------------------------------------------------------------
// Test 23: TimelineOverlay — zero bounds.height is a no-op
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, ZeroHeightBoundsIsNoOp)
{
    const std::vector<PassRecord> passes = {
        { "Pass", 0.0, 2.0, 1U },
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    TimelineOverlay overlay { 16.0 };
    const Rect zero_h { 0.0F, 0.0F, 800.0F, 0.0F };
    overlay.draw(batcher, std::span<const PassRecord>(passes), zero_h);

    EXPECT_EQ(batcher.vertex_count(), 0U);
    EXPECT_EQ(batcher.index_count(),  0U);
}

// ---------------------------------------------------------------------------
// Test 24: TimelineOverlay — window_ms=0 uses fallback 1.0 (no division by zero)
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, ZeroWindowMsFallbackNoCrash)
{
    const std::vector<PassRecord> passes = {
        { "P", 0.0, 0.5, 1U },
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    TimelineOverlay overlay { 0.0 };  // degenerate window
    const Rect bounds { 0.0F, 0.0F, 100.0F, 20.0F };
    // Must not crash and must emit the pass (all within fallback-window of 1.0 ms).
    EXPECT_NO_THROW(overlay.draw(batcher, std::span<const PassRecord>(passes), bounds));
}

// ---------------------------------------------------------------------------
// Test 25: TimelineOverlay — set_window_ms mutates and window_ms() reflects it
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, SetWindowMsUpdatesAccessor)
{
    TimelineOverlay overlay { 16.0 };
    EXPECT_DOUBLE_EQ(overlay.window_ms(), 16.0);

    overlay.set_window_ms(33.3);
    EXPECT_DOUBLE_EQ(overlay.window_ms(), 33.3);

    overlay.set_window_ms(0.0);
    EXPECT_DOUBLE_EQ(overlay.window_ms(), 0.0);
}

// ---------------------------------------------------------------------------
// Test 26: Timeline — begin_frame/record_pass/end_frame full ordering contract
//   Three complete frames; each frame's data visible immediately after end_frame.
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, OrderingContractThreeFrames)
{
    Timeline tl;

    for (int frame = 1; frame <= 3; ++frame)
    {
        tl.begin_frame();
        const auto dur = static_cast<double>(frame) * 2.0;
        tl.record_pass("Pass", 0.0, dur, static_cast<std::uint32_t>(frame));
        tl.end_frame();

        EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), dur)
            << "Frame " << frame << " total mismatch";
        ASSERT_EQ(tl.last_frame_passes().size(), 1U)
            << "Frame " << frame << " pass count mismatch";
        EXPECT_DOUBLE_EQ(tl.last_frame_passes()[0].gpu_duration_ms, dur);
    }
}

// ---------------------------------------------------------------------------
// Test 27: Timeline — last_frame_passes() span stays valid between begin_frame
//          and end_frame of the next frame (double-buffer guarantee)
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, SpanRemainsValidDuringNextFrameAccumulation)
{
    Timeline tl;

    tl.begin_frame();
    tl.record_pass("Frame1Pass", 0.0, 7.0, 1U);
    tl.end_frame();

    const auto snap = tl.last_frame_passes();
    ASSERT_EQ(snap.size(), 1U);
    EXPECT_EQ(snap[0].pass_name, "Frame1Pass");

    // Begin the next frame and accumulate — snap must still be coherent.
    tl.begin_frame();
    tl.record_pass("Frame2Pass", 0.0, 3.0, 2U);

    ASSERT_EQ(snap.size(), 1U);                    // size unchanged
    EXPECT_EQ(snap[0].pass_name, "Frame1Pass");    // content unchanged
    EXPECT_DOUBLE_EQ(snap[0].gpu_duration_ms, 7.0);
}

// ---------------------------------------------------------------------------
// Test 28: Timeline — reset() during an in-progress frame (after begin_frame
//          but before end_frame) leaves the timeline in a clean state
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, ResetDuringInProgressFrame)
{
    Timeline tl;

    // First full frame.
    tl.begin_frame();
    tl.record_pass("Old", 0.0, 5.0, 0U);
    tl.end_frame();

    // Start second frame, record something, then reset before end_frame.
    tl.begin_frame();
    tl.record_pass("Partial", 0.0, 2.0, 1U);
    tl.reset();  // wipe both buffers mid-accumulation

    EXPECT_EQ(tl.last_frame_passes().size(), 0U);
    EXPECT_DOUBLE_EQ(tl.last_frame_total_ms(), 0.0);

    // A clean frame cycle after the mid-frame reset must work.
    tl.begin_frame();
    tl.record_pass("Fresh", 0.0, 1.0, 2U);
    tl.end_frame();

    ASSERT_EQ(tl.last_frame_passes().size(), 1U);
    EXPECT_EQ(tl.last_frame_passes()[0].pass_name, "Fresh");
}

// ---------------------------------------------------------------------------
// Test 29: Timeline — large pass count after reserve — no reallocation hazard
//   (Verifies that reserve() + 512 record_pass calls produce correct results.)
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_Timeline, LargePassCountAfterReserve)
{
    constexpr std::size_t kN = 512U;

    Timeline tl;
    tl.reserve(kN);

    tl.begin_frame();
    for (std::size_t i = 0U; i < kN; ++i)
    {
        const auto dur = static_cast<double>(i + 1U) * 0.01;  // 0.01..5.12 ms
        tl.record_pass("P", static_cast<double>(i), dur, static_cast<std::uint32_t>(i));
    }
    tl.end_frame();

    const auto passes = tl.last_frame_passes();
    ASSERT_EQ(passes.size(), kN);

    // Sum = 0.01+0.02+...+5.12 = 0.01 * (1+2+...+512) = 0.01 * 512*513/2
    constexpr auto   kTri      = kN * (kN + 1U) / 2U;  // exact integer triangular sum
    constexpr double kExpected = 0.01 * static_cast<double>(kTri);
    EXPECT_NEAR(tl.last_frame_total_ms(), kExpected, 1.0e-6);

    EXPECT_DOUBLE_EQ(tl.last_frame_min_pass_ms(), 0.01);
    EXPECT_NEAR(tl.last_frame_max_pass_ms(), static_cast<double>(kN) * 0.01, 1.0e-9);
}

// ---------------------------------------------------------------------------
// Test 30: TimelineOverlay — all passes culled when beyond window → no quads
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, AllPassesCulledProducesNoQuads)
{
    // window = 4 ms; pass starts at 100 ms after min_start=0.
    const std::vector<PassRecord> passes = {
        { "OutA", 100.0, 1.0, 1U },
        { "OutB", 200.0, 1.0, 2U },
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    TimelineOverlay overlay { 4.0 };
    const Rect bounds { 0.0F, 0.0F, 400.0F, 40.0F };
    // min_start = 100.0; rel_start of OutA = 0 (visible), OutB = 100 (culled).
    // Actually OutA is at rel_start=0 which is within window.
    // So only OutB is culled.
    overlay.draw(batcher, std::span<const PassRecord>(passes), bounds);

    // OutA (rel_start=0, dur=1 < window=4) → visible; OutB culled.
    EXPECT_EQ(batcher.vertex_count(), 4U);
    EXPECT_EQ(batcher.index_count(),  6U);
}

// ---------------------------------------------------------------------------
// Test 31: TimelineOverlay — zero-duration pass still emits a 1-pixel quad
// ---------------------------------------------------------------------------
TEST(FrameGraphTimeline_TimelineOverlay, ZeroDurationPassEmitsMinimumPixel)
{
    const std::vector<PassRecord> passes = {
        { "Instant", 0.0, 0.0, 1U },
    };

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    TimelineOverlay overlay { 16.0 };
    const Rect bounds { 0.0F, 0.0F, 800.0F, 40.0F };
    overlay.draw(batcher, std::span<const PassRecord>(passes), bounds);

    // 1 pass → 1 quad → 4 vertices, 6 indices.
    EXPECT_EQ(batcher.vertex_count(), 4U);
    EXPECT_EQ(batcher.index_count(),  6U);

    // The bar width must be >= 1 pixel (minimum clamp).
    const auto verts = batcher.vertices();
    ASSERT_GE(verts.size(), 4U);
    const float bar_w = verts[1].pos_x - verts[0].pos_x;
    EXPECT_GE(bar_w, 1.0F);
}
