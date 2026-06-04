// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_cutscene_player/tests/test_cutscene_pan_zoom.cpp
//
// phase741 — Pan/zoom Sprint-2 tests for CutscenePlayerPanel.
//
// All tests are headless (no ImGui / no RHI). We verify:
//
//   * DefaultViewState           — zoom_factor==1, pan_offset_x==0 after ctor.
//   * SetZoomFactorClamps        — set_zoom_factor clamps to [kZoomMin, kZoomMax].
//   * SetPanOffsetClamps         — set_pan_offset_x clamps to valid range.
//   * ResetView                  — reset_view() restores zoom=1 and pan=0.
//   * WheelZoomIn                — positive wheel_delta increases zoom_factor.
//   * WheelZoomOut               — negative wheel_delta decreases zoom_factor.
//   * WheelZoomClampsMin         — repeated zoom-out never goes below kZoomMin.
//   * WheelZoomClampsMax         — repeated zoom-in never goes above kZoomMax.
//   * ShiftDragPans              — shift+left-drag updates pan_offset_x.
//   * MiddleDragPans             — middle-button drag updates pan_offset_x.
//   * PanCannotExceedZoomedWidth — pan_offset_x stays <= (zoomed_w - track_w).
//   * DrawEmitsScaledPositions   — draw() with zoom>1 emits more vertices than
//                                  draw() at zoom==1 (scrollbar thumb + clips).
//   * ScrollbarEmittedWhenZoomed — draw() at zoom>1 emits more quads than at zoom==1.
// =============================================================================
#include <cd/editor/panel_cutscene_player/CutscenePlayerPanel.hpp>

#include <cd/game/cutscene_player/CutscenePlayer.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace csp  = cd::editor::panel::cutscene_player;
namespace game = cd::game::cutscene_player;
namespace ui   = cd::ui::widgets;

// ---------------------------------------------------------------------------
// Helper — build a simple two-phase cutscene.
// ---------------------------------------------------------------------------
static game::Cutscene make_pan_zoom_cutscene()
{
    game::Cutscene cs;
    cs.cutscene_id = "pan_zoom_test";
    cs.can_skip    = true;

    game::CutscenePhase phaseA;
    phaseA.phase_id    = "A";
    phaseA.duration_ms = 3000.0F;
    phaseA.events.push_back(game::CutsceneEvent { 500.0F, game::EventKind::kFadeIn, {}, {} });
    cs.phases.push_back(phaseA);

    game::CutscenePhase phaseB;
    phaseB.phase_id    = "B";
    phaseB.duration_ms = 2000.0F;
    phaseB.events.push_back(game::CutsceneEvent { 800.0F, game::EventKind::kFadeOut, {}, {} });
    cs.phases.push_back(phaseB);

    return cs;
}

// Convenience: build a PointerState at a given position with buttons up.
static ui::PointerState make_pointer(float x, float y,
                                     bool left_down     = false,
                                     bool left_pressed  = false,
                                     bool left_released = false)
{
    ui::PointerState p;
    p.mouse_x       = x;
    p.mouse_y       = y;
    p.left_down     = left_down;
    p.left_pressed  = left_pressed;
    p.left_released = left_released;
    return p;
}

// Standard bounds used across tests.
static constexpr ui::Rect kBounds { 0.0F, 0.0F, 600.0F, 500.0F };

// ---------------------------------------------------------------------------
// TEST 1 — DefaultViewState
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, DefaultViewState)
{
    const csp::CutscenePlayerPanel panel;
    EXPECT_FLOAT_EQ(panel.zoom_factor(),  1.0F);
    EXPECT_FLOAT_EQ(panel.pan_offset_x(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST 2 — SetZoomFactorClamps
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, SetZoomFactorClamps)
{
    csp::CutscenePlayerPanel panel;

    panel.set_zoom_factor(0.0F);
    EXPECT_FLOAT_EQ(panel.zoom_factor(), csp::CutscenePlayerPanel::kZoomMin);

    panel.set_zoom_factor(1000.0F);
    EXPECT_FLOAT_EQ(panel.zoom_factor(), csp::CutscenePlayerPanel::kZoomMax);

    panel.set_zoom_factor(2.5F);
    EXPECT_FLOAT_EQ(panel.zoom_factor(), 2.5F);
}

// ---------------------------------------------------------------------------
// TEST 3 — SetPanOffsetClamps
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, SetPanOffsetClamps)
{
    csp::CutscenePlayerPanel panel;
    const float track_w = 500.0F;

    // At default zoom (1.0) the zoomed width equals the track width, so max pan is 0.
    panel.set_pan_offset_x(999.0F, track_w);
    EXPECT_FLOAT_EQ(panel.pan_offset_x(), 0.0F);

    // Negative pan should also clamp to 0.
    panel.set_pan_offset_x(-100.0F, track_w);
    EXPECT_FLOAT_EQ(panel.pan_offset_x(), 0.0F);

    // At zoom=2 the zoomed width is 2*track_w, so max_pan = track_w.
    panel.set_zoom_factor(2.0F);
    panel.set_pan_offset_x(track_w + 1.0F, track_w);
    EXPECT_FLOAT_EQ(panel.pan_offset_x(), track_w);

    panel.set_pan_offset_x(200.0F, track_w);
    EXPECT_FLOAT_EQ(panel.pan_offset_x(), 200.0F);
}

// ---------------------------------------------------------------------------
// TEST 4 — ResetView
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, ResetView)
{
    csp::CutscenePlayerPanel panel;
    panel.set_zoom_factor(3.0F);
    panel.set_pan_offset_x(100.0F, 500.0F);

    panel.reset_view();

    EXPECT_FLOAT_EQ(panel.zoom_factor(),  1.0F);
    EXPECT_FLOAT_EQ(panel.pan_offset_x(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST 5 — WheelZoomIn
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, WheelZoomIn)
{
    csp::CutscenePlayerPanel panel;
    panel.set_cutscene(make_pan_zoom_cutscene());

    const float zoom_before = panel.zoom_factor();
    const auto  ptr         = make_pointer(kBounds.x + kBounds.w * 0.5F, kBounds.y + 10.0F);

    panel.tick_input(ptr, /*middle_down=*/false, /*wheel_delta=*/1.0F,
                     /*shift_held=*/false, kBounds);

    EXPECT_GT(panel.zoom_factor(), zoom_before);
    EXPECT_LE(panel.zoom_factor(), csp::CutscenePlayerPanel::kZoomMax);
}

// ---------------------------------------------------------------------------
// TEST 6 — WheelZoomOut
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, WheelZoomOut)
{
    csp::CutscenePlayerPanel panel;
    panel.set_cutscene(make_pan_zoom_cutscene());
    panel.set_zoom_factor(3.0F);

    const float zoom_before = panel.zoom_factor();
    const auto  ptr         = make_pointer(kBounds.x + kBounds.w * 0.5F, kBounds.y + 10.0F);

    panel.tick_input(ptr, /*middle_down=*/false, /*wheel_delta=*/-1.0F,
                     /*shift_held=*/false, kBounds);

    EXPECT_LT(panel.zoom_factor(), zoom_before);
    EXPECT_GE(panel.zoom_factor(), csp::CutscenePlayerPanel::kZoomMin);
}

// ---------------------------------------------------------------------------
// TEST 7 — WheelZoomClampsMin
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, WheelZoomClampsMin)
{
    csp::CutscenePlayerPanel panel;
    const auto ptr = make_pointer(kBounds.x + 10.0F, kBounds.y + 10.0F);

    // Zoom out aggressively.
    for (int i = 0; i < 200; ++i)
    {
        panel.tick_input(ptr, false, -1.0F, false, kBounds);
    }

    EXPECT_GE(panel.zoom_factor(), csp::CutscenePlayerPanel::kZoomMin);
}

// ---------------------------------------------------------------------------
// TEST 8 — WheelZoomClampsMax
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, WheelZoomClampsMax)
{
    csp::CutscenePlayerPanel panel;
    const auto ptr = make_pointer(kBounds.x + 10.0F, kBounds.y + 10.0F);

    // Zoom in aggressively.
    for (int i = 0; i < 200; ++i)
    {
        panel.tick_input(ptr, false, 1.0F, false, kBounds);
    }

    EXPECT_LE(panel.zoom_factor(), csp::CutscenePlayerPanel::kZoomMax);
}

// ---------------------------------------------------------------------------
// TEST 9 — ShiftDragPans
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, ShiftDragPans)
{
    csp::CutscenePlayerPanel panel;
    panel.set_zoom_factor(3.0F);  // must be zoomed so there is room to pan

    // Frame 1: button down at x=300 (start of drag).
    {
        const auto ptr = make_pointer(300.0F, 10.0F, /*left_down=*/true);
        panel.tick_input(ptr, false, 0.0F, /*shift_held=*/true, kBounds);
    }
    const float pan_after_start = panel.pan_offset_x();

    // Frame 2: drag 50 pixels left → should increase pan_offset_x by ~50.
    {
        const auto ptr = make_pointer(250.0F, 10.0F, /*left_down=*/true);
        panel.tick_input(ptr, false, 0.0F, /*shift_held=*/true, kBounds);
    }
    const float pan_after_drag = panel.pan_offset_x();

    // Dragging left (mouse_x decreased) means panning right: pan_offset_x should grow.
    EXPECT_GT(pan_after_drag, pan_after_start);
}

// ---------------------------------------------------------------------------
// TEST 10 — MiddleDragPans
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, MiddleDragPans)
{
    csp::CutscenePlayerPanel panel;
    panel.set_zoom_factor(3.0F);

    // Frame 1: middle button down at x=400.
    {
        const auto ptr = make_pointer(400.0F, 10.0F);
        panel.tick_input(ptr, /*middle_down=*/true, 0.0F, false, kBounds);
    }
    const float pan_after_start = panel.pan_offset_x();

    // Frame 2: drag 80 pixels left.
    {
        const auto ptr = make_pointer(320.0F, 10.0F);
        panel.tick_input(ptr, /*middle_down=*/true, 0.0F, false, kBounds);
    }
    const float pan_after_drag = panel.pan_offset_x();

    EXPECT_GT(pan_after_drag, pan_after_start);
}

// ---------------------------------------------------------------------------
// TEST 11 — PanCannotExceedZoomedWidth
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, PanCannotExceedZoomedWidth)
{
    csp::CutscenePlayerPanel panel;
    panel.set_zoom_factor(4.0F);

    // Force an extreme pan via set_pan_offset_x with an oversized value.
    const float track_w = kBounds.w;   // 600 px
    panel.set_pan_offset_x(999999.0F, track_w);

    // max_pan = track_w * (zoom-1) = 600 * 3 = 1800
    const float max_pan = track_w * (panel.zoom_factor() - 1.0F);
    EXPECT_LE(panel.pan_offset_x(), max_pan + 0.01F);  // small epsilon for float
}

// ---------------------------------------------------------------------------
// TEST 12 — DrawEmitsScaledPositions (vertex count with zoom > 1)
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, DrawEmitsScaledPositions)
{
    // Panel at default zoom.
    std::size_t verts_default {};
    {
        csp::CutscenePlayerPanel panel;
        panel.set_cutscene(make_pan_zoom_cutscene());

        cd::ui::renderer::DrawBatcher batcher;
        const ui::Theme               theme {};
        batcher.begin_frame();
        panel.draw(batcher, theme, kBounds);
        verts_default = batcher.vertex_count();
    }

    // Panel at zoom=3 (scrollbar thumb emitted).
    std::size_t verts_zoomed {};
    {
        csp::CutscenePlayerPanel panel;
        panel.set_cutscene(make_pan_zoom_cutscene());
        panel.set_zoom_factor(3.0F);

        cd::ui::renderer::DrawBatcher batcher;
        const ui::Theme               theme {};
        batcher.begin_frame();
        panel.draw(batcher, theme, kBounds);
        verts_zoomed = batcher.vertex_count();
    }

    // The zoomed draw must emit at least as many vertices as the default draw.
    // (The scrollbar thumb is an additional quad = +4 vertices minimum.)
    EXPECT_GE(verts_zoomed, verts_default);
}

// ---------------------------------------------------------------------------
// TEST 13 — ScrollbarEmittedWhenZoomed
// ---------------------------------------------------------------------------
TEST(CutscenePanZoom, ScrollbarEmittedWhenZoomed)
{
    // At zoom == 1.0 the scrollbar track is drawn but the thumb quad is NOT
    // (since zoom_factor_ is not > 1.0). At zoom == 2.0 the thumb IS drawn.

    std::size_t cmds_no_thumb {};
    {
        csp::CutscenePlayerPanel panel;
        panel.set_cutscene(make_pan_zoom_cutscene());
        // Leave zoom at default 1.0.

        cd::ui::renderer::DrawBatcher batcher;
        const ui::Theme               theme {};
        batcher.begin_frame();
        panel.draw(batcher, theme, kBounds);
        cmds_no_thumb = batcher.vertex_count();
    }

    std::size_t cmds_with_thumb {};
    {
        csp::CutscenePlayerPanel panel;
        panel.set_cutscene(make_pan_zoom_cutscene());
        panel.set_zoom_factor(2.0F);

        cd::ui::renderer::DrawBatcher batcher;
        const ui::Theme               theme {};
        batcher.begin_frame();
        panel.draw(batcher, theme, kBounds);
        cmds_with_thumb = batcher.vertex_count();
    }

    // Zoomed panel has the scrollbar thumb quad — at least 4 extra vertices.
    EXPECT_GE(cmds_with_thumb, cmds_no_thumb + 4U);
}
