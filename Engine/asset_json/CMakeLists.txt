# engine/asset_json — cd::asset_json
#
# Hand-rolled JSON subset parser. cd::core only.

cd_add_library(asset_json
  TYPE STATIC
  SOURCES
    src/Json.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
