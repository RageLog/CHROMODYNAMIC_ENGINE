# engine/world/physics — cd::physics (Phase 4 / Sprint S4.3)
cd_add_library(physics
  TYPE STATIC
  SOURCES
    src/BuiltinPhysicsWorld.cpp
  PUBLIC_DEPS
    cd::math
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
