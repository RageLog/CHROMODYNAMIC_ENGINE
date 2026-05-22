# engine/asset_image — cd::asset_image
#
# Stand-alone image decoder (PNG/JPG/TGA/BMP/GIF/HDR) on top of stb_image.
# Kept separate from cd::asset_gltf so callers that only need image
# decoding don't pull the JSON / glTF parser dependency chain.
#
# Vendored deps:
#   * nothings/stb — single-header library; only stb_image.h is consumed,
#     declared via FetchContent (smallest practical clone, no git history
#     needed thanks to GIT_SHALLOW). stb has no formal versioning, so we
#     pin to a recent stable commit.

include(FetchContent)

FetchContent_Declare(
  stb_image
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        f0569113c93ad095470c54bf34a17b36646bbbb5
  GIT_SHALLOW    FALSE  # stb commit-pin is older than the shallow window allows.
)

FetchContent_MakeAvailable(stb_image)

cd_add_library(asset_image
  TYPE STATIC
  SOURCES
    src/Image.cpp
  PUBLIC_DEPS
    cd::core
)

# stb headers as SYSTEM PRIVATE so its `-Wold-style-cast` etc. never trip
# our -Werror policy. The pragma push/pop inside Image.cpp covers the
# remaining macro-expanded warnings that survive the SYSTEM downgrade.
target_include_directories(cd_asset_image SYSTEM PRIVATE
  ${stb_image_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(cd_asset_image PRIVATE
    /wd4100 /wd4244 /wd4267 /wd4456 /wd4505 /wd4996
  )
else()
  target_compile_options(cd_asset_image PRIVATE
    -Wno-error=conversion
    -Wno-error=sign-conversion
    -Wno-error=old-style-cast
    -Wno-error=unused-parameter
    -Wno-error=unused-function
    -Wno-error=cast-align
    -Wno-error=double-promotion
    -Wno-error=missing-declarations
  )
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
