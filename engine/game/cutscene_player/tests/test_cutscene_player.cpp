// =============================================================================
// CHROMODYNAMIC - cd/game/cutscene_player/tests/test_cutscene_player.cpp
// Phase 610 - cd::game::cutscene_player unit tests (M7 W4B).
//
// Covers the 7 contract requirements from the brief:
//
//   T1. play + tick fires events at the correct offset.
//   T2. pause + resume preserves the playhead offset.
//   T3. stop resets the player to kIdle (can_skip == true).
//   T4. can_skip == false: stop() is a no-op.
//   T5. Multiple-phase progression: events fire in the correct phase order.
//   T6. is_complete() becomes true only when all phases finish naturally.
//   T7. Events at offset_ms == 0 fire on the first tick of their phase.
//
// Extras:
//   T8.  Empty-phase cutscene completes immediately on play().
//   T9.  dt spanning multiple phase boundaries fires events in all phases.
//   T10. Negative dt is clamped; playhead does not go backwards.
// =============================================================================
#include <cd/game/cutscene_player/CutscenePlayer.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace cd::game::cutscene_player::tests
{

// Helper: build a one-phase cutscene with named events at given offsets.
static Cutscene make_single_phase(const std::string& csid,
                                  float              phase_duration_ms,
                                  std::vector<std::pair<float, EventKind>> events,
                                  bool               can_skip = true)
{
    CutscenePhase phase;
    phase.phase_id    = "p0";
    phase.duration_ms = phase_duration_ms;
    for (auto& [off, kind] : events)
    {
        CutsceneEvent ev;
        ev.offset_ms = off;
        ev.kind      = kind;
        phase.events.push_back(ev);
    }
    Cutscene cs;
    cs.cutscene_id = csid;
    cs.can_skip    = can_skip;
    cs.phases.push_back(std::move(phase));
    return cs;
}

// ============================================================================
// T1: play + tick fires events at the right offset
// ============================================================================
TEST(CutscenePlayerTest, T1_PlayTickFiresEventsAtCorrectOffset)
{
    CutscenePlayer player;

    // Cutscene: 1000 ms, two events at 100 ms and 600 ms.
    const Cutscene cs = make_single_phase(
        "cs_t1", 1000.0F,
        {{100.0F, EventKind::kPlaySound}, {600.0F, EventKind::kCameraMove}});

    player.play(cs);
    ASSERT_TRUE(player.is_playing());
    ASSERT_FALSE(player.is_complete());

    // Tick to 99 ms -- no events should fire yet.
    player.tick(99.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());
    EXPECT_FALSE(player.is_complete());

    // Tick 1 ms -- crosses 100 ms; kPlaySound should fire.
    player.tick(1.0F);
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kPlaySound);
    }

    // Tick 500 ms -- reaches 600 ms exactly; kCameraMove fires.
    // (playhead is at 100 ms; 100 + 500 = 600 ms)
    player.tick(500.0F);
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kCameraMove);
    }

    // Tick 400 ms -- crosses end of phase; no more events.
    player.tick(400.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());
    EXPECT_TRUE(player.is_complete());
    EXPECT_FALSE(player.is_playing());
}

// ============================================================================
// T2: pause + resume preserves the playhead offset
// ============================================================================
TEST(CutscenePlayerTest, T2_PauseResumePreservesOffset)
{
    CutscenePlayer player;

    const Cutscene cs = make_single_phase(
        "cs_t2", 2000.0F,
        {{500.0F, EventKind::kFadeOut}, {1500.0F, EventKind::kFadeIn}});

    player.play(cs);

    // Advance to 300 ms.
    player.tick(300.0F);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 300.0F);

    // Pause.
    player.pause();
    EXPECT_FALSE(player.is_playing());

    // Several ticks while paused -- offset must not change.
    player.tick(100.0F);
    player.tick(200.0F);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 300.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());

    // Resume and tick to cross 500 ms event.
    player.resume();
    EXPECT_TRUE(player.is_playing());
    player.tick(210.0F);  // 300 + 210 = 510 ms -- crosses 500 ms event.
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kFadeOut);
    }
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 510.0F);
}

// ============================================================================
// T3: stop resets the player (can_skip == true)
// ============================================================================
TEST(CutscenePlayerTest, T3_StopResetsPlayer)
{
    CutscenePlayer player;

    const Cutscene cs = make_single_phase(
        "cs_t3", 5000.0F,
        {{1000.0F, EventKind::kCharacterTalk}},
        /*can_skip=*/true);

    player.play(cs);
    player.tick(800.0F);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 800.0F);
    EXPECT_TRUE(player.is_playing());

    player.stop();
    EXPECT_FALSE(player.is_playing());
    EXPECT_FALSE(player.is_complete());
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 0.0F);
    EXPECT_EQ(player.current_phase_index(), 0U);

    // Further ticks after stop are no-ops.
    player.tick(9999.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());
    EXPECT_FALSE(player.is_complete());
}

// ============================================================================
// T4: can_skip == false -- stop() is a no-op
// ============================================================================
TEST(CutscenePlayerTest, T4_CanSkipFalseStopIsNoOp)
{
    CutscenePlayer player;

    const Cutscene cs = make_single_phase(
        "cs_t4", 3000.0F,
        {{500.0F, EventKind::kSpawnEntity}},
        /*can_skip=*/false);

    player.play(cs);
    player.tick(200.0F);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 200.0F);
    EXPECT_TRUE(player.is_playing());

    // stop() should be ignored because can_skip == false.
    player.stop();
    EXPECT_TRUE(player.is_playing());
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 200.0F);

    // Cutscene continues to tick normally.
    player.tick(350.0F);
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kSpawnEntity);
    }
}

// ============================================================================
// T5: multiple-phase progression
// ============================================================================
TEST(CutscenePlayerTest, T5_MultiplePhasesProgressCorrectly)
{
    CutscenePlayer player;

    // Three phases, each with one event.
    Cutscene cs;
    cs.cutscene_id = "cs_t5";
    cs.can_skip    = true;

    auto make_phase = [](const std::string& id, float dur, float ev_off, EventKind kind)
    {
        CutscenePhase ph;
        ph.phase_id    = id;
        ph.duration_ms = dur;
        CutsceneEvent ev;
        ev.offset_ms   = ev_off;
        ev.kind        = kind;
        ph.events.push_back(ev);
        return ph;
    };

    cs.phases.push_back(make_phase("p0", 500.0F,  250.0F, EventKind::kFadeOut));
    cs.phases.push_back(make_phase("p1", 1000.0F, 750.0F, EventKind::kCharacterTalk));
    cs.phases.push_back(make_phase("p2", 300.0F,  100.0F, EventKind::kFadeIn));

    player.play(cs);
    EXPECT_EQ(player.current_phase_index(), 0U);

    // Tick into phase 0: fires kFadeOut at 250 ms.
    player.tick(260.0F);
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kFadeOut);
    }
    EXPECT_EQ(player.current_phase_index(), 0U);

    // Tick to end phase 0 and into phase 1 past 750 ms: fires kCharacterTalk.
    // We are at 260 ms in phase 0 (duration 500). Need 240 more to end phase 0,
    // then 760 to reach offset 760 in phase 1.
    player.tick(240.0F + 760.0F);  // crosses phase boundary and fires p1 event.
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kCharacterTalk);
    }
    EXPECT_EQ(player.current_phase_index(), 1U);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 760.0F);

    // Advance through the rest of phase 1 and into phase 2.
    // phase 1 remaining: 1000 - 760 = 240 ms; phase 2 event at 100 ms.
    player.tick(240.0F + 150.0F);
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kFadeIn);
    }
    EXPECT_EQ(player.current_phase_index(), 2U);

    // Finish phase 2.
    player.tick(200.0F);  // 150 + 200 = 350 > 300 ms phase duration.
    EXPECT_TRUE(player.is_complete());
    EXPECT_FALSE(player.is_playing());
}

// ============================================================================
// T6: is_complete only after natural end
// ============================================================================
TEST(CutscenePlayerTest, T6_IsCompleteOnlyAfterNaturalEnd)
{
    CutscenePlayer player;

    const Cutscene cs = make_single_phase("cs_t6", 200.0F, {});

    player.play(cs);
    EXPECT_FALSE(player.is_complete());

    player.tick(100.0F);
    EXPECT_FALSE(player.is_complete());
    EXPECT_TRUE(player.is_playing());

    player.tick(100.0F);  // exactly reaches end.
    EXPECT_TRUE(player.is_complete());
    EXPECT_FALSE(player.is_playing());
}

// ============================================================================
// T7: Events at offset 0 fire on the very first tick of their phase
// ============================================================================
TEST(CutscenePlayerTest, T7_EventAtOffsetZeroFiresOnFirstTick)
{
    CutscenePlayer player;

    const Cutscene cs = make_single_phase(
        "cs_t7", 1000.0F,
        {{0.0F, EventKind::kSetFlag}});

    player.play(cs);

    // Even a tiny tick should fire the offset-0 event.
    player.tick(0.001F);
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kSetFlag);
    }
}

// ============================================================================
// T8 (extra): Empty-phase cutscene completes immediately on play()
// ============================================================================
TEST(CutscenePlayerTest, T8_EmptyPhaseCutsceneCompletesImmediately)
{
    CutscenePlayer player;

    Cutscene cs;
    cs.cutscene_id = "cs_t8";
    cs.can_skip    = true;
    // No phases.

    player.play(cs);
    EXPECT_FALSE(player.is_playing());
    EXPECT_TRUE(player.is_complete());
}

// ============================================================================
// T9 (extra): Large dt spanning multiple phase boundaries fires all events
// ============================================================================
TEST(CutscenePlayerTest, T9_LargeDtSpansMultiplePhasesAndFiresAllEvents)
{
    CutscenePlayer player;

    Cutscene cs;
    cs.cutscene_id = "cs_t9";
    cs.can_skip    = true;

    {
        CutscenePhase p;
        p.phase_id    = "pa";
        p.duration_ms = 100.0F;
        CutsceneEvent ev;
        ev.offset_ms  = 50.0F;
        ev.kind       = EventKind::kDespawnEntity;
        p.events.push_back(ev);
        cs.phases.push_back(p);
    }
    {
        CutscenePhase p;
        p.phase_id    = "pb";
        p.duration_ms = 100.0F;
        CutsceneEvent ev;
        ev.offset_ms  = 50.0F;
        ev.kind       = EventKind::kSpawnEntity;
        p.events.push_back(ev);
        cs.phases.push_back(p);
    }

    player.play(cs);

    // Single large tick covers both phases entirely.
    player.tick(9999.0F);
    {
        const auto fired = player.events_fired_this_tick();
        // Both events should be in fired_this_tick_.
        ASSERT_EQ(fired.size(), 2U);
        EXPECT_EQ(fired[0].kind, EventKind::kDespawnEntity);
        EXPECT_EQ(fired[1].kind, EventKind::kSpawnEntity);
    }
    EXPECT_TRUE(player.is_complete());
}

// ============================================================================
// T10 (extra): Negative dt is clamped; playhead does not regress
// ============================================================================
TEST(CutscenePlayerTest, T10_NegativeDtClamped)
{
    CutscenePlayer player;

    const Cutscene cs = make_single_phase("cs_t10", 1000.0F, {});

    player.play(cs);
    player.tick(300.0F);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 300.0F);

    // Negative tick should not regress the playhead.
    player.tick(-999.0F);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 300.0F);
    EXPECT_TRUE(player.is_playing());
}

}  // namespace cd::game::cutscene_player::tests
