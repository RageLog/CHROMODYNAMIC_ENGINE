# engine/asset_obj — cd::asset_obj
#
# Hand-rolled Wavefront .obj loader. Depends only on cd::core + cd::math.
# No third-party deps (no tinyobjloader) — the format is small enough that
# a self-contained parser is shorter than wrapping a vendored library.

cd_add_library(asset_obj
  TYPE STATIC
  SOURCES
    src/ObjLoader.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
