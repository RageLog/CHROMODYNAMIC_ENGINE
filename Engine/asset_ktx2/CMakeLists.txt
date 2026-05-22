# engine/asset_ktx2 — cd::asset_ktx2
#
# Minimal KTX2 runtime reader (BC7 + RGBA8, no supercompression).
# Hand-rolled binary parser; dep-free (cd::core only).

cd_add_library(asset_ktx2
  TYPE STATIC
  SOURCES
    src/Ktx2.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
