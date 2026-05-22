# engine/asset_cdmesh — cd::asset_cdmesh
#
# Cooked binary mesh format (.cdmesh). Producer side: any of the
# import paths (cd::asset_obj, cd::asset_gltf) → `save()`. Consumer side:
# the runtime reads `load()` straight into a CdMesh struct that the
# renderer can memcpy into GPU buffers.

cd_add_library(asset_cdmesh
  TYPE STATIC
  SOURCES
    src/CdMesh.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
