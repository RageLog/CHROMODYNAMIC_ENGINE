# engine/world/scene — cd::scene (Phase 4 / Sprint S4.2)
cd_add_library(scene
  TYPE STATIC
  SOURCES
    src/Scene.cpp
  PUBLIC_DEPS
    cd::ecs
    cd::math
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
