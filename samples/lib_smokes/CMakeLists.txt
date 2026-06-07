# samples/lib_smokes — CPU-reference parity smokes for the queued
# render libs (Phase 232-233). Each sample exercises the library's
# public API + verifies the same math the GPU compute kernel runs at
# scale. Pattern is borrowed from hello_compute_cluster.
#
# phase900-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], lib_smokes
# samples are being audited and folded into either (a) library
# gtests when the smoke is pure CPU API + invariant checks, or
# (b) hello_engine panel toggles when the demo is interactive.
# Retired samples are kept out of the add_subdirectory list below
# until the directories are physically removed in a follow-on
# cleanup PR.
#
# Retired in phase 900/901 (each retirement's invariants are
# already covered by the library's own gtests):
#   * hello_ddgi              → engine/render/ddgi/tests/test_ddgi.cpp (20+ cases)
#   * hello_gpu_particles     → engine/render/gpu_particles/tests/test_gpu_particles.cpp (6 cases)
#   * hello_restir            → engine/render/restir_di/tests/test_restir_di.cpp (6 cases)
#   * hello_mesh_shader       → engine/render/mesh_shader/tests/test_mesh_shader.cpp (6 cases)
#   * hello_virtual_geometry  → engine/render/virtual_geometry/tests/test_cluster_dag.cpp (6 cases)
#   * hello_virtual_textures  → engine/render/virtual_textures/tests/test_virtual_textures.cpp (5 cases)
# Source directories deleted in the same commit (phase 901).
#
# Retained until library gtest coverage exists OR until the
# interactive demo path lands in hello_engine:
#   * hello_volumetric_fog    — no library tests yet (queue: migrate to engine/render/volumetric/tests/)
#   * hello_volumetric_clouds — no library tests yet (queue: same)
#   * hello_decals            — no library tests yet (queue: same)
#   * hello_nrc               — only 3 library tests; sample exercises extra surface (queue: expand tests)
#
add_subdirectory(hello_volumetric_fog)
add_subdirectory(hello_volumetric_clouds)
add_subdirectory(hello_decals)
add_subdirectory(hello_nrc)
