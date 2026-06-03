# samples/game — Phase G gameplay-tier showcases.
#
# Console-only (headless) demos that exercise the cd::game::* and
# cd::gameplay::* library families end-to-end. No RHI / Vulkan
# dependency, so this sub-tree is always compiled regardless of
# CD_ENABLE_VULKAN — the gate lives one level up in samples/CMakeLists.txt.
#
# Members:
#   hello_game — Phase 479 / G1.5 closeout: TimeKeeper fixed-tick loop
#                + ActionMap "Jump" binding + Save slot roundtrip + Settings
#                persistence across runs. Ties every G1 library together so
#                a downstream consumer can copy-paste the pattern.
#   hello_npc  — Phase 480 / G2.4 closeout: NPC driven by cd::game::fsm
#                { Patrol, Chase, Attack } with a per-mode cd::game::ai_bt
#                BehaviorTree. cd::game::anim_graph (Phase C) is stubbed
#                with printf locomotion logs for the walk-vs-run blend.
#   hello_world — Phase 504 / G3.5 closeout: 3rd-person "player" walking
#                 a diagonal path through a sphere trigger volume, followed
#                 by a critically-damped Cinemachine-style vcam, with a
#                 "jump_dust" particle burst fired every 2 s. Driven by
#                 cd::gameplay::time::TimeKeeper at a fixed 60 Hz tick over
#                 10 simulated seconds. Stitches the four G3 libraries
#                 (cd::game::query / trigger / camera / particles_event)
#                 into one console-only smoke.
#   hello_narrative — Phase 505 / G4.4 closeout: branching dialogue +
#                QuestLog "Find 3 apples" in en/tr/ar with CLDR plurals
#                (cd::game::dialogue + cd::game::quest + cd::game::l10n).
#   hello_ai_director — Phase 634 / M9 W2: console proof of cd::game::ai_director;
#                3 EncounterTemplates, 20-tick simulation with player events,
#                TensionState transitions + request_next_encounter output.
#   hello_cutscene_player — Phase 636 / M9 W2: console proof that
#                cd::game::cutscene_player is consumable standalone.
#                3 phases x 1000 ms, 2 events/phase, 50 x 100 ms ticks.

#   hello_dialog_branch — Phase 652 / M10 W5A: BG3-style 7-node dialog graph
#                (cd::game::dialog_tree). 4 simulated play-throughs show kSay /
#                kChoice / kCondition / kEnd routing as 4 distinct stories.
#                Designer reads main.cpp once, knows how to author 200 nodes.

#   hello_input_replay — Phase 662 / M11 W5A: HL2-style input record/replay
#                (cd::game::input_recorder). 60-event record (60 Hz circle
#                sweep + 4 KeyDown) saved to tmp/, loaded back, replayed
#                frame-accurately at 16.6 ms steps. Stats confirm round-trip.
#                Moment: developer records 5s of player input to reproduce a
#                flaky physics bug and replays it 100x to root-cause.

add_subdirectory(hello_game)
add_subdirectory(hello_npc)
add_subdirectory(hello_world)
add_subdirectory(hello_narrative)
add_subdirectory(hello_ai_director)
add_subdirectory(hello_cutscene_player)
add_subdirectory(hello_dialog_branch)
add_subdirectory(hello_input_replay)
