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
#include <filesystem>
#include <fstream>
#include <optional>
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

}  // namespace cd::game::cutscene_player::tests
