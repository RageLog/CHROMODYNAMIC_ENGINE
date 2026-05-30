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
#   fsm        — G2.1: cd::game::fsm Hierarchical Finite State Machine
#                (Harel 1987 statecharts + UML 2.5.1 regions + history).
#   ai_bt      — G2.2: cd::game::ai_bt Behavior Tree
#                (Sequence / Selector / Parallel + Decorators + Blackboard).
#   anim_graph — G2.3: cd::game::anim_graph Animation graph
#                (PlayClip leaves + 1D / 2D blend trees + nested state
#                machine over cd::anim::SkinnedClip / Pose / Skeleton).
#
# Phase G3 — "Spatial / ECS queries / world interaction":
#   query    — G3.1: cd::game::query QueryWorld facade over
#              cd::scene::SpatialHash + cd::scene::Frustum. Provides
#              raycast / sphere / box / frustum gameplay queries.
#   trigger  — G3.2: cd::game::trigger TriggerVolume + TriggerWorld
#              (AABB/Sphere volumes + on_enter/on_stay/on_exit dispatch +
#              layer-mask filter + implicit despawn-exit). Forward-declares
#              cd::game::query::QueryWorld so the indexed broad-phase plugs
#              into tick() without a hard link-time dependency.
#   camera   — G3.3: cd::game::camera Cinemachine-style virtual camera
#              stack + brain (priority + blend + critically-damped spring
#              damping). Outputs cd::camera::Camera; resolves entity
#              positions through an injected TargetPositionFn so the
#              gameplay tier never pulls cd::scene downward.
#   particles_event — G3.4: cd::game::particles_event recipe-driven
#                     gameplay particle dispatcher. Stages authored named
#                     bursts + on_emit callbacks; the GPU sink
#                     (cd::render::gpu_particles) is one tier above and
#                     pulls staged data via active_bursts() — this library
#                     does NOT link against it.
#
# Phase G4 — "Narrative / authoring tools":
#   dialogue — G4.1: cd::game::dialogue Branching dialogue VM
#              (Ink / Yarn Spinner-style DialogueNode + DialogueChoice graph
#              with predicate-gated choices over a typed Blackboard).
#   quest    — G4.2: cd::game::quest QuestLog objective tracker + journal
#              (4-bucket partitioning + auto-complete / auto-fail on
#              objective state change + binary serialise / restore).
#
# (Subsequent sub-libraries — time, input_binding — land in their own
# phase commits as the gameplay family grows. The world/ tree carries
# transitional `gameplay_time` and `gameplay_input_binding` aliases that
# will be folded under engine/game/ in a separate ADR-tracked migration.)

add_subdirectory(save)
add_subdirectory(settings)
add_subdirectory(fsm)
add_subdirectory(ai_bt)
add_subdirectory(anim_graph)
add_subdirectory(query)
add_subdirectory(trigger)
add_subdirectory(camera)
add_subdirectory(dialogue)
add_subdirectory(quest)
add_subdirectory(particles_event)

# Phase G5 — "Live authoring / runtime tooling":
#   asset_hot_reload — G5.1: cd::game::asset_hot_reload HotReloadBus over
#                       cd::asset::FileWatcher. Categorised subscriber model
#                       with 100 ms throttle window for editor-save bursts.
add_subdirectory(asset_hot_reload)
