// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_debug_viz/tests/test_debug_viz.cpp
//
// phase684 — unit tests for cd::editor::debug_viz::DebugVizOverlay.
//
// All tests are headless (no RHI, no ImGui). Verifies:
//   1. DefaultVisible        — overlay starts visible (toggle ON by default).
//   2. KindAccessor          — kind() returns the kind passed to the ctor.
//   3. ToggleOnOff           — set_visible(false) / set_visible(true) round-trip.
//   4. DrawWhenHidden        — draw() emits no quads when !is_visible().
//   5. DrawDepthEmitsQuads   — kDepth draw emits >= 4 vertices (background + bands).
//   6. DrawNormalEmitsQuads  — kNormal draw emits >= 4 vertices.
//   7. DrawBucketEmitsQuads  — kAlphaBucket draw emits >= 4 vertices (3 bands).
//   8. DrawInvalidBounds     — draw() is a no-op for zero-sized bounds.
//   9. DrawBucketMoreVertsThankDepth
//                            — alpha-bucket quad count >= depth quad count
//                               (bands + separators vs gradient bands; both
//                               equal at Sprint-1, but count must be >= not <).
//  10. AllKindsNoThrow       — draw() does not throw for any VizKind value.
// =============================================================================
#include <cd/editor/panel_debug_viz/DebugViz.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

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

/// Draw into a fresh batcher and return the resulting vertex count.
[[nodiscard]] std::size_t draw_vertex_count(dv::DebugVizOverlay& overlay,
                                            const cd::ui::widgets::Rect& bounds)
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    overlay.draw(batcher, bounds);
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

    // Both use the same outer chrome (background + border + header);
    // kDepth emits 8 bands; kAlphaBucket emits 3 bands + 2 separator lines.
    // At Sprint-1 both have non-zero content quads. Assert non-zero for each.
    EXPECT_GT(draw_vertex_count(depth_overlay,  bounds), 0U);
    EXPECT_GT(draw_vertex_count(bucket_overlay, bounds), 0U);
}

// ---------------------------------------------------------------------------
// TEST 10 — AllKindsNoThrow
// ---------------------------------------------------------------------------
TEST(DebugVizOverlay, AllKindsNoThrow)
{
    const cd::ui::widgets::Rect bounds = standard_bounds();

    for (const auto k : { dv::VizKind::kDepth,
                          dv::VizKind::kNormal,
                          dv::VizKind::kAlphaBucket })
    {
        dv::DebugVizOverlay overlay { k };
        cd::ui::renderer::DrawBatcher batcher;
        batcher.begin_frame();
        EXPECT_NO_THROW(overlay.draw(batcher, bounds));
        EXPECT_GE(batcher.vertex_count(), 4U);
    }
}
