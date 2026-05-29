cd_add_library(post_smaa
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
