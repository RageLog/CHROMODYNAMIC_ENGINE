# engine/world/physics_soft_body — cd::physics::soft_body (Phase 721)
#
# Sprint-1 (phase721): CPU mass-spring soft-body simulation for cloth and rope.
#   * Verlet integration with position-based dynamics (PBD) constraint solver.
#   * Particle + SpringConstraint + SoftBodyConfig + SoftBody API.
#   * Pinned particles, damping, multi-iteration solver, apply_force impulse.
#
# Sprint-2 (phase760, shipped): self-collision — uniform spatial-hash broad-phase
#   + PBD particle-particle repulsion near-phase (default disabled).
# Sprint-2.5 (depth pass, shipped): Provot (1995) bending constraints +
#   half-space ground-plane collision response with optional Coulomb friction
#   (both opt-in -> existing sims unchanged).
#
# Sprint-3 (sealed/deferred): GPU compute port (Vulkan/D3D12) and FEM /
#   continuum volumetric soft bodies — multi-week, out of mass-spring charter.
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
