# samples/world — ECS / anim / events / replication / scene-graph samples.
#
# phase910-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], 5 headless
# world-tier smokes have been folded into the source libraries' gtests.
# Source directories deleted in this commit.
#
# Retired in phase 910:
#   * hello_ecs              -> engine/world/ecs/tests/test_ecs.cpp (44 incl.
#       BulkLifecycleStress + MultiComponentEach*) + test_archetype_world.cpp (11)
#       + test_scheduler.cpp (11) + test_query_cache.cpp + test_sphere_query.cpp.
#       The 10 000-entity sample stress N is a perf datapoint that is best
#       reproduced by the bench_archetype microbench (retained below).
#   * hello_events           -> engine/foundation/events/tests/test_events.cpp (14)
#   * hello_replication      -> engine/world/net/tests/test_net*.cpp (4 files,
#       ChannelMux + loopback_pair + reliable ordering) + scene serialize tests.
#   * hello_particles        -> engine/world/particle_system/tests/test_particle_system.cpp (7)
#       + engine/world/scene/tests/test_scene.cpp (3 ParticleSystem cases).
#   * hello_particle_system  -> engine/world/particle_system/tests/test_particle_system.cpp (7)
#
# Retained:
#   * bench_archetype           - SparseSetWorld vs ArchetypeWorld 1000-entity
#       microbenchmark. Category (c) standalone tool (perf datapoint).
#
# Retired in phase 1152 (endgame audit, SAFE-DELETE — covered elsewhere):
#   * hello_inspector    -> hello_editor hosts panel_inspector + HierarchyView
#       + scene save/load (samples/editor/hello_editor/main.cpp:53).
#   * hello_scene_graph  -> hello_engine Scene Serializer + ECS World Stress
#       probes (HelloShowcaseProbes.hpp) + engine/world/scene/tests (9).
#
# Retired in phase 1172 (SAFE-DELETE — capability superset: HelloSkinnedAnim in
#   hello_engine covers real CesiumMan skinning; cd::anim keyframe/clip/player
#   interpolation fully covered by engine/world/anim/tests/test_anim.cpp 55
#   unit-test cases + test_dual_quat.cpp; golden redundant):
#   * hello_anim -> hello_engine HelloSkinnedAnim.hpp + cd::anim unit tests.
add_subdirectory(bench_archetype)
