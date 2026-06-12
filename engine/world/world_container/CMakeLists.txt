# engine/world/world_container — cd::world_container (gap #18)
# Foundation library for the production World + Project + Level +
# Layer model. Header-only; integrates with the editor (v1.6) +
# streaming (v1.7) ships.
cd_add_library(world_container
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math cd::asset_json cd::ecs  # asset_json: ProjectIo (.cdproject persistence)
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
