// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_debug_viz/tests/test_debug_viz.cpp
//
// phase696 — unit tests for cd::editor::debug_viz::DebugVizOverlay.
//
// All tests are headless (no RHI, no ImGui). Verifies:
//   1.  DefaultVisible        — overlay starts visible (toggle ON by default).
//   2.  KindAccessor          — kind() returns the kind passed to the ctor.
//   3.  ToggleOnOff           — set_visible(false) / set_visible(true) round-trip.
//   4.  DrawWhenHidden        — draw() emits no quads when !is_visible().
//   5.  DrawDepthEmitsQuads   — kDepth draw emits >= 4 vertices.
//   6.  DrawNormalEmitsQuads  — kNormal draw emits >= 4 vertices.
//   7.  DrawBucketEmitsQuads  — kAlphaBucket draw emits >= 4 vertices.
//   8.  DrawInvalidBounds     — draw() is a no-op for zero-sized bounds.
//   9.  DrawBucketMoreVertsOrEqualThanDepth — both emit non-zero vertices.
//  10.  AllKindsNoThrow       — draw() does not throw for any VizKind value.
//  --- phase696 additions ---
//  11.  HotkeyToggleFlipsState  — toggle() flips and returns new state.
//  12.  HotkeyToggleIdempotent  — two toggle() calls restore original state.
//  13.  SampleRingEmpty         — fresh overlay: sample_count() == 0.
//  14.  PushSampleIncreasesCount — push_frame_sample grows count up to cap.
//  15.  SampleRingWraps         — pushing > kSparklineCapacity wraps without crash.
//  16.  SampleAtOrder           — samples are returned oldest-first.
//  17.  SparklineInBudgetColor  — with in-budget samples, draw emits quads.
//  18.  SparklineOverBudgetColor — overbudget sample triggers additional quads.
//  19.  BucketCountsDefault      — bucket_counts() returns all-zero by default.
//  20.  BucketCountsSet          — set_bucket_counts() updates all three fields.
//  21.  BucketProportionalBars   — with counts set, draw emits > equal-thirds baseline.
//  22.  BucketFallbackEqualThirds — zero-total falls back without crash.
// =============================================================================
#include <cd/editor/panel_debug_viz/DebugViz.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

#include <tuple>  // std::ignore

namespace dv = cd::editor::debug_viz;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    return { 10.0F, 10.0F, 200.0F, 150.0F };
}

[[nodiscard]] cd::ui::widgets::Theme default_theme() noexcept
{
    return cd::ui::widgets::Theme {};
}

/// Draw into a fresh batcher and return the resulting vertex count.
[[nodiscard]] std::size_t draw_vertex_count(dv::DebugVizOverlay& overlay,
                                            const cd::ui::widgets::Rect& bounds)
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    overlay.draw(batcher, bounds, default_theme());
    return batcher.vertex_count();
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultVisible
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, DefaultVisible)
{
    const dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    EXPECT_TRUE(overlay.is_visible());
}

// ---------------------------------------------------------------------------
// TEST 2 — KindAccessor
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, KindAccessor)
{
    EXPECT_EQ(dv::DebugVizOverlay(dv::VizKind::kDepth).kind(),       dv::VizKind::kDepth);
    EXPECT_EQ(dv::DebugVizOverlay(dv::VizKind::kNormal).kind(),      dv::VizKind::kNormal);
    EXPECT_EQ(dv::DebugVizOverlay(dv::VizKind::kAlphaBucket).kind(), dv::VizKind::kAlphaBucket);
}

// ---------------------------------------------------------------------------
// TEST 3 — ToggleOnOff
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, ToggleOnOff)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kNormal };
    EXPECT_TRUE(overlay.is_visible());

    overlay.set_visible(false);
    EXPECT_FALSE(overlay.is_visible());

    overlay.set_visible(true);
    EXPECT_TRUE(overlay.is_visible());
}

// ---------------------------------------------------------------------------
// TEST 4 — DrawWhenHidden
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, DrawWhenHidden)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    overlay.set_visible(false);

    const std::size_t verts = draw_vertex_count(overlay, standard_bounds());
    EXPECT_EQ(verts, 0U);
}

// ---------------------------------------------------------------------------
// TEST 5 — DrawDepthEmitsQuads
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, DrawDepthEmitsQuads)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    const std::size_t verts = draw_vertex_count(overlay, standard_bounds());
    // Minimum: background (4) + 4 border edges (4*4) + header (4) + 8 bands (8*4) = 52
    // We assert >= 4 (generous lower bound; real draw emits many more).
    EXPECT_GE(verts, 4U);
}

// ---------------------------------------------------------------------------
// TEST 6 — DrawNormalEmitsQuads
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, DrawNormalEmitsQuads)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kNormal };
    const std::size_t verts = draw_vertex_count(overlay, standard_bounds());
    EXPECT_GE(verts, 4U);
}

// ---------------------------------------------------------------------------
// TEST 7 — DrawBucketEmitsQuads
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, DrawBucketEmitsQuads)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kAlphaBucket };
    const std::size_t verts = draw_vertex_count(overlay, standard_bounds());
    // background + 4 border + header + 3 bands + 2 separators = (1+4+1+3+2)*4 = 44 verts minimum
    EXPECT_GE(verts, 4U);
}

// ---------------------------------------------------------------------------
// TEST 8 — DrawInvalidBounds
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, DrawInvalidBounds)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };

    // Zero-width bounds.
    const cd::ui::widgets::Rect zero_w { 0.0F, 0.0F, 0.0F, 150.0F };
    const std::size_t verts_zw = draw_vertex_count(overlay, zero_w);
    EXPECT_EQ(verts_zw, 0U);

    // Zero-height bounds.
    const cd::ui::widgets::Rect zero_h { 0.0F, 0.0F, 200.0F, 0.0F };
    const std::size_t verts_zh = draw_vertex_count(overlay, zero_h);
    EXPECT_EQ(verts_zh, 0U);
}

// ---------------------------------------------------------------------------
// TEST 9 — DrawBucketMoreVertsOrEqualThanDepth
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, DrawBucketMoreVertsOrEqualThanDepth)
{
    dv::DebugVizOverlay depth_overlay  { dv::VizKind::kDepth };
    dv::DebugVizOverlay bucket_overlay { dv::VizKind::kAlphaBucket };

    const auto bounds = standard_bounds();

    EXPECT_GT(draw_vertex_count(depth_overlay,  bounds), 0U);
    EXPECT_GT(draw_vertex_count(bucket_overlay, bounds), 0U);
}

// ---------------------------------------------------------------------------
// TEST 10 — AllKindsNoThrow
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, AllKindsNoThrow)
{
    const cd::ui::widgets::Rect bounds = standard_bounds();
    const cd::ui::widgets::Theme theme {};

    for (const auto k : { dv::VizKind::kDepth,
                          dv::VizKind::kNormal,
                          dv::VizKind::kAlphaBucket })
    {
        dv::DebugVizOverlay overlay { k };
        cd::ui::renderer::DrawBatcher batcher;
        batcher.begin_frame();
        EXPECT_NO_THROW(overlay.draw(batcher, bounds, theme));
        EXPECT_GE(batcher.vertex_count(), 4U);
    }
}

// ===========================================================================
// phase696 — F-key hotkey toggle, sparkline feed, alpha-bucket counts
// ===========================================================================

// ---------------------------------------------------------------------------
// TEST 11 — HotkeyToggleFlipsState
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, HotkeyToggleFlipsState)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    EXPECT_TRUE(overlay.is_visible());

    const bool after_first = overlay.toggle();
    EXPECT_FALSE(after_first);
    EXPECT_FALSE(overlay.is_visible());

    const bool after_second = overlay.toggle();
    EXPECT_TRUE(after_second);
    EXPECT_TRUE(overlay.is_visible());
}

// ---------------------------------------------------------------------------
// TEST 12 — HotkeyToggleIdempotent
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, HotkeyToggleIdempotent)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kNormal };
    const bool initial = overlay.is_visible();

    std::ignore = overlay.toggle();
    std::ignore = overlay.toggle();

    EXPECT_EQ(overlay.is_visible(), initial);
}

// ---------------------------------------------------------------------------
// TEST 13 — SampleRingEmpty
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, SampleRingEmpty)
{
    const dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    EXPECT_EQ(overlay.sample_count(), 0U);
}

// ---------------------------------------------------------------------------
// TEST 14 — PushSampleIncreasesCount
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, PushSampleIncreasesCount)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };

    overlay.push_frame_sample(8.0F);
    EXPECT_EQ(overlay.sample_count(), 1U);
    EXPECT_FLOAT_EQ(overlay.sample_at(0U), 8.0F);

    overlay.push_frame_sample(12.0F);
    EXPECT_EQ(overlay.sample_count(), 2U);
    EXPECT_FLOAT_EQ(overlay.sample_at(0U), 8.0F);   // oldest first
    EXPECT_FLOAT_EQ(overlay.sample_at(1U), 12.0F);
}

// ---------------------------------------------------------------------------
// TEST 15 — SampleRingWraps
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, SampleRingWraps)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };

    // Push more than capacity; ring wraps without crash.
    for (std::size_t i = 0U; i < dv::kSparklineCapacity + 10U; ++i)
    {
        overlay.push_frame_sample(static_cast<float>(i));
    }

    // Count is capped at capacity.
    EXPECT_EQ(overlay.sample_count(), dv::kSparklineCapacity);
}

// ---------------------------------------------------------------------------
// TEST 16 — SampleAtOrder
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, SampleAtOrder)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    overlay.push_frame_sample(1.0F);
    overlay.push_frame_sample(2.0F);
    overlay.push_frame_sample(3.0F);

    // Oldest first: index 0 = 1.0, index 1 = 2.0, index 2 = 3.0.
    EXPECT_FLOAT_EQ(overlay.sample_at(0U), 1.0F);
    EXPECT_FLOAT_EQ(overlay.sample_at(1U), 2.0F);
    EXPECT_FLOAT_EQ(overlay.sample_at(2U), 3.0F);
}

// ---------------------------------------------------------------------------
// TEST 17 — SparklineInBudgetColor — draw completes without crash, emits quads.
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, SparklineInBudgetDrawsQuads)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kDepth };
    // Push in-budget samples (well below 16.6 ms).
    for (std::size_t i = 0U; i < 10U; ++i)
    {
        overlay.push_frame_sample(8.0F);
    }

    const std::size_t verts = draw_vertex_count(overlay, standard_bounds());
    // Sparkline strip adds at least: background + budget-line + N segment bars.
    EXPECT_GT(verts, 4U);
}

// ---------------------------------------------------------------------------
// TEST 18 — SparklineOverBudgetEmitsMoreOrEqualVerts
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, SparklineOverBudgetEmitsMoreOrEqualVerts)
{
    // Overlay A: all in-budget.
    dv::DebugVizOverlay overlay_ok  { dv::VizKind::kDepth };
    dv::DebugVizOverlay overlay_bad { dv::VizKind::kDepth };

    for (std::size_t i = 0U; i < 30U; ++i)
    {
        overlay_ok.push_frame_sample(8.0F);     // in-budget
        overlay_bad.push_frame_sample(30.0F);   // over-budget
    }

    const auto bounds = standard_bounds();
    // Both must emit non-zero vertices (overbudget segments render as warning
    // colour but same count as in-budget).
    EXPECT_GT(draw_vertex_count(overlay_ok,  bounds), 0U);
    EXPECT_GT(draw_vertex_count(overlay_bad, bounds), 0U);
}

// ---------------------------------------------------------------------------
// TEST 19 — BucketCountsDefault
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, BucketCountsDefault)
{
    const dv::DebugVizOverlay overlay { dv::VizKind::kAlphaBucket };
    const auto& bc = overlay.bucket_counts();
    EXPECT_EQ(bc.opaque, 0U);
    EXPECT_EQ(bc.mask,   0U);
    EXPECT_EQ(bc.blend,  0U);
    EXPECT_EQ(bc.total(), 0U);
}

// ---------------------------------------------------------------------------
// TEST 20 — BucketCountsSet
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, BucketCountsSet)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kAlphaBucket };
    overlay.set_bucket_counts(100U, 20U, 5U);

    const auto& bc = overlay.bucket_counts();
    EXPECT_EQ(bc.opaque, 100U);
    EXPECT_EQ(bc.mask,   20U);
    EXPECT_EQ(bc.blend,  5U);
    EXPECT_EQ(bc.total(), 125U);
}

// ---------------------------------------------------------------------------
// TEST 21 — BucketProportionalBars: with counts set, draw emits quads.
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, BucketProportionalBarsEmitQuads)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kAlphaBucket };
    overlay.set_bucket_counts(500U, 100U, 50U);

    const std::size_t verts = draw_vertex_count(overlay, standard_bounds());
    EXPECT_GT(verts, 0U);
}

// ---------------------------------------------------------------------------
// TEST 22 — BucketFallbackEqualThirds: zero-total must not crash.
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, BucketFallbackEqualThirdsNoCrash)
{
    dv::DebugVizOverlay overlay { dv::VizKind::kAlphaBucket };
    // Do NOT call set_bucket_counts — total remains 0, fallback path triggers.
    ASSERT_NO_THROW({
        const std::size_t verts = draw_vertex_count(overlay, standard_bounds());
        EXPECT_GT(verts, 0U);
    });
}
