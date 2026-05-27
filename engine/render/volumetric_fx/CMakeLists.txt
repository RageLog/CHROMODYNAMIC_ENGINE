# engine/render/volumetric_fx — cd::volumetric_fx (Phase 234 umbrella)
#
# Aggregates the three volumetric libs:
#   cd::render::volumetric  (legacy height-fog reference math)
#   cd::volumetric_fog       (Wronski 2014 froxel grid)
#   cd::volumetric_clouds    (Schneider 2017 cloud ray-march)
cd_add_library(volumetric_fx
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::volumetric
    cd::volumetric_fog
    cd::volumetric_clouds
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
