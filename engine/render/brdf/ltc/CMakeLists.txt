# engine/render/brdf_ltc — cd::brdf_ltc (Day 19)
# Heitz 2016 Linearly Transformed Cosines for analytical area lighting.
cd_add_library(brdf_ltc
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
