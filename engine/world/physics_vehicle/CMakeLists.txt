# engine/world/physics_vehicle — cd::physics::vehicle (Phase 670)
#
# Sprint-1: CPU-side 4-wheel vehicle model (bicycle model + gear-ratio torque).
#
# Provides:
#   * WheelConfig   — per-wheel geometry, suspension, and steering parameters.
#   * EngineConfig  — max torque, 6 gear ratios, final drive, RPM limits.
#   * VehicleConfig — chassis + wheel array + engine assembly.
#   * VehicleState  — per-frame speed/rpm/gear/steer output.
#   * Vehicle       — configure / set_input / tick / state API.
#
# Sprint-1 scope: longitudinal bicycle model, auto gear-shift, no terrain.
# Sprint-2 will wire cd::physics::IPhysicsWorld rigid-body handles and real
# terrain-contact queries (Jolt integration, see physics_jolt CMakeLists).
#
# Dependencies (CLAUDE.md §7):
#   cd::physics (public interface + IPhysicsWorld header) — Sprint-2 wiring.
#   cd::math    — Vec3f transitively via cd::physics.
#   cd::core    — CD_NODISCARD + base defines.
#
# Sprint-1 note: cd::physics is listed as a PUBLIC dep so Sprint-2 consumers
# only need to link cd::physics_vehicle and get the physics interface headers
# transitively. No cd::physics symbols are called in Sprint-1; the dependency
# is structural only.

cd_add_library(physics_vehicle
  TYPE STATIC
  SOURCES
    src/Vehicle.cpp
  PUBLIC_DEPS
    cd::physics
    cd::math
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
