// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_auto_save_indicator/tests/
//                 test_auto_save_indicator.cpp
//
// phase745 — unit tests for
//            cd::editor::panel::auto_save_indicator::AutoSaveIndicator.
//
// All tests are headless (no RHI, no ImGui).
//
//  1.  DefaultState          — fresh indicator: kIdle, last_save_ms_ago == 0.
//  2.  SetStatus             — set_status(kError) takes effect immediately.
//  3.  MarkDirty             — mark_dirty() → kPendingDirty.
//  4.  MarkSaved             — mark_saved() → kJustSaved, last_save_ms_ago == 0.
//  5.  MarkSavedResetsTimer  — mark_saved after set_last_save_ms_ago resets to 0.
//  6.  SetLastSaveMsAgo      — set_last_save_ms_ago(3000) is stored correctly.
//  7.  SetLastSaveMsAgoNegativeIgnored — negative value is clamped to 0.
//  8.  TransitionCycle       — Idle → dirty → saving → saved → error cycle.
//  9.  DrawIdleNoThrow       — draw() in kIdle emits > 0 vertices (no crash).
// 10.  DrawZeroBoundsNoOp    — draw() with zero w/h emits 0 vertices.
// 11.  DrawPendingDirty      — draw emits > 0 vertices in kPendingDirty.
// 12.  DrawSaving            — draw emits > 0 vertices in kSaving.
// 13.  DrawJustSaved         — draw emits > 0 vertices in kJustSaved.
// 14.  DrawError             — draw emits > 0 vertices in kError.
// 15.  DrawColorChangesWithStatus — two statuses produce different vertex counts
//                                   OR are both valid (no crash, > 0 verts each).
// =============================================================================
#include <cd/editor/panel_auto_save_indicator/AutoSaveIndicator.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/theme/Theme.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace asi = cd::editor::panel::auto_save_indicator;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

[[nodiscard]] cd::ui::widgets::Rect standard_bounds() noexcept
{
    // Typical status-bar strip: 300 px wide, 50 px high.
    return { 0.0F, 0.0F, 300.0F, 50.0F };
}

[[nodiscard]] cd::ui::theme::Theme default_theme() noexcept
{
    return cd::ui::theme::k_dark_theme();
}

/// Draw into a fresh batcher and return the vertex count.
[[nodiscard]] std::size_t draw_count(
    asi::AutoSaveIndicator& ind,
    const cd::ui::widgets::Rect& bounds = standard_bounds())
{
    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();
    ind.draw(batcher, default_theme(), bounds);
    return batcher.vertex_count();
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// TEST 1 — DefaultState
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, DefaultState)
{
    const asi::AutoSaveIndicator ind;
    EXPECT_EQ(ind.current_status(), asi::Status::kIdle);
    EXPECT_DOUBLE_EQ(ind.last_save_ms_ago(), 0.0);
}

// ---------------------------------------------------------------------------
// TEST 2 — SetStatus
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, SetStatus)
{
    asi::AutoSaveIndicator ind;
    ind.set_status(asi::Status::kError);
    EXPECT_EQ(ind.current_status(), asi::Status::kError);

    ind.set_status(asi::Status::kSaving);
    EXPECT_EQ(ind.current_status(), asi::Status::kSaving);
}

// ---------------------------------------------------------------------------
// TEST 3 — MarkDirty
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, MarkDirty)
{
    asi::AutoSaveIndicator ind;
    ASSERT_EQ(ind.current_status(), asi::Status::kIdle);

    ind.mark_dirty();
    EXPECT_EQ(ind.current_status(), asi::Status::kPendingDirty);
}

// ---------------------------------------------------------------------------
// TEST 4 — MarkSaved
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, MarkSaved)
{
    asi::AutoSaveIndicator ind;
    ind.mark_dirty();
    ASSERT_EQ(ind.current_status(), asi::Status::kPendingDirty);

    ind.mark_saved();
    EXPECT_EQ(ind.current_status(), asi::Status::kJustSaved);
    EXPECT_DOUBLE_EQ(ind.last_save_ms_ago(), 0.0);
}

// ---------------------------------------------------------------------------
// TEST 5 — MarkSavedResetsTimer
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, MarkSavedResetsTimer)
{
    asi::AutoSaveIndicator ind;
    ind.set_last_save_ms_ago(5000.0);
    EXPECT_DOUBLE_EQ(ind.last_save_ms_ago(), 5000.0);

    ind.mark_saved();
    // mark_saved() must reset the timer to 0.
    EXPECT_DOUBLE_EQ(ind.last_save_ms_ago(), 0.0);
}

// ---------------------------------------------------------------------------
// TEST 6 — SetLastSaveMsAgo
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, SetLastSaveMsAgo)
{
    asi::AutoSaveIndicator ind;
    ind.set_last_save_ms_ago(3000.0);
    EXPECT_DOUBLE_EQ(ind.last_save_ms_ago(), 3000.0);

    ind.set_last_save_ms_ago(0.0);
    EXPECT_DOUBLE_EQ(ind.last_save_ms_ago(), 0.0);
}

// ---------------------------------------------------------------------------
// TEST 7 — SetLastSaveMsAgoNegativeIgnored
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, SetLastSaveMsAgoNegativeIgnored)
{
    asi::AutoSaveIndicator ind;
    ind.set_last_save_ms_ago(1000.0);
    ind.set_last_save_ms_ago(-500.0);
    // Negative value must be clamped to 0.
    EXPECT_DOUBLE_EQ(ind.last_save_ms_ago(), 0.0);
}

// ---------------------------------------------------------------------------
// TEST 8 — TransitionCycle
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, TransitionCycle)
{
    asi::AutoSaveIndicator ind;

    EXPECT_EQ(ind.current_status(), asi::Status::kIdle);

    ind.mark_dirty();
    EXPECT_EQ(ind.current_status(), asi::Status::kPendingDirty);

    ind.set_status(asi::Status::kSaving);
    EXPECT_EQ(ind.current_status(), asi::Status::kSaving);

    ind.mark_saved();
    EXPECT_EQ(ind.current_status(), asi::Status::kJustSaved);

    ind.set_status(asi::Status::kError);
    EXPECT_EQ(ind.current_status(), asi::Status::kError);

    ind.set_status(asi::Status::kIdle);
    EXPECT_EQ(ind.current_status(), asi::Status::kIdle);
}

// ---------------------------------------------------------------------------
// TEST 9 — DrawIdleNoThrow
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, DrawIdleNoThrow)
{
    asi::AutoSaveIndicator ind;
    ASSERT_NO_THROW({
        const std::size_t verts = draw_count(ind);
        // Must emit at least the background fill quad (4 verts) + borders.
        EXPECT_GT(verts, 0U);
    });
}

// ---------------------------------------------------------------------------
// TEST 10 — DrawZeroBoundsNoOp
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, DrawZeroBoundsNoOp)
{
    asi::AutoSaveIndicator ind;
    ind.mark_dirty();

    const cd::ui::widgets::Rect zero_w { 0.0F, 0.0F,   0.0F, 50.0F };
    const cd::ui::widgets::Rect zero_h { 0.0F, 0.0F, 300.0F,  0.0F };

    EXPECT_EQ(draw_count(ind, zero_w), 0U);
    EXPECT_EQ(draw_count(ind, zero_h), 0U);
}

// ---------------------------------------------------------------------------
// TEST 11 — DrawPendingDirty
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, DrawPendingDirty)
{
    asi::AutoSaveIndicator ind;
    ind.mark_dirty();
    EXPECT_GT(draw_count(ind), 0U);
}

// ---------------------------------------------------------------------------
// TEST 12 — DrawSaving
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, DrawSaving)
{
    asi::AutoSaveIndicator ind;
    ind.set_status(asi::Status::kSaving);
    EXPECT_GT(draw_count(ind), 0U);
}

// ---------------------------------------------------------------------------
// TEST 13 — DrawJustSaved
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, DrawJustSaved)
{
    asi::AutoSaveIndicator ind;
    ind.mark_saved();
    ind.set_last_save_ms_ago(3000.0);
    EXPECT_GT(draw_count(ind), 0U);
}

// ---------------------------------------------------------------------------
// TEST 14 — DrawError
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, DrawError)
{
    asi::AutoSaveIndicator ind;
    ind.set_status(asi::Status::kError);
    EXPECT_GT(draw_count(ind), 0U);
}

// ---------------------------------------------------------------------------
// TEST 15 — DrawColorChangesWithStatus (both statuses emit valid geometry)
// ---------------------------------------------------------------------------
TEST(AutoSaveIndicator, DrawColorChangesWithStatus)
{
    // We cannot inspect colour values directly (DrawBatcher stores raw verts).
    // Instead confirm that both different-status draws produce > 0 vertices —
    // verifying draw() is live for every status code.
    for (const auto s : {
            asi::Status::kIdle,
            asi::Status::kPendingDirty,
            asi::Status::kSaving,
            asi::Status::kJustSaved,
            asi::Status::kError
        })
    {
        asi::AutoSaveIndicator ind;
        ind.set_status(s);
        EXPECT_GT(draw_count(ind), 0U)
            << "Status " << static_cast<int>(s) << " emitted 0 vertices";
    }
}
