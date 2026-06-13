# samples/game — Phase G gameplay-tier showcases.
#
# phase914-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], all 7 headless
# gameplay-tier "console proof" samples have been folded into their source
# libraries' gtests. hello_world (Vulkan visual variant of Phase 504
# G3 closeout) is the only retained sample.
#
# Retired in phase 914:
#   * hello_game           -> covered by engine/world/gameplay_time/tests/test_time.cpp
#                            + engine/game/save/tests/test_save.cpp
#                            + engine/game/settings/tests/test_settings.cpp
#                            + engine/world/gameplay_input_binding/tests/test_input_binding.cpp.
#   * hello_npc            -> engine/game/fsm/tests/test_fsm.cpp (Patrol/Chase/Attack
#                            transitions) + engine/game/ai_bt/tests/test_ai_bt.cpp.
#   * hello_narrative      -> engine/game/dialogue/tests/test_dialogue.cpp (14)
#                            + engine/game/quest/tests/test_quest.cpp (11)
#                            + engine/game/l10n/tests/test_l10n.cpp.
#   * hello_ai_director    -> engine/game/ai_director/tests/test_ai_director.cpp (17).
#   * hello_cutscene_player -> engine/game/cutscene_player/tests/test_cutscene_player.cpp (13).
#   * hello_dialog_branch  -> engine/game/dialog_tree/tests/test_dialog_tree.cpp (8).
#   * hello_input_replay   -> engine/game/input_recorder/tests/test_input_recorder.cpp (10).
#
# Retired in phase1173 (samples-consolidation, hello_world fold):
#   * hello_world — Phase 504 / G3.5 closeout VISUAL variant. The four
#     gameplay libraries (query / trigger / camera / particles_event) it
#     exercised are now driven by a CPU-only "Gameplay Stack Demo" probe in
#     hello_engine (samples/engine/hello_engine/HelloGameplayDemo.hpp), per
#     [feedback_sample_consolidation_and_per_feature_demo]. The ~600 lines of
#     platform-window / Vulkan-swapchain / Renderer boilerplate hello_world
#     carried are already provided by hello_engine, so the binary was retired
#     rather than ported byte-for-byte.
#
# samples/game/ now hosts no standalone binaries; the directory is kept for
# future gameplay-tier showcases and so the parent add_subdirectory stays
# valid.
