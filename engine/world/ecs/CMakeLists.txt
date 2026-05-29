# engine/world/ecs — cd::ecs (Phase 4 / Sprint S4.1)
#
# Sparse-set ECS (EnTT-style storage layout). Header-only-with-impl-in-one-
# TU: the templates live in the public headers; the small EntityManager
# allocator gets a single TU so its TU footprint is amortized across
# consumers.

cd_add_library(ecs
  TYPE STATIC
  SOURCES
    src/Entity.cpp
    src/ArchetypeWorld.cpp
  PUBLIC_DEPS
    cd::core
    cd::concurrency  # Scheduler::tick_parallel(World&, ThreadPool&) overload
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
