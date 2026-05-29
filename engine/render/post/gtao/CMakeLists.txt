# engine/render/post_gtao — cd::post_gtao (Day 4)
# Jimenez 2016 GTAO: horizon-scan AO + bent normals + bilateral denoise.
cd_add_library(post_gtao
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
