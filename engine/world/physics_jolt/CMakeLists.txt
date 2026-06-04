# engine/world/physics_jolt — cd::physics_jolt (FINALE-3 / W2B-C2, phase748)
#
# Jolt Physics 5.x backend implementing `cd::physics::IPhysicsWorld`
# (engine/world/physics/include/cd/physics/IPhysicsWorld.hpp).
#
# Dependency resolution tiers (same pattern as engine/render/spirv_cross_glue
# and engine/ui/font — see vcpkg.json `$x-policy-note`):
#   1. vcpkg find_package(Jolt CONFIG) — active when CD_DISABLE_VCPKG=OFF and
#      joltphysics is installed in the manifest vcpkg_installed dir.
#   2. FetchContent from https://github.com/jrouwe/JoltPhysics at tag v5.5.0
#      — offline-friendly once the GIT cache is warm.
#   3. STUB fallback — if both tiers fail (network-isolated CI or explicit
#      CD_PHYSICS_JOLT_FORCE_STUB=ON), delegates to the built-in semi-implicit
#      Euler reference so downstream consumers can still build and test.
#
# CMake compile definitions:
#   CD_PHYSICS_JOLT_REAL=1   — Jolt::Jolt linked; JPH::PhysicsSystem is active.
#   CD_PHYSICS_JOLT_STUB=1   — fallback Euler delegate (no Jolt symbols).
#
# Vendor boundary policy (ADR-20260530 §B):
#   * Public header (JoltWorld.hpp) NEVER includes <Jolt/Jolt.h>.
#   * JPH::* symbols are PIMPL-hidden inside JoltWorld.cpp.
#   * Downstream consumers depend ONLY on cd::physics::IPhysicsWorld.
#
# Dependency rule (CLAUDE.md §7):
#   * Public:  cd::physics  cd::math  cd::core
#   * Private: Jolt::Jolt  (when tier 1 or 2 resolves)

include(FetchContent)

option(CD_PHYSICS_JOLT_FORCE_STUB
  "Force the Euler stub backend even when Jolt::Jolt is available (CI escape hatch)"
  OFF)

# ---------------------------------------------------------------------------
# Tier 1: vcpkg find_package
# ---------------------------------------------------------------------------
set(_CD_JOLT_FOUND FALSE)
if(NOT CD_PHYSICS_JOLT_FORCE_STUB)
  find_package(Jolt CONFIG QUIET)
  if(Jolt_FOUND)
    message(STATUS "[cd] physics_jolt: Jolt ${Jolt_VERSION} via vcpkg/find_package")
    set(_CD_JOLT_TARGET Jolt::Jolt)
    set(_CD_JOLT_FOUND TRUE)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tier 2: FetchContent — JoltPhysics v5.5.0
# ---------------------------------------------------------------------------
if(NOT _CD_JOLT_FOUND AND NOT CD_PHYSICS_JOLT_FORCE_STUB)
  message(STATUS "[cd] physics_jolt: Jolt via FetchContent (v5.5.0)")

  set(BUILD_UNIT_TESTS          OFF CACHE BOOL "" FORCE)
  set(TARGET_UNIT_TESTS         OFF CACHE BOOL "" FORCE)
  set(TARGET_HELLO_WORLD        OFF CACHE BOOL "" FORCE)
  set(TARGET_PERFORMANCE_TEST   OFF CACHE BOOL "" FORCE)
  set(TARGET_SAMPLES            OFF CACHE BOOL "" FORCE)
  set(TARGET_VIEWER             OFF CACHE BOOL "" FORCE)
  set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
  set(ENABLE_ALL_WARNINGS       OFF CACHE BOOL "" FORCE)
  set(OVERRIDE_CXX_FLAGS        OFF CACHE BOOL "" FORCE)
  set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.5.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
  )

  FetchContent_GetProperties(JoltPhysics)
  if(NOT joltphysics_POPULATED)
    FetchContent_MakeAvailable(JoltPhysics)
  endif()

  if(TARGET Jolt)
    message(STATUS "[cd] physics_jolt: Jolt FetchContent target ready")
    set(_CD_JOLT_TARGET Jolt)
    set(_CD_JOLT_FOUND TRUE)

    # Silence vendor warnings — Jolt uses -Wall internally but we don't own it
    if(MSVC)
      target_compile_options(Jolt PRIVATE /W0)
    else()
      target_compile_options(Jolt PRIVATE -w)
    endif()
  else()
    message(STATUS "[cd] physics_jolt: FetchContent did not produce Jolt target — using Euler stub")
  endif()
endif()

# ---------------------------------------------------------------------------
# Tier 3: stub fallback
# ---------------------------------------------------------------------------
if(NOT _CD_JOLT_FOUND)
  message(STATUS "[cd] physics_jolt: using semi-implicit Euler stub (no Jolt vendor)")
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
cd_add_library(physics_jolt
  TYPE STATIC
  SOURCES
    src/JoltWorld.cpp
  PUBLIC_DEPS
    cd::physics
    cd::math
    cd::core
)

if(_CD_JOLT_FOUND)
  target_compile_definitions(cd_physics_jolt PRIVATE CD_PHYSICS_JOLT_REAL=1)
  target_link_libraries(cd_physics_jolt PRIVATE ${_CD_JOLT_TARGET})

  # Silence vendor include warnings from Jolt headers pulled in via PRIVATE link
  if(TARGET ${_CD_JOLT_TARGET})
    cd_mark_target_system_includes(${_CD_JOLT_TARGET})
  endif()
else()
  target_compile_definitions(cd_physics_jolt PRIVATE CD_PHYSICS_JOLT_STUB=1)
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
