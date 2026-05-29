cd_add_library(asset_streaming
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
