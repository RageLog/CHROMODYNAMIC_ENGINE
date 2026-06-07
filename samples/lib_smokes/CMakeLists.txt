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
# Retired (covered by library gtests):
#   * hello_ddgi — phase 900. Probe-weight invariants already in
#     `engine/render/ddgi/tests/test_ddgi.cpp` (20+ cases incl.
#     BlendWeightsSumToOneInsideGrid + BlendWeightsSumToZeroOutsideGrid).
#     Source directory deleted in the same commit.
#
add_subdirectory(hello_volumetric_fog)
add_subdirectory(hello_volumetric_clouds)
add_subdirectory(hello_gpu_particles)
add_subdirectory(hello_decals)
add_subdirectory(hello_restir)
add_subdirectory(hello_mesh_shader)
add_subdirectory(hello_nrc)
add_subdirectory(hello_virtual_geometry)
add_subdirectory(hello_virtual_textures)
