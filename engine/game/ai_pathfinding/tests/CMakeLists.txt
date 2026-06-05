cd_add_test(game_ai_pathfinding
  SOURCES test_pathfinding.cpp
  DEPS    cd::game_ai_pathfinding cd::core
)

cd_add_test(game_ai_pathfinding_triangulator
  SOURCES test_triangulator.cpp
  DEPS    cd::game_ai_pathfinding cd::core
)
