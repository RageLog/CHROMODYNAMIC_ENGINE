# engine/render/mesh_shader — cd::mesh_shader (Day 25)
# Meshlet cluster builder + task/mesh shader skeletons for VK_EXT_mesh_shader.
cd_add_library(mesh_shader
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
    cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
