# samples/render — render-tier showcase samples (Vulkan-gated).
#
# phase915-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], headless
# CPU "console proof" render smokes folded into library gtests.
# Source directories deleted in this commit.
#
# Retired in phase 915:
#   * hello_lit_pbr           -- orphan stub (CMakeLists.txt without
#       main.cpp). Dead infrastructure removed entirely.
#   * hello_clustered_lights  -> engine/render/cluster/tests/core/test_cluster.cpp (13)
#       (ClusterGrid + assign + finalise + histogram coverage).
#   * hello_compute_cluster   -> engine/render/cluster/tests/core/test_cluster.cpp (13)
#       (run_reference_compute CPU-CPU parity covered as part of the same suite).
#   * hello_ddgi              -> engine/render/ddgi/tests/test_ddgi.cpp (20)
#       (probe placement + irradiance accumulation + dispatch budget).
#   * hello_restir            -> engine/render/restir_di/tests/test_restir_di.cpp (6)
#       (reservoir + spatial reuse + sample weight invariants).
#
# Retained (all category b Vulkan / D3D12 visual demos):
#   hello_d3d12_pbr     - D3D12 backend PBR demo (Win32 only)
#   hello_framegraph    - framegraph end-to-end visual
#   hello_gpu_cluster   - Forward+ GPU dispatch + GPU/CPU parity (compute-gated)
#   hello_imgui         - first end-to-end ImGui visual
# phase1142: hello_pbr, hello_skybox, hello_render_thread folded
# (batch-2 sample consolidation; visual coverage lives in hello_engine).
if(WIN32)
  add_subdirectory(hello_d3d12_pbr)
endif()
add_subdirectory(hello_framegraph)
