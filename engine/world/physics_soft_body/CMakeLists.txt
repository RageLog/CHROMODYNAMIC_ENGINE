# engine/world/physics_soft_body — cd::physics::soft_body (Phase 721)
#
# Sprint-1: CPU mass-spring soft-body simulation for cloth and rope.
#   * Verlet integration with position-based dynamics (PBD) constraint solver.
#   * Particle + SpringConstraint + SoftBodyConfig + SoftBody API.
#   * Pinned particles, damping, multi-iteration solver, apply_force impulse.
#
# Sprint-2 (deferred): self-collision detection + spatial hashing.
# Sprint-3 (deferred): GPU compute port (Vulkan/D3D12 compute shader).
#
# Dependencies (CLAUDE.md §7):
#   cd::core — base defines (CD_NODISCARD etc.). PRIVATE — no downstream leak.
#
# No dependency on cd::physics (rigid-body interface) or cd::physics_jolt.
# Soft-body simulation is an independent deformable-body system — it does not
# share the IPhysicsWorld rigid-body abstraction.

cd_add_library(physics_soft_body
  TYPE STATIC
  SOURCES
    src/SoftBody.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
