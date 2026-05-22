# engine/asset_cdtex — cd::asset_cdtex
#
# Runtime reader for the .cdtex BC7 texture format produced by
# tools/cook_texture. Hand-rolled binary parser, dep-free (cd::core only).

cd_add_library(asset_cdtex
  TYPE STATIC
  SOURCES
    src/CdTex.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
