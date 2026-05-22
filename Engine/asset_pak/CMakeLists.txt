# engine/asset_pak — cd::asset_pak
#
# Single-file asset bundle format. Header-only — depends only on cd::core.

cd_add_library(asset_pak
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
