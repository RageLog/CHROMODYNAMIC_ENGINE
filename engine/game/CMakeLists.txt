# engine/game — Phase G gameplay tier (cd::game::*)
#
# Family of libraries defined by ADR-20260530-gameplay-library-family.
# Each sub-library lives at engine/game/<lib>/ with the canonical layout
# (include/cd/game/<lib>/ + src/ + tests/ + CMakeLists.txt + README.md),
# the cd_<lib> CMake target, and the cd::game::<lib> namespace.
#
# Cross-library dependency rule (CLAUDE.md §7): the gameplay tier sits
# strictly above world / script / ui and strictly below editor / samples.
# No reverse edges — no existing library acquires a dependency on
# cd::game::*. See ADR §4.3 for the full DAG.
#
# Phase G1 — "Foundations":
#   save     — G1.3: cd::game::save SaveSystem with atomic slot writes
#              (temp file + rename, default user-data root).
#   settings — G1.4: cd::game::settings persistent prefs + live broadcast.
#
# Phase G2 — "AI / Behaviour":
#   fsm      — G2.1: cd::game::fsm Hierarchical Finite State Machine
#              (Harel 1987 statecharts + UML 2.5.1 regions + history).
#   ai_bt    — G2.2: cd::game::ai_bt Behavior Tree
#              (Sequence / Selector / Parallel + Decorators + Blackboard).
#
# (Subsequent sub-libraries — time, input_binding — land in their own
# phase commits as the gameplay family grows. The world/ tree carries
# transitional `gameplay_time` and `gameplay_input_binding` aliases that
# will be folded under engine/game/ in a separate ADR-tracked migration.)

add_subdirectory(save)
add_subdirectory(settings)
add_subdirectory(fsm)
add_subdirectory(ai_bt)
