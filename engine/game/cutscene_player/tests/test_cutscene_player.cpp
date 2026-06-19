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
//
// Phase 703 / M15 W4B — JSON round-trip tests:
//   T11. Round-trip simple cutscene: save_to_json + load_from_json preserves all fields.
//   T12. Malformed JSON file rejected: load_from_json returns nullopt.
//   T13. Deeply-nested phases preserved: 5-phase cutscene with events round-trips intact.
// =============================================================================
#include <cd/game/cutscene_player/CutscenePlayer.hpp>
#include <cd/game/cutscene_player/CutsceneJson.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
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

// ============================================================================
// T11 (Phase 703): Round-trip simple cutscene — save + load preserves fields
// ============================================================================
TEST(CutscenePlayerTest, T11_RoundTripSimpleCutscene)
{
    // Build a cutscene with two phases, each with events covering every field.
    Cutscene cs;
    cs.cutscene_id = "opening_cinematic";
    cs.can_skip    = false;

    {
        CutscenePhase ph;
        ph.phase_id    = "intro";
        ph.duration_ms = 2500.0F;

        CutsceneEvent ev0;
        ev0.offset_ms  = 100.0F;
        ev0.kind       = EventKind::kFadeIn;
        ev0.string_arg = "ease_in_cubic";
        ev0.vec3_arg   = { 0.25F, 0.5F, 0.75F };
        ph.events.push_back(ev0);

        CutsceneEvent ev1;
        ev1.offset_ms  = 800.0F;
        ev1.kind       = EventKind::kPlaySound;
        ev1.string_arg = "audio/voice/opening_line.wav";
        ev1.vec3_arg   = { 1.0F, 2.0F, 3.0F };
        ph.events.push_back(ev1);

        cs.phases.push_back(std::move(ph));
    }
    {
        CutscenePhase ph;
        ph.phase_id    = "outro";
        ph.duration_ms = 1000.0F;

        CutsceneEvent ev;
        ev.offset_ms  = 0.0F;
        ev.kind       = EventKind::kSetFlag;
        ev.string_arg = "level_complete";
        ev.vec3_arg   = { 1.0F, 0.0F, 0.0F };
        ph.events.push_back(ev);

        cs.phases.push_back(std::move(ph));
    }

    // Write to a temp file.
    const auto tmp_path = std::filesystem::temp_directory_path()
                        / "cd_test_cutscene_t11.json";

    const bool saved = save_to_json(cs, tmp_path);
    ASSERT_TRUE(saved) << "save_to_json failed for path: " << tmp_path;

    // Load back.
    const auto loaded = load_from_json(tmp_path);
    ASSERT_TRUE(loaded.has_value()) << "load_from_json returned nullopt";

    const Cutscene& rt = *loaded;

    EXPECT_EQ(rt.cutscene_id, cs.cutscene_id);
    EXPECT_EQ(rt.can_skip,    cs.can_skip);
    ASSERT_EQ(rt.phases.size(), cs.phases.size());

    for (std::size_t pi = 0; pi < cs.phases.size(); ++pi)
    {
        EXPECT_EQ(rt.phases[pi].phase_id,    cs.phases[pi].phase_id);
        EXPECT_FLOAT_EQ(rt.phases[pi].duration_ms, cs.phases[pi].duration_ms);
        ASSERT_EQ(rt.phases[pi].events.size(), cs.phases[pi].events.size());

        for (std::size_t ei = 0; ei < cs.phases[pi].events.size(); ++ei)
        {
            const auto& eo = cs.phases[pi].events[ei];
            const auto& er = rt.phases[pi].events[ei];
            EXPECT_FLOAT_EQ(er.offset_ms, eo.offset_ms);
            EXPECT_EQ(er.kind,       eo.kind);
            EXPECT_EQ(er.string_arg, eo.string_arg);
            EXPECT_FLOAT_EQ(er.vec3_arg[0], eo.vec3_arg[0]);
            EXPECT_FLOAT_EQ(er.vec3_arg[1], eo.vec3_arg[1]);
            EXPECT_FLOAT_EQ(er.vec3_arg[2], eo.vec3_arg[2]);
        }
    }

    // Cleanup.
    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
}

// ============================================================================
// T12 (Phase 703): Malformed JSON rejected — load_from_json returns nullopt
// ============================================================================
TEST(CutscenePlayerTest, T12_MalformedJsonRejected)
{
    const auto tmp_path = std::filesystem::temp_directory_path()
                        / "cd_test_cutscene_t12_malformed.json";

    // Write deliberately broken JSON (unclosed brace, no schema_version).
    {
        std::ofstream f(tmp_path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f << R"({ "cutscene_id": "bad" "missing_colon" true, "phases": [)";
        // intentionally NOT closed
    }

    const auto result = load_from_json(tmp_path);
    EXPECT_FALSE(result.has_value())
        << "load_from_json should return nullopt for malformed JSON";

    // Also test: valid JSON but missing schema_version.
    {
        std::ofstream f(tmp_path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f << R"({"cutscene_id":"no_schema","can_skip":true,"phases":[]})";
    }
    const auto result2 = load_from_json(tmp_path);
    EXPECT_FALSE(result2.has_value())
        << "load_from_json should return nullopt when schema_version is absent";

    // Also test: schema_version present but wrong value.
    {
        std::ofstream f(tmp_path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f << R"({"schema_version":99,"cutscene_id":"future","can_skip":true,"phases":[]})";
    }
    const auto result3 = load_from_json(tmp_path);
    EXPECT_FALSE(result3.has_value())
        << "load_from_json should return nullopt for schema_version != 1";

    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
}

// ============================================================================
// T13 (Phase 703): Deeply-nested phases preserved — 5-phase cutscene round-trips
// ============================================================================
TEST(CutscenePlayerTest, T13_DeeplyNestedPhasesPreserved)
{
    constexpr std::size_t kNumPhases     = 5U;
    constexpr std::size_t kEventsPerPhase = 4U;

    Cutscene cs;
    cs.cutscene_id = "multi_phase_stress";
    cs.can_skip    = true;

    for (std::size_t pi = 0; pi < kNumPhases; ++pi)
    {
        CutscenePhase ph;
        ph.phase_id    = "phase_" + std::to_string(pi);
        ph.duration_ms = static_cast<float>((pi + 1U) * 1000U);

        for (std::size_t ei = 0; ei < kEventsPerPhase; ++ei)
        {
            CutsceneEvent ev;
            ev.offset_ms  = static_cast<float>(ei) * (ph.duration_ms / static_cast<float>(kEventsPerPhase));
            // Cycle through all EventKind values (8 defined).
            ev.kind       = static_cast<EventKind>(static_cast<std::uint8_t>((pi * kEventsPerPhase + ei) % 8U));
            ev.string_arg = "arg_p" + std::to_string(pi) + "_e" + std::to_string(ei);
            ev.vec3_arg   = { static_cast<float>(pi), static_cast<float>(ei), 0.5F };
            ph.events.push_back(ev);
        }

        cs.phases.push_back(std::move(ph));
    }

    const auto tmp_path = std::filesystem::temp_directory_path()
                        / "cd_test_cutscene_t13_deep.json";

    ASSERT_TRUE(save_to_json(cs, tmp_path));

    const auto loaded = load_from_json(tmp_path);
    ASSERT_TRUE(loaded.has_value());

    const Cutscene& rt = *loaded;
    EXPECT_EQ(rt.cutscene_id, cs.cutscene_id);
    EXPECT_EQ(rt.can_skip,    cs.can_skip);
    ASSERT_EQ(rt.phases.size(), kNumPhases);

    for (std::size_t pi = 0; pi < kNumPhases; ++pi)
    {
        EXPECT_EQ(rt.phases[pi].phase_id, cs.phases[pi].phase_id);
        EXPECT_FLOAT_EQ(rt.phases[pi].duration_ms, cs.phases[pi].duration_ms);
        ASSERT_EQ(rt.phases[pi].events.size(), kEventsPerPhase);

        for (std::size_t ei = 0; ei < kEventsPerPhase; ++ei)
        {
            const auto& eo = cs.phases[pi].events[ei];
            const auto& er = rt.phases[pi].events[ei];
            EXPECT_FLOAT_EQ(er.offset_ms,   eo.offset_ms);
            EXPECT_EQ(er.kind,              eo.kind);
            EXPECT_EQ(er.string_arg,        eo.string_arg);
            EXPECT_FLOAT_EQ(er.vec3_arg[0], eo.vec3_arg[0]);
            EXPECT_FLOAT_EQ(er.vec3_arg[1], eo.vec3_arg[1]);
            EXPECT_FLOAT_EQ(er.vec3_arg[2], eo.vec3_arg[2]);
        }
    }

    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
}

// ============================================================================
// BAND-1 event-window boundary + dt-straddle fuzz (ADR-20260616 §2.6).
// ============================================================================

// T14: An event at offset == phase_duration fires exactly when the phase
//      ends (the half-open window [prev, phase_duration] includes the
//      upper bound at end-of-phase).
TEST(CutscenePlayerTest, T14_EventAtPhaseEndBoundaryFires)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase(
        "cs_t14", 500.0F,
        {{500.0F, EventKind::kFadeOut}});  // exactly at the boundary

    player.play(cs);
    // One tick that ends the phase precisely.
    player.tick(500.0F);
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0].kind, EventKind::kFadeOut);
    EXPECT_TRUE(player.is_complete());
}

// T15: An event one epsilon below the boundary fires in the tick that
//      crosses it; an event past the boundary (clamped out) never fires.
TEST(CutscenePlayerTest, T15_NearBoundaryAndPastBoundaryEvents)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase(
        "cs_t15", 1000.0F,
        {{999.0F, EventKind::kPlaySound},   // just below end -> fires
         {1500.0F, EventKind::kCameraMove}}); // beyond duration -> never fires

    player.play(cs);
    player.tick(1000.0F);  // consume the whole phase in one tick
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0].kind, EventKind::kPlaySound);
    EXPECT_TRUE(player.is_complete());
}

// T16: A negative-offset event is silently skipped (lower bound is >= 0
//      at phase start).
TEST(CutscenePlayerTest, T16_NegativeOffsetEventSkipped)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase(
        "cs_t16", 500.0F,
        {{-10.0F, EventKind::kSetFlag},     // negative -> skipped
         {100.0F, EventKind::kFadeIn}});    // valid -> fires

    player.play(cs);
    player.tick(200.0F);
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0].kind, EventKind::kFadeIn);
}

// T17: In-phase event ordering follows authoring order when multiple events
//      fall in the same tick window (matches documented contract).
TEST(CutscenePlayerTest, T17_EventOrderWithinWindowIsAuthoringOrder)
{
    CutscenePlayer player;
    Cutscene cs;
    cs.cutscene_id = "cs_t17";
    cs.can_skip    = true;
    CutscenePhase ph;
    ph.phase_id    = "p0";
    ph.duration_ms = 1000.0F;
    // Authoring order deliberately NOT ascending by offset.
    auto add = [&](float off, EventKind k) {
        CutsceneEvent e; e.offset_ms = off; e.kind = k; ph.events.push_back(e);
    };
    add(300.0F, EventKind::kCameraMove);   // authored 1st
    add(100.0F, EventKind::kPlaySound);    // authored 2nd
    add(200.0F, EventKind::kFadeIn);       // authored 3rd
    cs.phases.push_back(std::move(ph));

    player.play(cs);
    player.tick(400.0F);  // window (0,400] captures all three at once
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 3U);
    // Order == authoring order, NOT offset order.
    EXPECT_EQ(fired[0].kind, EventKind::kCameraMove);
    EXPECT_EQ(fired[1].kind, EventKind::kPlaySound);
    EXPECT_EQ(fired[2].kind, EventKind::kFadeIn);
}

// T18: dt-straddle fuzz — feeding the SAME total time as one big tick vs.
//      many small random ticks must fire the EXACT same set of events
//      (each exactly once, no double-fire, no drop across the boundary).
TEST(CutscenePlayerTest, T18_DtStraddleFuzzManySmallEqualsOneBig)
{
    // Three phases, several events spread across boundaries.
    auto build = [] {
        Cutscene cs;
        cs.cutscene_id = "cs_t18";
        cs.can_skip    = true;
        auto phase = [](const std::string& id, float dur,
                        std::vector<std::pair<float, EventKind>> evs) {
            CutscenePhase p;
            p.phase_id = id; p.duration_ms = dur;
            for (auto& [o, k] : evs) {
                CutsceneEvent e; e.offset_ms = o; e.kind = k; p.events.push_back(e);
            }
            return p;
        };
        cs.phases.push_back(phase("a", 100.0F,
            {{0.0F, EventKind::kFadeIn}, {50.0F, EventKind::kPlaySound},
             {100.0F, EventKind::kCameraMove}}));
        cs.phases.push_back(phase("b", 250.0F,
            {{1.0F, EventKind::kCharacterTalk}, {249.0F, EventKind::kSpawnEntity}}));
        cs.phases.push_back(phase("c", 80.0F,
            {{40.0F, EventKind::kDespawnEntity}, {80.0F, EventKind::kFadeOut}}));
        return cs;
    };

    const float total = 100.0F + 250.0F + 80.0F;  // sum of all durations

    // Reference run: one big tick.
    std::vector<EventKind> big;
    {
        CutscenePlayer player;
        player.play(build());
        player.tick(total + 1.0F);  // a hair past the end
        for (const auto& e : player.events_fired_this_tick())
            big.push_back(e.kind);
        EXPECT_TRUE(player.is_complete());
    }

    // Fuzz: drive with many small random ticks summing to >= total. Collect
    // every fired event across all ticks.
    std::mt19937 rng(0x5EED1U);
    for (int trial = 0; trial < 24; ++trial)
    {
        CutscenePlayer player;
        player.play(build());
        std::vector<EventKind> small;
        float elapsed = 0.0F;
        // Step until complete; cap iterations defensively.
        for (int step = 0; step < 100000 && !player.is_complete(); ++step)
        {
            const float dt = 0.5F + static_cast<float>(rng() % 70U);  // [0.5,69.5]
            elapsed += dt;
            player.tick(dt);
            for (const auto& e : player.events_fired_this_tick())
                small.push_back(e.kind);
        }
        EXPECT_TRUE(player.is_complete()) << "trial " << trial;
        // Same multiset AND same overall order as the one-big-tick run.
        ASSERT_EQ(small.size(), big.size()) << "trial " << trial
            << " elapsed=" << elapsed;
        for (std::size_t i = 0; i < big.size(); ++i)
            EXPECT_EQ(small[i], big[i]) << "trial " << trial << " idx " << i;
    }
}

// T19: A single tick that straddles a phase boundary fires the trailing
//      event of phase N and the leading event of phase N+1 in one tick,
//      in phase order, each exactly once.
TEST(CutscenePlayerTest, T19_SingleTickStraddlesBoundaryFiresBothPhases)
{
    CutscenePlayer player;
    Cutscene cs;
    cs.cutscene_id = "cs_t19";
    cs.can_skip    = true;
    auto phase = [](const std::string& id, float dur, float off, EventKind k) {
        CutscenePhase p; p.phase_id = id; p.duration_ms = dur;
        CutsceneEvent e; e.offset_ms = off; e.kind = k; p.events.push_back(e);
        return p;
    };
    cs.phases.push_back(phase("p0", 100.0F, 90.0F, EventKind::kFadeOut));  // trailing
    cs.phases.push_back(phase("p1", 100.0F, 10.0F, EventKind::kFadeIn));   // leading
    player.play(cs);

    // Park at 80 ms in p0, then one tick of 40 ms -> crosses 90 (p0) AND
    // 100 (boundary) into p1 reaching offset 20 (>10 in p1).
    player.tick(80.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());
    player.tick(40.0F);
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 2U);
    EXPECT_EQ(fired[0].kind, EventKind::kFadeOut);  // phase 0 trailing first
    EXPECT_EQ(fired[1].kind, EventKind::kFadeIn);   // phase 1 leading second
    EXPECT_EQ(player.current_phase_index(), 1U);
}

// ============================================================================
// T20: seek() repositions the playhead silently (no events fired).
// ============================================================================
TEST(CutscenePlayerTest, T20_SeekRepositionsPlayheadNoEventsFired)
{
    // Two phases: p0 = 500 ms (event at 200), p1 = 800 ms (event at 400).
    Cutscene cs;
    cs.cutscene_id = "cs_t20";
    cs.can_skip    = true;
    {
        CutscenePhase ph;
        ph.phase_id    = "p0";
        ph.duration_ms = 500.0F;
        CutsceneEvent ev; ev.offset_ms = 200.0F; ev.kind = EventKind::kFadeIn;
        ph.events.push_back(ev);
        cs.phases.push_back(ph);
    }
    {
        CutscenePhase ph;
        ph.phase_id    = "p1";
        ph.duration_ms = 800.0F;
        CutsceneEvent ev; ev.offset_ms = 400.0F; ev.kind = EventKind::kFadeOut;
        ph.events.push_back(ev);
        cs.phases.push_back(ph);
    }

    CutscenePlayer player;
    player.play(cs);

    // total = 1300 ms. Seek into p1 at abs 700 ms (p1 offset = 700-500 = 200).
    player.seek(700.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty()) << "seek must not fire events";
    EXPECT_EQ(player.current_phase_index(), 1U);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 200.0F);
    EXPECT_TRUE(player.is_playing());

    // Next tick crosses the p1 event at offset 400: fires kFadeOut.
    player.tick(250.0F);  // 200 + 250 = 450 > 400
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kFadeOut);
    }
}

// ============================================================================
// T21: seek() to 0 repositions to the very start of phase 0.
// ============================================================================
TEST(CutscenePlayerTest, T21_SeekToZeroReposToStart)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t21", 1000.0F,
        {{300.0F, EventKind::kPlaySound}});

    player.play(cs);
    player.tick(500.0F);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 500.0F);

    player.seek(0.0F);
    EXPECT_EQ(player.current_phase_index(), 0U);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 0.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());

    // The event at 300 ms must still fire after seeking back to 0.
    player.tick(310.0F);
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0].kind, EventKind::kPlaySound);
}

// ============================================================================
// T22: seek() past total_duration_ms() completes the cutscene immediately.
// ============================================================================
TEST(CutscenePlayerTest, T22_SeekPastEndCompletesImmediately)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t22", 1000.0F, {});

    player.play(cs);
    EXPECT_FLOAT_EQ(player.total_duration_ms(), 1000.0F);

    player.seek(99999.0F);
    EXPECT_FALSE(player.is_playing());
    EXPECT_TRUE(player.is_complete());
    EXPECT_TRUE(player.events_fired_this_tick().empty());
}

// ============================================================================
// T23: seek() with negative value is clamped to 0.
// ============================================================================
TEST(CutscenePlayerTest, T23_SeekNegativeClampsToZero)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t23", 500.0F, {});

    player.play(cs);
    player.tick(200.0F);
    player.seek(-100.0F);

    EXPECT_EQ(player.current_phase_index(), 0U);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 0.0F);
    EXPECT_TRUE(player.is_playing());
}

// ============================================================================
// T24: seek() is a no-op when the player is kIdle.
// ============================================================================
TEST(CutscenePlayerTest, T24_SeekNoOpWhenIdle)
{
    CutscenePlayer player;  // never played — kIdle
    player.seek(500.0F);
    EXPECT_FALSE(player.is_playing());
    EXPECT_FALSE(player.is_complete());
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 0.0F);
}

// ============================================================================
// T25: restart() replays from the beginning after natural completion.
// ============================================================================
TEST(CutscenePlayerTest, T25_RestartAfterCompletion)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t25", 100.0F,
        {{50.0F, EventKind::kSetFlag}});

    player.play(cs);
    player.tick(200.0F);  // finish
    EXPECT_TRUE(player.is_complete());

    // Restart: must clear complete and begin again.
    player.restart();
    EXPECT_TRUE(player.is_playing());
    EXPECT_FALSE(player.is_complete());
    EXPECT_EQ(player.current_phase_index(), 0U);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 0.0F);

    // Event fires again on the second playthrough.
    player.tick(60.0F);
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0].kind, EventKind::kSetFlag);
}

// ============================================================================
// T26: restart() is a no-op when the player has never been loaded.
// ============================================================================
TEST(CutscenePlayerTest, T26_RestartNoOpWhenNeverLoaded)
{
    CutscenePlayer player;
    player.restart();
    EXPECT_FALSE(player.is_playing());
    EXPECT_FALSE(player.is_complete());
}

// ============================================================================
// T27: total_duration_ms() sums all phase durations correctly.
// ============================================================================
TEST(CutscenePlayerTest, T27_TotalDurationMsSumsAllPhases)
{
    Cutscene cs;
    cs.cutscene_id = "cs_t27";
    cs.can_skip    = true;
    auto add_phase = [&](float dur) {
        CutscenePhase ph; ph.duration_ms = dur; cs.phases.push_back(ph);
    };
    add_phase(300.0F);
    add_phase(700.0F);
    add_phase(1000.0F);

    CutscenePlayer player;
    player.play(cs);
    EXPECT_FLOAT_EQ(player.total_duration_ms(), 2000.0F);
}

// ============================================================================
// T28: total_duration_ms() returns 0 when kIdle (never played).
// ============================================================================
TEST(CutscenePlayerTest, T28_TotalDurationMsZeroWhenIdle)
{
    CutscenePlayer player;
    EXPECT_FLOAT_EQ(player.total_duration_ms(), 0.0F);
}

// ============================================================================
// T29: Zero-duration phase clamped to 1 ms; event at offset 0 still fires.
// ============================================================================
TEST(CutscenePlayerTest, T29_ZeroDurationPhaseClamped)
{
    Cutscene cs;
    cs.cutscene_id = "cs_t29";
    cs.can_skip    = true;
    CutscenePhase ph;
    ph.phase_id    = "zero";
    ph.duration_ms = 0.0F;  // will be clamped to 1 ms by play()
    CutsceneEvent ev;
    ev.offset_ms   = 0.0F;
    ev.kind        = EventKind::kSetFlag;
    ph.events.push_back(ev);
    cs.phases.push_back(ph);

    CutscenePlayer player;
    player.play(cs);
    EXPECT_TRUE(player.is_playing());
    // A single tick of 1 ms (the clamped minimum) fires the offset-0 event
    // and completes the cutscene.
    player.tick(1.0F);
    {
        const auto fired = player.events_fired_this_tick();
        ASSERT_EQ(fired.size(), 1U);
        EXPECT_EQ(fired[0].kind, EventKind::kSetFlag);
    }
    EXPECT_TRUE(player.is_complete());
}

// ============================================================================
// T30: events_fired_this_tick() is empty immediately after stop().
// ============================================================================
TEST(CutscenePlayerTest, T30_EventsFiredEmptyAfterStop)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t30", 500.0F,
        {{100.0F, EventKind::kFadeIn}});

    player.play(cs);
    player.tick(150.0F);  // fires kFadeIn
    ASSERT_EQ(player.events_fired_this_tick().size(), 1U);

    player.stop();
    EXPECT_TRUE(player.events_fired_this_tick().empty());
}

// ============================================================================
// T31: pause() is idempotent — calling twice from kPaused is harmless.
// ============================================================================
TEST(CutscenePlayerTest, T31_PauseIdempotent)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t31", 1000.0F, {});

    player.play(cs);
    player.pause();
    player.pause();  // second call must be a no-op
    EXPECT_FALSE(player.is_playing());
    player.resume();
    EXPECT_TRUE(player.is_playing());
}

// ============================================================================
// T32: resume() is idempotent — calling twice from kPlaying is harmless.
// ============================================================================
TEST(CutscenePlayerTest, T32_ResumeIdempotent)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t32", 1000.0F, {});

    player.play(cs);
    player.resume();  // already playing — no-op
    player.resume();  // still playing — no-op
    EXPECT_TRUE(player.is_playing());
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 0.0F);
}

// ============================================================================
// T33: play() on an already-playing cutscene force-resets regardless of
//      can_skip, and clears is_complete().
// ============================================================================
TEST(CutscenePlayerTest, T33_PlayWhilePlayingForceResets)
{
    CutscenePlayer player;
    const Cutscene cs_a = make_single_phase("cs_a", 1000.0F, {}, /*can_skip=*/false);
    const Cutscene cs_b = make_single_phase("cs_b", 500.0F,
        {{200.0F, EventKind::kCameraMove}});

    player.play(cs_a);
    player.tick(600.0F);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 600.0F);

    // Force a new cutscene in, even though can_skip == false on the active one.
    player.play(cs_b);
    EXPECT_TRUE(player.is_playing());
    EXPECT_FALSE(player.is_complete());
    EXPECT_EQ(player.current_phase_index(), 0U);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 0.0F);

    // cs_b event fires correctly.
    player.tick(210.0F);
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0].kind, EventKind::kCameraMove);
}

// ============================================================================
// T34: JSON — missing cutscene_id returns nullopt.
// ============================================================================
TEST(CutscenePlayerTest, T34_JsonMissingCutsceneIdReturnsNullopt)
{
    const auto tmp_path = std::filesystem::temp_directory_path()
                        / "cd_test_cutscene_t34.json";
    {
        std::ofstream f(tmp_path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        // Valid JSON, valid schema_version, but no cutscene_id.
        f << R"({"schema_version":1,"can_skip":true,"phases":[]})";
    }
    const auto result = load_from_json(tmp_path);
    EXPECT_FALSE(result.has_value())
        << "load_from_json should return nullopt when cutscene_id is absent";

    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
}

// ============================================================================
// T35: JSON — file not found returns nullopt.
// ============================================================================
TEST(CutscenePlayerTest, T35_JsonFileNotFoundReturnsNullopt)
{
    const auto bad_path = std::filesystem::temp_directory_path()
                        / "cd_test_cutscene_nonexistent_xyz987.json";
    // Ensure it really doesn't exist.
    std::error_code ec;
    std::filesystem::remove(bad_path, ec);

    const auto result = load_from_json(bad_path);
    EXPECT_FALSE(result.has_value())
        << "load_from_json should return nullopt for a non-existent file";
}

// ============================================================================
// T36: JSON — root is a JSON array (not object) returns nullopt.
// ============================================================================
TEST(CutscenePlayerTest, T36_JsonRootIsArrayReturnsNullopt)
{
    const auto tmp_path = std::filesystem::temp_directory_path()
                        / "cd_test_cutscene_t36.json";
    {
        std::ofstream f(tmp_path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f << R"([1, 2, 3])";
    }
    const auto result = load_from_json(tmp_path);
    EXPECT_FALSE(result.has_value())
        << "load_from_json should return nullopt when root is a JSON array";

    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
}

// ============================================================================
// T37: JSON — can_skip absent defaults to true.
// ============================================================================
TEST(CutscenePlayerTest, T37_JsonCanSkipAbsentDefaultsToTrue)
{
    const auto tmp_path = std::filesystem::temp_directory_path()
                        / "cd_test_cutscene_t37.json";
    {
        std::ofstream f(tmp_path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        // No "can_skip" key — must default to true.
        f << R"({"schema_version":1,"cutscene_id":"default_skip","phases":[]})";
    }
    const auto result = load_from_json(tmp_path);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->can_skip);
    EXPECT_EQ(result->cutscene_id, "default_skip");

    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
}

// ============================================================================
// T38: seek() while paused repositions correctly; resume() + tick() then fires
//      only events beyond the new position.
// ============================================================================
TEST(CutscenePlayerTest, T38_SeekWhilePausedThenResume)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t38", 1000.0F,
        {{100.0F, EventKind::kFadeIn},
         {600.0F, EventKind::kFadeOut}});

    player.play(cs);
    player.pause();
    // Seek past the 100 ms event into the middle of the phase.
    player.seek(400.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());
    EXPECT_EQ(player.current_phase_index(), 0U);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 400.0F);

    player.resume();
    // Tick to cross 600 ms event; kFadeIn at 100 ms must NOT fire (already past).
    player.tick(250.0F);  // 400 + 250 = 650 > 600
    const auto fired = player.events_fired_this_tick();
    ASSERT_EQ(fired.size(), 1U);
    EXPECT_EQ(fired[0].kind, EventKind::kFadeOut);
}

// ============================================================================
// T39: restart() mid-playthrough resets and re-fires events from the start.
// ============================================================================
TEST(CutscenePlayerTest, T39_RestartMidPlaythrough)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t39", 500.0F,
        {{0.0F,   EventKind::kFadeIn},
         {300.0F, EventKind::kSetFlag}});

    player.play(cs);
    player.tick(350.0F);  // fires both events
    ASSERT_EQ(player.events_fired_this_tick().size(), 2U);

    player.restart();
    EXPECT_TRUE(player.is_playing());
    EXPECT_EQ(player.current_phase_index(), 0U);
    EXPECT_FLOAT_EQ(player.current_offset_ms(), 0.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());

    // Both events fire again on the fresh play.
    player.tick(350.0F);
    ASSERT_EQ(player.events_fired_this_tick().size(), 2U);
}

// ============================================================================
// T40: events_fired_this_tick() is cleared by seek().
// ============================================================================
TEST(CutscenePlayerTest, T40_SeekClearsEventBuffer)
{
    CutscenePlayer player;
    const Cutscene cs = make_single_phase("cs_t40", 1000.0F,
        {{200.0F, EventKind::kCameraMove}});

    player.play(cs);
    player.tick(250.0F);  // fires kCameraMove
    ASSERT_EQ(player.events_fired_this_tick().size(), 1U);

    // seek() must clear the buffer.
    player.seek(300.0F);
    EXPECT_TRUE(player.events_fired_this_tick().empty());
}

}  // namespace cd::game::cutscene_player::tests
