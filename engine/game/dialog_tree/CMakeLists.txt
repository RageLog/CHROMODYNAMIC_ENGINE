# engine/game/dialog_tree - cd::game_dialog_tree (Phase 645)
#
# BG3-style branching dialog graph runtime. A designer authors a DialogTree
# (POD: tree_id + root_id + vector<DialogNode>) and the DialogTreeRuntime
# walks it with player choices and named boolean condition variables — without
# recompiling.
#
# Sibling to cd::game::dialogue (linear chain VM, Phase 484 / G4.1):
#   dialogue      = Ink/Yarn linear chain + predicate-gated choices + DSL
#   dialog_tree   = BG3 graph with NodeKind routing (Say/Choice/Condition/End)
#                   + transparent kCondition chaining + bool condition store
#
# Dependencies (CLAUDE.md §7): cd::core only. No allocator, no math, no I/O.
# The runtime is a pure state machine.

cd_add_library(game_dialog_tree
  TYPE STATIC
  SOURCES
    src/DialogTree.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
