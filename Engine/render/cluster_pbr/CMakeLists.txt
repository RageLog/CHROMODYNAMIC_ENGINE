# engine/render/cluster_pbr — cd::cluster_pbr
#
# GLSL helper + C++ binding layout describing how a PBR fragment
# shader consumes the cluster_offsets + light_indices buffers produced
# by cd::cluster_gpu (Phase 9 Sprint 2). Closes Phase 8 routing item
# #1 — Forward+ PBR integration design.
#
# Interface library: no compiled code; the shader header is exposed as
# a string-literal constant for callers that runtime-compile via
# cd::shader, and a C++ struct documents the bindings layout for
# pipeline-side wiring.

cd_add_library(cluster_pbr
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
