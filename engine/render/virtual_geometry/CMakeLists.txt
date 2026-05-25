cd_add_library(virtual_geometry
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
