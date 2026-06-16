// =============================================================================
// CHROMODYNAMIC — cd::profile::frame_graph_timeline tests
//
// Phase 593 — 7 tests covering Timeline + TimelineOverlay.
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
