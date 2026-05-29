# engine/render/volumetric_fog — cd::volumetric_fog (Day 16)
# Wronski 2014 froxel-grid fog: injection + integration compute passes.
cd_add_library(volumetric_fog
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
