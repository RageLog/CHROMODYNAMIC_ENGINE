# engine/render/volumetric — volumetric sub-libraries (phase420)
#
# Layout:
#   include/cd/render/volumetric/  — volumetric core headers (legacy namespace)
#   include/cd/volumetric/<sub>/   — fog/clouds/fx headers
#   tests/<sub>/                   — per-sub tests
#
# Targets: cd::volumetric cd::volumetric_fog cd::volumetric_clouds cd::volumetric_fx

cd_add_library(volumetric
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(volumetric_fog
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(volumetric_clouds
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

cd_add_library(volumetric_fx
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::volumetric
    cd::volumetric_fog
    cd::volumetric_clouds
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests/core)
  add_subdirectory(tests/clouds)
  add_subdirectory(tests/fog)
  add_subdirectory(tests/fx)
endif()
