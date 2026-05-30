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

add_subdirectory(hello_game)
add_subdirectory(hello_npc)
