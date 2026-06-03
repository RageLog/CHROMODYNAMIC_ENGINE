// =============================================================================
// CHROMODYNAMIC — samples/game/hello_cutscene_player/main.cpp
//
// Phase 636 (M9 W2 closeout).
//
// Console-only proof that cd::game::cutscene_player is consumable as a
// standalone library.
//
// Demo flow:
//   1. Build a Cutscene with 3 phases, each 1000 ms, 2 events per phase
//      (event offsets at 200 ms and 700 ms within each phase).
//   2. Call player.play(cutscene).
//   3. Tick 50 times at 100 ms per tick (= 5 000 ms total, covers all 3
//      phases of 3 000 ms and leaves the player idle).
//   4. For each tick print the events_fired_this_tick() list.
//   5. Assert is_complete() == true and exit 0.
//
// Headless — no RHI, no platform window.
// =============================================================================
#include <cd/game/cutscene_player/CutscenePlayer.hpp>

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace
{

namespace cp = cd::game::cutscene_player;

// ---------------------------------------------------------------------------
// event_kind_name — human-readable label for console output.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string_view event_kind_name(const cp::EventKind kind) noexcept
{
    switch (kind)
    {
        case cp::EventKind::kCameraMove:    return "CameraMove";
        case cp::EventKind::kCharacterTalk: return "CharacterTalk";
        case cp::EventKind::kPlaySound:     return "PlaySound";
        case cp::EventKind::kFadeIn:        return "FadeIn";
        case cp::EventKind::kFadeOut:       return "FadeOut";
        case cp::EventKind::kSpawnEntity:   return "SpawnEntity";
        case cp::EventKind::kDespawnEntity: return "DespawnEntity";
        case cp::EventKind::kSetFlag:       return "SetFlag";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// make_demo_cutscene — 3 phases x 1000 ms, 2 events per phase.
//
// Phase 0 "intro"       : FadeIn @200 ms, CameraMove @700 ms
// Phase 1 "boss_arrives": CharacterTalk @200 ms, PlaySound @700 ms
// Phase 2 "outro"       : FadeOut @200 ms, SetFlag @700 ms
// ---------------------------------------------------------------------------
[[nodiscard]] cp::Cutscene make_demo_cutscene()
{
    cp::Cutscene cs;
    cs.cutscene_id = "hello_cutscene";
    cs.can_skip    = true;

    // Phase 0 — intro
    {
        cp::CutscenePhase ph;
        ph.phase_id    = "intro";
        ph.duration_ms = 1000.0F;

        cp::CutsceneEvent ev0;
        ev0.offset_ms  = 200.0F;
        ev0.kind       = cp::EventKind::kFadeIn;
        ev0.string_arg = "intro_fade";

        cp::CutsceneEvent ev1;
        ev1.offset_ms  = 700.0F;
        ev1.kind       = cp::EventKind::kCameraMove;
        ev1.string_arg = "linear";
        ev1.vec3_arg   = { 0.0F, 5.0F, -10.0F };

        ph.events.push_back(ev0);
        ph.events.push_back(ev1);
        cs.phases.push_back(ph);
    }

    // Phase 1 — boss_arrives
    {
        cp::CutscenePhase ph;
        ph.phase_id    = "boss_arrives";
        ph.duration_ms = 1000.0F;

        cp::CutsceneEvent ev0;
        ev0.offset_ms  = 200.0F;
        ev0.kind       = cp::EventKind::kCharacterTalk;
        ev0.string_arg = "boss_entrance_line";

        cp::CutsceneEvent ev1;
        ev1.offset_ms  = 700.0F;
        ev1.kind       = cp::EventKind::kPlaySound;
        ev1.string_arg = "audio/boss_roar.wav";
        ev1.vec3_arg   = { 0.0F, 0.0F, 0.0F };

        ph.events.push_back(ev0);
        ph.events.push_back(ev1);
        cs.phases.push_back(ph);
    }

    // Phase 2 — outro
    {
        cp::CutscenePhase ph;
        ph.phase_id    = "outro";
        ph.duration_ms = 1000.0F;

        cp::CutsceneEvent ev0;
        ev0.offset_ms  = 200.0F;
        ev0.kind       = cp::EventKind::kFadeOut;
        ev0.string_arg = "outro_fade";

        cp::CutsceneEvent ev1;
        ev1.offset_ms  = 700.0F;
        ev1.kind       = cp::EventKind::kSetFlag;
        ev1.string_arg = "boss_cutscene_seen";
        ev1.vec3_arg   = { 1.0F, 0.0F, 0.0F };  // vec3_arg[0] = flag value 1

        ph.events.push_back(ev0);
        ph.events.push_back(ev1);
        cs.phases.push_back(ph);
    }

    return cs;
}

}  // namespace

// =============================================================================
// main
// =============================================================================
int main()
{
    std::puts("=== CHROMODYNAMIC hello_cutscene_player (Phase 636) ===");

    // -----------------------------------------------------------------------
    // 1. Build and start the cutscene.
    // -----------------------------------------------------------------------
    const cp::Cutscene demo = make_demo_cutscene();

    cp::CutscenePlayer player;
    player.play(demo);

    std::printf("Cutscene '%s' started: %zu phase(s).\n",
                demo.cutscene_id.c_str(),
                demo.phases.size());

    // -----------------------------------------------------------------------
    // 2. Tick 50 x 100 ms — covers 5 000 ms (all 3 000 ms of cutscene + tail).
    // -----------------------------------------------------------------------
    constexpr int   kTickCount = 50;
    constexpr float kDtMs      = 100.0F;

    for (int tick = 0; tick < kTickCount; ++tick)
    {
        player.tick(kDtMs);

        const auto fired = player.events_fired_this_tick();
        if (!fired.empty())
        {
            std::printf("[tick %2d | t=%.0f ms] %zu event(s) fired:\n",
                        tick,
                        static_cast<double>((static_cast<float>(tick) + 1.0F) * kDtMs),
                        fired.size());
            for (const auto& ev : fired)
            {
                std::printf("  -> %-14s  offset=%.0f ms  arg=\"%s\"\n",
                            event_kind_name(ev.kind).data(),
                            static_cast<double>(ev.offset_ms),
                            ev.string_arg.c_str());
            }
        }
        else
        {
            std::printf("[tick %2d | t=%.0f ms] (no events)\n",
                        tick,
                        static_cast<double>((static_cast<float>(tick) + 1.0F) * kDtMs));
        }
    }

    // -----------------------------------------------------------------------
    // 3. Assert completion.
    // -----------------------------------------------------------------------
    if (!player.is_complete())
    {
        std::fputs("ERROR: player did not reach is_complete() == true.\n", stderr);
        return EXIT_FAILURE;
    }

    std::puts("=== hello_cutscene_player OK — is_complete() == true ===");
    return EXIT_SUCCESS;
}
