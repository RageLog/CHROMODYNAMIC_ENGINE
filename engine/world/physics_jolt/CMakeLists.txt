# engine/world/physics_jolt — cd::physics_jolt (Phase 525 / T0.3)
#
# Jolt Physics 5.x backend implementing `cd::physics::IPhysicsWorld`
# (engine/world/physics/include/cd/physics/IPhysicsWorld.hpp).
#
# Scope at Run 525:
#   This subdirectory ships the **STUB** backend per the brief's escape
#   hatch. The actual Jolt vendor wiring (vcpkg `jolt-physics` port +
#   `Dependencies/jolt/` submodule + `Jolt::Jolt` private link) is
#   deferred to a follow-up ADR (`ADR-2026xxxx-physics-jolt-bringup`).
#   See `docs/ADR/ADR-20260530-jolt-physics-integration.md` §F for the
#   phased delivery plan.
#
# Dependency resolution tiers (same pattern as engine/render/spirv_cross_glue
# and engine/ui/font — see vcpkg.json `$x-policy-note`):
#   1. vcpkg find_package(jolt CONFIG)  — reserved; not active until bringup
#      ADR lands AND the port is verified resolvable from
#      `vcpkg.json` builtin-baseline 56bb2411.
#   2. FetchContent fallback from https://github.com/jrouwe/JoltPhysics
#      pinned to a specific tag — reserved.
#   3. STUB backend (current default) — delegates to
#      `cd::physics::make_builtin_physics_world()` for the dynamics
#      step, keeps the public `cd::physics_jolt::JoltWorld` surface
#      stable so downstream consumers / tests / samples can build
#      against the API today.
#
# Dependency rule (CLAUDE.md §7):
#   * Depends on `cd::physics` (public interface, ABI-stable).
#   * Depends on `cd::math` + `cd::core` transitively via `cd::physics`.
#   * NEVER include any Jolt header from a public file — the boundary
#     is enforced by ADR-20260530 §B and a CI grep gate when the real
#     backend lands.

cd_add_library(physics_jolt
  TYPE STATIC
  SOURCES
    src/JoltWorld.cpp
  PUBLIC_DEPS
    cd::physics
    cd::math
    cd::core
)

target_compile_definitions(cd_physics_jolt PRIVATE CD_PHYSICS_JOLT_STUB=1)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
