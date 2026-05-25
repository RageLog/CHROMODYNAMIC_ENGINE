# engine/render/post_motion_blur — cd::post_motion_blur (Day 9)
# McGuire 2012 per-object tiled-max gather motion blur.
cd_add_library(post_motion_blur
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
