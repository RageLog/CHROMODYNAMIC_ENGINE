# engine/render/cluster — cluster sub-libraries (phase420)
#
# Layout:
#   include/cd/render/cluster/  — cluster core headers
#   include/cd/cluster/<sub>/   — gpu/pbr/fx headers
#   src/gpu/                    — GpuPipeline.cpp
#   shaders/                    — compute shaders
#   tests/<sub>/                — per-sub tests
#
# Targets: cd::cluster cd::cluster_gpu cd::cluster_pbr cd::cluster_fx

cd_add_library(cluster
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(cluster_gpu
  TYPE STATIC
  EXCLUDE_FROM_INSTALL
  SOURCES src/gpu/GpuPipeline.cpp
  PUBLIC_DEPS cd::cluster cd::rhi cd::shader cd::core
)

cd_add_library(cluster_pbr
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

cd_add_library(cluster_fx
  TYPE INTERFACE
  EXCLUDE_FROM_INSTALL
  PUBLIC_DEPS
    cd::cluster
    cd::cluster_gpu
    cd::cluster_pbr
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests/core)
  add_subdirectory(tests/fx)
  add_subdirectory(tests/gpu)
  add_subdirectory(tests/pbr)
endif()
