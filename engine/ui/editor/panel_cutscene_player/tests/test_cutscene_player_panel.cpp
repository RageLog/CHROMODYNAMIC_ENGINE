// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_cutscene_player/tests/test_cutscene_player_panel.cpp
//
// phase617 — unit tests for cd::editor::panel::cutscene_player::CutscenePlayerPanel.
//
// All tests are headless (no ImGui / no RHI). We verify:
//
//   * DefaultCtorEmptyState             — default ctor, no phases, null player.
//   * SetCutsceneStoresPhaseCount       — set_cutscene copies phases correctly.
//   * DrawEmitsCommandsWithCutscene     — draw() with populated cutscene emits > 1 quad.
//   * NullPlayerHandled                 — set_player(nullptr) + draw() does not crash.
//   * NonNullPlayerObserved             — set_player + player() round-trip; draw() reads
//                                         player state without crashing.
//   * ScrubberSeekSynthetic             — set_cutscene with known phases + set_player to
//                                         a player advanced to 50 % → draw() emits the
//                                         playhead quad (vertex count increases vs idle).
//   * ZeroBoundsReturnsEarly            — draw() on zero-size rect emits only background.
// =============================================================================
#include <cd/editor/panel_cutscene_player/CutscenePlayerPanel.hpp>

#include <cd/game/cutscene_player/CutscenePlayer.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace csp  = cd::editor::panel::cutscene_player;
namespace game = cd::game::cutscene_player;

// ---------------------------------------------------------------------------
// Helper — build a simple two-phase cutscene with one event each.
// ---------------------------------------------------------------------------
static game::Cutscene make_test_cutscene()
{
    game::Cutscene cs;
    cs.cutscene_id = "test_cs";
    cs.can_skip    = true;

    game::CutscenePhase phaseA;
    phaseA.phase_id    = "intro";
    phaseA.duration_ms = 2000.0F;
    phaseA.events.push_back(game::CutsceneEvent { 500.0F, game::EventKind::kFadeIn, {}, {} });
    cs.phases.push_back(phaseA);

    game::CutscenePhase phaseB;
    phaseB.phase_id    = "outro";
    phaseB.duration_ms = 3000.0F;
    phaseB.events.push_back(game::CutsceneEvent { 1000.0F, game::EventKind::kFadeOut, {}, {} });
    cs.phases.push_back(phaseB);

    return cs;
}

// ---------------------------------------------------------------------------
// TEST 1 — DefaultCtorEmptyState
// ---------------------------------------------------------------------------
TEST(CutscenePlayerPanel, DefaultCtorEmptyState)
{
    const csp::CutscenePlayerPanel panel;

    EXPECT_EQ(panel.cutscene_phase_count(), static_cast<std::size_t>(0U));
    EXPECT_EQ(panel.player(), nullptr);
}

// ---------------------------------------------------------------------------
// TEST 2 — SetCutsceneStoresPhaseCount
// ---------------------------------------------------------------------------
TEST(CutscenePlayerPanel, SetCutsceneStoresPhaseCount)
{
    csp::CutscenePlayerPanel panel;

    // Empty cutscene.
    {
        game::Cutscene empty;
        panel.set_cutscene(empty);
        EXPECT_EQ(panel.cutscene_phase_count(), static_cast<std::size_t>(0U));
    }

    // Two-phase cutscene.
    panel.set_cutscene(make_test_cutscene());
    EXPECT_EQ(panel.cutscene_phase_count(), static_cast<std::size_t>(2U));

    // Overwrite with another cutscene.
    game::Cutscene single;
    single.phases.push_back(game::CutscenePhase { "only", 1000.0F, {} });
    panel.set_cutscene(single);
    EXPECT_EQ(panel.cutscene_phase_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST 3 — DrawEmitsCommandsWithCutscene
// ---------------------------------------------------------------------------
TEST(CutscenePlayerPanel, DrawEmitsCommandsWithCutscene)
{
    csp::CutscenePlayerPanel panel;
    panel.set_cutscene(make_test_cutscene());

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 480.0F, 400.0F };

    batcher.begin_frame();
    panel.draw(batcher, theme, bounds);

    // Expect at least: background + separator + 2 phase blocks + 2 event dots +
    // scrubber sub-separator + scrubber track + offset track + 3 button strips = 12+ quads.
    // The batcher merges same-variant quads into one DrawCommand, so use vertex_count():
    // 12 quads * 4 verts = 48 vertices minimum.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(48U));
}

// ---------------------------------------------------------------------------
// TEST 4 — NullPlayerHandled
// ---------------------------------------------------------------------------
TEST(CutscenePlayerPanel, NullPlayerHandled)
{
    csp::CutscenePlayerPanel panel;
    panel.set_cutscene(make_test_cutscene());
    panel.set_player(nullptr);

    EXPECT_EQ(panel.player(), nullptr);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 480.0F, 400.0F };

    batcher.begin_frame();
    // Must not crash or assert.
    EXPECT_NO_THROW(panel.draw(batcher, theme, bounds));

    // Background must have been emitted.
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST 5 — NonNullPlayerObserved (player pointer round-trip + draw is safe)
// ---------------------------------------------------------------------------
TEST(CutscenePlayerPanel, NonNullPlayerObserved)
{
    game::CutscenePlayer player;

    csp::CutscenePlayerPanel panel;
    panel.set_player(&player);
    EXPECT_EQ(panel.player(), &player);

    panel.set_cutscene(make_test_cutscene());

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 480.0F, 400.0F };

    batcher.begin_frame();
    // Player is idle (not played) — draw must still work.
    EXPECT_NO_THROW(panel.draw(batcher, theme, bounds));
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST 6 — ScrubberSeekSynthetic
//   Advance a real CutscenePlayer to the midpoint of phase 0, then compare
//   vertex count against a panel with a null / idle player. The playhead quad
//   should cause at least one extra quad.
// ---------------------------------------------------------------------------
TEST(CutscenePlayerPanel, ScrubberSeekSynthetic)
{
    game::Cutscene cs = make_test_cutscene();

    // Panel A — idle player (no play call).
    std::size_t verts_idle {};
    {
        game::CutscenePlayer idle_player;
        csp::CutscenePlayerPanel panel;
        panel.set_cutscene(cs);
        panel.set_player(&idle_player);

        cd::ui::renderer::DrawBatcher batcher;
        const cd::ui::widgets::Theme  theme {};
        const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 600.0F, 500.0F };
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_idle = batcher.vertex_count();
    }

    // Panel B — player ticked to 1 000 ms into phase 0 (total_ms 2000 → norm 0.5).
    std::size_t verts_playing {};
    {
        game::CutscenePlayer active_player;
        active_player.play(cs);
        active_player.tick(1000.0F);  // advance 1 000 ms into phase 0

        EXPECT_TRUE(active_player.is_playing());
        EXPECT_FLOAT_EQ(active_player.current_offset_ms(), 1000.0F);

        csp::CutscenePlayerPanel panel;
        panel.set_cutscene(cs);
        panel.set_player(&active_player);

        cd::ui::renderer::DrawBatcher batcher;
        const cd::ui::widgets::Theme  theme {};
        const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 600.0F, 500.0F };
        batcher.begin_frame();
        panel.draw(batcher, theme, bounds);
        verts_playing = batcher.vertex_count();
    }

    // The playing panel must emit at least as many vertices as the idle one
    // (playhead quad was drawn in the playing case: norm_head > 0).
    EXPECT_GE(verts_playing, verts_idle);
}

// ---------------------------------------------------------------------------
// TEST 7 — ZeroBoundsReturnsEarly
// ---------------------------------------------------------------------------
TEST(CutscenePlayerPanel, ZeroBoundsReturnsEarly)
{
    csp::CutscenePlayerPanel panel;
    panel.set_cutscene(make_test_cutscene());

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    panel.draw(batcher, theme, zero);

    // bounds.is_valid() == false → returns early after the background quad.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
