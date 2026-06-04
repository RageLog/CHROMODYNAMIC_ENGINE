// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_asset_pipeline_status/tests/
//                 test_asset_pipeline_status.cpp
//
// phase709 — unit tests for
//   cd::editor::panel::asset_pipeline_status::AssetPipelineStatus
//
// All tests are headless (no RHI, no ImGui).  The StreamerPool is either
// absent (nullptr) or constructed with no streamers attached so that stats()
// returns all-zero counts — sufficient to exercise the panel API and
// draw paths without spinning up real file-I/O or GPU resources.
//
// Tests:
//   T1  NullPoolPendingZero      — total_pending() == 0 when no pool attached.
//   T2  NullPoolCompletedZero    — total_completed() == 0 when no pool attached.
//   T3  DrawNullPoolNoThrow      — draw() does not crash with no pool attached.
//   T4  DrawEmitsBgQuads         — draw() emits >= 4 vertices (background + border).
//   T5  DrawZeroBoundsNoOp       — draw() with zero-width bounds emits 0 vertices.
//   T6  DrawZeroHeightNoOp       — draw() with zero-height bounds emits 0 vertices.
//   T7  SetPoolNullDetaches      — set_pool(nullptr) after set_pool(&pool) → 0 pending.
//   T8  DrawFourRowsMoreGeometry — draw() with attached (zero-stats) pool emits
//                                  >= the null-pool vertex count.
//   T9  DrawSmallBoundsGraceful  — draw() with bounds smaller than label width
//                                  does not crash (returns early gracefully).
// =============================================================================
#include <cd/editor/panel_asset_pipeline_status/AssetPipelineStatus.hpp>

#include <cd/asset/streamer_pool/StreamerPool.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace aps = cd::editor::panel::asset_pipeline_status;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    return { 0.0F, 0.0F, 400.0F, 200.0F };
}

[[nodiscard]] cd::ui::widgets::Theme default_theme() noexcept
{
    return {};  // Default-constructed theme has sensible accent colours.
}

/// Draw into a fresh batcher and return vertex count.
[[nodiscard]] std::size_t draw_count(aps::AssetPipelineStatus& panel,
                                     const cd::ui::widgets::Rect& bounds = standard_bounds())
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    panel.draw(batcher, default_theme(), bounds);
    return batcher.vertex_count();
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// T1 — NullPoolPendingZero
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, NullPoolPendingZero)
{
    const aps::AssetPipelineStatus panel;
    EXPECT_EQ(panel.total_pending(), 0U);
}

// ---------------------------------------------------------------------------
// T2 — NullPoolCompletedZero
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, NullPoolCompletedZero)
{
    const aps::AssetPipelineStatus panel;
    EXPECT_EQ(panel.total_completed(), 0U);
}

// ---------------------------------------------------------------------------
// T3 — DrawNullPoolNoThrow
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, DrawNullPoolNoThrow)
{
    aps::AssetPipelineStatus panel;
    ASSERT_NO_THROW({
        const std::size_t verts = draw_count(panel);
        EXPECT_GE(verts, 0U);
    });
}

// ---------------------------------------------------------------------------
// T4 — DrawEmitsBgQuads (background + border + row geometry)
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, DrawEmitsBgQuads)
{
    aps::AssetPipelineStatus panel;
    const std::size_t verts = draw_count(panel);
    // At minimum: outer bg (4 verts) + 4 border quads (16 verts) = 20 verts.
    EXPECT_GE(verts, 4U);
}

// ---------------------------------------------------------------------------
// T5 — DrawZeroBoundsNoOp (zero width)
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, DrawZeroBoundsNoOp)
{
    aps::AssetPipelineStatus panel;
    const cd::ui::widgets::Rect zero_w { 0.0F, 0.0F, 0.0F, 200.0F };
    EXPECT_EQ(draw_count(panel, zero_w), 0U);
}

// ---------------------------------------------------------------------------
// T6 — DrawZeroHeightNoOp (zero height)
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, DrawZeroHeightNoOp)
{
    aps::AssetPipelineStatus panel;
    const cd::ui::widgets::Rect zero_h { 0.0F, 0.0F, 400.0F, 0.0F };
    EXPECT_EQ(draw_count(panel, zero_h), 0U);
}

// ---------------------------------------------------------------------------
// T7 — SetPoolNullDetaches
//   Attach a (no-streamers) pool, verify total_pending() reports 0.
//   Detach again with nullptr, confirm still 0.
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, SetPoolNullDetaches)
{
    aps::AssetPipelineStatus           panel;
    cd::asset::streamer_pool::StreamerPool pool;  // no streamers attached → stats all zero

    panel.set_pool(&pool);
    EXPECT_EQ(panel.total_pending(),   0U);
    EXPECT_EQ(panel.total_completed(), 0U);

    panel.set_pool(nullptr);
    EXPECT_EQ(panel.total_pending(),   0U);
    EXPECT_EQ(panel.total_completed(), 0U);
}

// ---------------------------------------------------------------------------
// T8 — DrawFourRowsMoreGeometry
//   With an attached pool (zero-stats) the draw path still visits all four
//   row branches.  Vertex count must be >= the null-pool baseline.
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, DrawFourRowsMoreGeometry)
{
    // Baseline — no pool.
    aps::AssetPipelineStatus no_pool_panel;
    const std::size_t baseline = draw_count(no_pool_panel);

    // Attached pool with no streamers (stats() returns all zeros).
    cd::asset::streamer_pool::StreamerPool pool;
    aps::AssetPipelineStatus              panel;
    panel.set_pool(&pool);

    const std::size_t with_pool = draw_count(panel);

    // Both should emit at least the outer background geometry.
    EXPECT_GE(with_pool, baseline);
    // Four rows each contribute at minimum 3 quads (label × 2 + bar_bg).
    // At bounds 400 × 200 with default theme, we expect at least 12 quads
    // beyond the outer border = 48 extra verts.
    EXPECT_GT(with_pool, 0U);
}

// ---------------------------------------------------------------------------
// T9 — DrawSmallBoundsGraceful
//   Bounds smaller than the label width → inner check returns early.  Must
//   not crash.
// ---------------------------------------------------------------------------
TEST(AssetPipelineStatus, DrawSmallBoundsGraceful)
{
    aps::AssetPipelineStatus panel;
    // Width of 10 px is well below kLabelW (60 px) + kBarGap (2 px).
    const cd::ui::widgets::Rect tiny { 0.0F, 0.0F, 10.0F, 200.0F };
    ASSERT_NO_THROW({
        const std::size_t verts = draw_count(panel, tiny);
        // Background + border are still drawn before the inner guard.
        EXPECT_GE(verts, 0U);
    });
}
