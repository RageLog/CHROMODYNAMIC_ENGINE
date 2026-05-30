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

add_subdirectory(hello_game)
add_subdirectory(hello_npc)
add_subdirectory(hello_world)
add_subdirectory(hello_narrative)
