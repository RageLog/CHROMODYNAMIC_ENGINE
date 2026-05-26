# engine/render/post_camera — cd::post_camera
#
# Inline camera/composition post-effects (vignette, chromatic aberration,
# film grain). Header-only GLSL helper strings + Settings structs.

cd_add_library(post_camera
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
)
