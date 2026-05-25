# engine/render/atmosphere — cd::atmosphere (Day 15)
# Hillaire 2020 production atmospheric scattering: transmittance +
# multi-scattering + sky-view + aerial-perspective LUTs.
cd_add_library(atmosphere
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
