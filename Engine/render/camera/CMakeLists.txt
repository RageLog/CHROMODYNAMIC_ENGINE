# engine/render/camera — cd::camera
#
# Header-only viewing camera + controllers. Depends only on cd::math.
# Kept INTERFACE so consumers (samples, editor, scene) don't drag in a .cpp
# TU just to compute MVP. The whole library is templates + inline functions.

cd_add_library(camera
  TYPE INTERFACE
  PUBLIC_DEPS cd::math
)
