# engine/render/post_bloom — cd::post_bloom (Day 8)
# Karis 2013 stable bloom: 13-tap weighted downsample + 9-tap tent
# upsample + soft-knee threshold. Header-only CPU reference + GLSL
# compute kernels.
cd_add_library(post_bloom
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
