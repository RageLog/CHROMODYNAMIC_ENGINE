# engine/game/world2d — cd::world2d
# FAZ 35 (ADR-20261111) — Unified Commercial 2D World, Physics2D + CharacterController2D,
# Sandboxed Lua 5.4 Entity Behaviors, Symmetric Human/AI CommandBus2D & PlaytestRunner2D

cd_add_library(world2d
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # links cd::script (vendored Lua via FetchContent)
  SOURCES
    src/Stage2D.cpp
    src/CommandBus2D.cpp
    src/PlaytestRunner2D.cpp
    src/ParticleSystem2D.cpp
    src/SpatialHash2D.cpp
    src/AudioCue2D.cpp
    src/GenreTemplates2D.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::schema
    cd::render2d
    cd::physics
    cd::script
    cd::asset_json
    cd::asset_pak
    cd::audio
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
