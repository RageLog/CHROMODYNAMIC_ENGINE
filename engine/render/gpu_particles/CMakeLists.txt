cd_add_library(gpu_particles
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
