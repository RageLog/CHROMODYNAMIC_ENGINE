# engine/game/anim_graph - cd::game_anim_graph  (Phase 481 / G2.3)
#
# Animation graph: state-machine + 1D/2D blend nodes + clip leaves, all
# composable through a single AnimNode polymorphic base.
#
# Reference: Unity Animator + 1D / 2D Blend Tree, production AnimGraph (Persson,
# GDC 2010), Bevy bevy_animation_graph crate.
#
# Dependencies (CLAUDE.md S7): cd::core + cd::math + cd::anim — the graph
# sits strictly above cd::anim (which it composes) and strictly below the
# scene / sample tiers. No reverse edges.

cd_add_library(game_anim_graph
  TYPE STATIC
  SOURCES
    src/AnimGraph.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::anim
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
