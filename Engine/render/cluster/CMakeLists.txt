# engine/render/cluster — cd::render::cluster
#
# CPU baseline for clustered forward+ light culling. Header-only;
# depends only on cd::core + cd::math. GPU compute shader port lands
# in a Phase 7+ wave.

cd_add_library(cluster
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
