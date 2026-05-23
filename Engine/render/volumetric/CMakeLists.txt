# engine/render/volumetric — cd::render::volumetric
#
# CPU baseline for volumetric fog / atmospheric scattering. Header-
# only; cd::core + cd::math only. GPU compute / froxel-grid integration
# is a Phase 8+ wave.

cd_add_library(volumetric
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
