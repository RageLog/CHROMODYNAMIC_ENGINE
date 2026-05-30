# engine/game/ai_bt — cd::game::ai_bt (Phase 473)
#
# G2.2 — Behavior Tree library. Composite (Sequence / Selector / Parallel),
# Decorator (Inverter / Repeater / UntilSuccess), and Leaf (lambda-wrapping)
# node families plus a `Blackboard` key-value scratch store.
#
# Reference: Champandard 2008, "Behavior Trees for Next-Gen Game AI" (AIGameDev
# / GDC). Marzinotto et al. 2014 ICRA on the unified BT framework provides the
# formal kRunning / kSuccess / kFailure return-type contract.
#
# Dependencies (CLAUDE.md §7): cd::core only — the BT machinery is pure stdlib
# and stays game-side; tying it to ECS / scene / physics would lock callers
# into specific representations. Game code adapts entity payloads into the
# `Blackboard` at the call site.

cd_add_library(game_ai_bt
  TYPE STATIC
  SOURCES
    src/BehaviorTree.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
