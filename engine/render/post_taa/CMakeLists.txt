# engine/render/post_taa — cd::post_taa (Day 11)
# Karis 2014 TAA: history reprojection + neighborhood AABB clipping +
# motion-aware blend. Header-only CPU reference + GLSL compute kernel.
cd_add_library(post_taa
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
