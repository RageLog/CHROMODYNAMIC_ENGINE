# samples/game/hello_npc - Phase 480 (G2.4 - closes Phase G2).
#
# End-to-end demo of the Wave-1 gameplay AI stack: an NPC driven by a
# StateMachine over { Patrol, Chase, Attack } with a per-mode BehaviorTree
# (walk-the-waypoints in Patrol, run-to-player in Chase, swing+cooldown in
# Attack). A printf stand-in stubs the missing cd::game::anim_graph
# (Phase C) so the walk-vs-run blend signal still tells a complete story.
#
# Dependencies (CLAUDE.md S7): core + the two Wave-1 game libraries.
cd_add_sample(hello_npc
  SOURCES main.cpp
  DEPS    cd::core cd::game_fsm cd::game_ai_bt
)
