# engine/world — Phase 4 world tier
# Sub-libraries (in build / dependency order):
#   ecs              — Sprint S4.1: entity-component-system (sparse-set storage).
#   scene            — S4.2 (planned): scene graph / transforms on top of ECS.
#   physics          — S4.3 (planned).
#   anim             — S4.4 (planned).
#   audio            — S4.5 (planned).
#   input            — S4.6 (planned).
#   net              — S4.7 (planned).
#   world_container  — gap #18 — World / Project / Level / Layer foundation.
#   sample_framework — Mega-Marathon M2: cd::sample::App lifecycle library.
#
# Cross-library dependency rule (CLAUDE.md §7): explicit context/registry
# objects only — no global state shared between world libraries.

add_subdirectory(ecs)
add_subdirectory(scene)
add_subdirectory(physics)
add_subdirectory(anim)
add_subdirectory(audio)
add_subdirectory(input)
add_subdirectory(net)
add_subdirectory(world_container)
add_subdirectory(sample_framework)
