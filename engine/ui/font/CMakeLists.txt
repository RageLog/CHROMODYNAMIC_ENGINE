# engine/ui/font — Glyph atlas + TTF rasterizer (ADR-20260530, Phase 1.1)
#
# cd::ui_font — wraps stb_truetype to rasterize Unicode codepoint ranges
# into a single-page atlas with skyline bin-packing. Phase 4 upgrades
# the stack to FreeType + HarfBuzz per ADR-009 (BiDi / script shaping).
#
# stb_truetype.h is part of the nothings/stb repository already fetched
# by cd::asset_image (engine/asset/CMakeLists.txt) -- we reuse
# `${stb_image_SOURCE_DIR}` as the include path. CMake ordering puts
# engine/asset before engine/ui at the engine/ level so the variable
# is populated before this file runs.

cd_add_library(ui_font
  TYPE STATIC
  SOURCES
    src/Font.cpp
  PUBLIC_DEPS
    cd::core
)

include(FetchContent)
FetchContent_GetProperties(stb_image)
if(NOT stb_image_POPULATED)
  # asset_image normally fetches this; we re-declare in case ui_font is
  # built standalone (FetchContent dedupes by name so this is a no-op
  # when asset_image already declared stb_image).
  FetchContent_Declare(
    stb_image
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f0569113c93ad095470c54bf34a17b36646bbbb5
    GIT_SHALLOW    FALSE
  )
  FetchContent_MakeAvailable(stb_image)
endif()

target_include_directories(cd_ui_font SYSTEM PRIVATE
  ${stb_image_SOURCE_DIR}
)

# stb_truetype uses old-style casts + sign conversion + unused
# parameters internally; silence those for the .cpp that pulls it in.
if(MSVC)
  target_compile_options(cd_ui_font PRIVATE /wd4244 /wd4267 /wd4456 /wd4505 /wd4996)
else()
  target_compile_options(cd_ui_font PRIVATE
    -Wno-error=old-style-cast
    -Wno-error=sign-conversion
    -Wno-error=conversion
    -Wno-error=cast-align
    -Wno-error=unused-parameter
    -Wno-error=double-promotion
    -Wno-error=missing-declarations
    -Wno-error=cast-qual
    -Wno-error=shadow
  )
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
