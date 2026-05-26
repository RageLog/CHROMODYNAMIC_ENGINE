# engine/asset — cd::asset
# Sprint S3.2 — typed asset loading on top of cd::vfs.

cd_add_library(asset
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::io cd::vfs cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
