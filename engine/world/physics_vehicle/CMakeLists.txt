# engine/world/physics_vehicle — cd::physics::vehicle (Phase 670/692)
#
# Sprint-1 (Phase 670): CPU-side 4-wheel vehicle model (bicycle model + gear
#   ratio torque chain).
# Sprint-2 (Phase 692): JoltAdapter bridges to cd::physics::IPhysicsWorld so
#   the chassis responds to terrain via rigid-body collision.
#
# Provides:
#   * WheelConfig          — per-wheel geometry, suspension, and steering.
#   * EngineConfig         — max torque, 6 gear ratios, final drive, RPM.
#   * VehicleConfig        — chassis + wheel array + engine assembly.
#   *   use_jolt bool      — Sprint-2 flag (default false).
#   * VehicleState         — per-frame speed/rpm/gear/steer output.
#   * Vehicle              — configure / configure_jolt / set_input / tick.
#   * JoltAdapter          — configure / sync_to_jolt / sync_from_jolt.
#   * WheelConstraintDesc  — per-wheel bookkeeping (Sprint-2 stub; Sprint-3
#                             replaces with JPH::WheelConstraint* once the
#                             vcpkg jolt-physics port lands).
#
# Dependencies (CLAUDE.md §7):
#   cd::physics      — IPhysicsWorld interface + BodyHandle.  PUBLIC so
#                      downstream consumers inherit the header transitively.
#   cd::physics_jolt — is_stub_backend() used by tests. PRIVATE — callers
#                      should not need to see JoltWorld.hpp to drive a Vehicle.
#   cd::math         — Vec3f.  Transitive via cd::physics.
#   cd::core         — CD_NODISCARD + base defines.  Transitive.

cd_add_library(physics_vehicle
  TYPE STATIC
  SOURCES
    src/Vehicle.cpp
    src/JoltAdapter.cpp
  PUBLIC_DEPS
    cd::physics
    cd::math
    cd::core
  PRIVATE_DEPS
    cd::physics_jolt
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
