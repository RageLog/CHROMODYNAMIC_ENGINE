# engine/game/camera - cd::game_camera  (Phase 482 / G3.3)
#
# Cinemachine-style virtual camera stack + brain.  Sits in the cd::game::*
# tier (above world/render primitives, below editor/samples).
#
# Dependencies (CLAUDE.md S7):
#   cd::core   - CD_NODISCARD + base defines.
#   cd::math   - Vec3f + lerp + length / normalize.
#   cd::camera - Camera struct (output of brain.tick()).
#   cd::ecs    - Entity opaque handle (follow targets).
#
# Note on the scene dependency in the G3.3 brief: the brain consumes target
# positions through an injected `TargetPositionFn` callable rather than
# pulling cd::scene into this library directly.  That keeps the dependency
# DAG flowing upward through gameplay -> sample/editor while remaining a
# drop-in for SceneCameraController's `scene_->world_transform(target)`
# path - the sample wraps the scene call site in a lambda.

cd_add_library(game_camera
  TYPE STATIC
  SOURCES
    src/VirtualCamera.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::camera
    cd::ecs
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
