# engine/render/decal — cd::decal (Day 22)
# Persson 2009 deferred screen-space decals.
cd_add_library(decal
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
