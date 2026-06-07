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
# Retired in phase 900-904 (each retirement's invariants are
# already covered by the library's own gtests):
#   * hello_ddgi              → engine/render/ddgi/tests/test_ddgi.cpp (20+ cases)
#   * hello_gpu_particles     → engine/render/gpu_particles/tests/test_gpu_particles.cpp (6 cases)
#   * hello_restir            → engine/render/restir_di/tests/test_restir_di.cpp (6 cases)
#   * hello_mesh_shader       → engine/render/mesh_shader/tests/test_mesh_shader.cpp (6 cases)
#   * hello_virtual_geometry  → engine/render/virtual_geometry/tests/test_cluster_dag.cpp (6 cases)
#   * hello_virtual_textures  → engine/render/virtual_textures/tests/test_virtual_textures.cpp (5 cases)
#   * hello_volumetric_fog    → engine/render/volumetric/tests/fog/test_volumetric_fog.cpp (6) + _froxel.cpp (20) — 26 cases total
#   * hello_volumetric_clouds → engine/render/volumetric/tests/clouds/test_volumetric_clouds.cpp (6 cases)
#   * hello_decals            → engine/render/decal/tests/test_decal.cpp (6 cases)
#   * hello_nrc               → engine/render/nrc/tests/test_nrc.cpp (3 cases including TrainingReducesError)
# All source directories deleted. samples/lib_smokes/ is now EMPTY.
#
# The phase 901 audit missed the volumetric/decal tests because they
# live under nested test subdirs (engine/render/volumetric/tests/fog/
# not engine/render/volumetric/fog/tests/). Phase 904 corrected the
# coverage check + retired the 4 lingering entries in one batch.

# Empty — see retirement notes above. Directory kept as a place-
# holder so the consolidation policy stays discoverable; will be
# deleted entirely when samples/asset + samples/audio + samples/
# editor follow the same pattern.
