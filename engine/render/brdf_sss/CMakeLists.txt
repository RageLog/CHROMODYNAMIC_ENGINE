# engine/render/brdf_sss — cd::brdf_sss (Day 20)
# Burley 2015 SSS profile + Jimenez 2010 separable blur kernel.
cd_add_library(brdf_sss
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
