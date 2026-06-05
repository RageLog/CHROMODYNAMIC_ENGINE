# engine/ui/font — Glyph atlas + outline rasterizer (ADR-20260530, Phase 1.1 + 4 + 752)
#
# cd::ui_font Phase 1.1:    stb_truetype rasterizer + skyline bin-pack +
#                           alpha atlas (single page, no LRU).
# cd::ui_font Phase 4 (T2.4): FreeType outline backend + HarfBuzz BiDi /
#                           ligature shaping + 8-SSED unsigned MSDF atlas.
# cd::ui_font Phase 752 Sprint-2: msdfgen core multi-channel MSDF atlas
#                           (AtlasMode::kMsdfMulti — sharp corners, no rounding).
#
# Dependency resolution tiers (same as cd::spirv_cross_glue / cd::shader):
#   1. vcpkg find_package(Freetype CONFIG) + find_package(harfbuzz CONFIG)
#      + find_package(msdfgen CONFIG QUIET)  — CI canonical.
#   2. FetchContent fallback — freetype + harfbuzz vendored at pinned tags
#      (only attempted when CD_UI_FONT_ENABLE_FETCHCONTENT_FT is ON; OFF
#      by default to keep clean ninja-base configures cheap).
#   3. stb_truetype-only — when neither tier resolves, the FT/HB defines
#      stay 0 and Font::shape() falls back to identity shaping. All
#      Phase 1.1 tests continue to pass unchanged.
#
# Backend selection at compile time:
#   CD_UI_FONT_HAVE_FREETYPE = 1  when FreeType was resolved
#   CD_UI_FONT_HAVE_HARFBUZZ = 1  when HarfBuzz was resolved (implies FT)
#   CD_UI_FONT_HAVE_MSDFGEN  = 1  when msdfgen core was resolved (kMsdfMulti)
#
# Note: stb_truetype.h is reused from cd::asset_image's stb FetchContent.
# CMake ordering puts engine/asset before engine/ui so the variable is
# populated by the time this file runs.

include(FetchContent)

set(_CD_UI_FONT_HAS_MSDF 0)
set(_CD_UI_FONT_MSDF_TARGETS "")

option(CD_UI_FONT_ENABLE_FETCHCONTENT_FT
       "Allow cd::ui_font to FetchContent freetype + harfbuzz when find_package fails (off by default; keeps clean configures fast). When OFF the lib silently degrades to the stb_truetype-only backend."
       OFF)

set(_CD_UI_FONT_HAS_FT 0)
set(_CD_UI_FONT_HAS_HB 0)
set(_CD_UI_FONT_FT_TARGETS "")
set(_CD_UI_FONT_HB_TARGETS "")

# --- Tier 1: vcpkg find_package -------------------------------------------
# Freetype CONFIG mode (vcpkg) exports Freetype::Freetype. The legacy
# MODULE form (used by some Linux distros) exposes FREETYPE_LIBRARIES.
find_package(Freetype CONFIG QUIET)
if(NOT Freetype_FOUND)
  find_package(Freetype QUIET)
endif()
if(Freetype_FOUND OR FREETYPE_FOUND)
  message(STATUS "[cd] ui_font: FreeType via find_package")
  set(_CD_UI_FONT_HAS_FT 1)
  if(TARGET Freetype::Freetype)
    list(APPEND _CD_UI_FONT_FT_TARGETS Freetype::Freetype)
  endif()
endif()

if(_CD_UI_FONT_HAS_FT)
  # HB is OPTIONAL — when missing we keep FT outlines but degrade
  # shape() to identity.
  find_package(harfbuzz CONFIG QUIET)
  if(harfbuzz_FOUND AND TARGET harfbuzz::harfbuzz)
    message(STATUS "[cd] ui_font: HarfBuzz via find_package")
    set(_CD_UI_FONT_HAS_HB 1)
    list(APPEND _CD_UI_FONT_HB_TARGETS harfbuzz::harfbuzz)
  endif()
endif()

# --- Tier 1.5: HarfBuzz FetchContent (CMake) when vcpkg HB absent ---------
# vcpkg harfbuzz uses Meson which may hang on some CI/dev environments.
# This fallback uses HarfBuzz's community-maintained CMakeLists.txt (CMake 3.14+)
# and is activated when:
#   - FreeType WAS resolved (tier 1 or 2 — we need FT for shaping) AND
#   - HarfBuzz was NOT resolved via vcpkg/find_package AND
#   - CD_UI_FONT_ENABLE_FETCHCONTENT_HB is ON (opt-in; OFF by default so
#     clean CI without network access stays fast).
# We build only the core harfbuzz target (no Graphite2, no GLib, no ICU,
# no FreeType integration inside HB — HarfBuzz links our Freetype::Freetype
# at the cd_ui_font level instead to avoid circular dep).
option(CD_UI_FONT_ENABLE_FETCHCONTENT_HB
       "Allow cd::ui_font to FetchContent harfbuzz@12.3.1 via CMake when find_package fails. Off by default; enable when vcpkg meson-based build hangs."
       OFF)

if(_CD_UI_FONT_HAS_FT AND NOT _CD_UI_FONT_HAS_HB AND CD_UI_FONT_ENABLE_FETCHCONTENT_HB)
  message(STATUS "[cd] ui_font: HarfBuzz via FetchContent CMake (12.3.1)")
  set(HB_HAVE_FREETYPE   OFF CACHE BOOL "" FORCE)  # no FT inside HB; we link separately
  set(HB_HAVE_GLIB       OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_GOBJECT    OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_ICU        OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_GRAPHITE2  OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_DIRECTWRITE OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_CAIRO      OFF CACHE BOOL "" FORCE)
  set(HB_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
  set(HB_BUILD_SUBSET    OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG        12.3.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(harfbuzz)
  # HarfBuzz CMake exports 'harfbuzz' target (no namespace in CMakeLists.txt)
  if(TARGET harfbuzz)
    set(_CD_UI_FONT_HAS_HB 1)
    list(APPEND _CD_UI_FONT_HB_TARGETS harfbuzz)
    if(NOT TARGET harfbuzz::harfbuzz)
      add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
    endif()
  endif()
endif()

# --- Tier 2: FetchContent fallback ----------------------------------------
if(NOT _CD_UI_FONT_HAS_FT AND CD_UI_FONT_ENABLE_FETCHCONTENT_FT)
  message(STATUS "[cd] ui_font: FreeType via FetchContent (VER-2-13-3)")
  set(FT_DISABLE_HARFBUZZ ON  CACHE BOOL "" FORCE)  # avoid HB->FT cycle
  set(FT_DISABLE_BROTLI   ON  CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2    ON  CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG      ON  CACHE BOOL "" FORCE)
  set(FT_DISABLE_ZLIB     ON  CACHE BOOL "" FORCE)
  FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(freetype)
  if(TARGET freetype)
    set(_CD_UI_FONT_HAS_FT 1)
    list(APPEND _CD_UI_FONT_FT_TARGETS freetype)
    if(NOT TARGET Freetype::Freetype)
      add_library(Freetype::Freetype ALIAS freetype)
    endif()
  endif()
endif()

# --- Tier 1: msdfgen vcpkg find_package (core-only, no extensions) -------
# msdfgen is pulled from vcpkg with default-features=false in vcpkg.json,
# which means only the core library (MSDFGEN_CORE_ONLY=ON). This gives us
# generateMSDF() without pulling in freetype/libpng/tinyxml2/skia again.
# The vcpkg CONFIG package exports msdfgen::msdfgen-core.
# msdfgen requires FreeType to be available as well (we build the glyph
# Shape by decomposing the FT_Outline), so we gate activation on _HAS_FT.
if(_CD_UI_FONT_HAS_FT)
  find_package(msdfgen CONFIG QUIET)
  if(msdfgen_FOUND)
    # vcpkg core-only build exports msdfgen::msdfgen-core (no font loading).
    if(TARGET msdfgen::msdfgen-core)
      message(STATUS "[cd] ui_font: msdfgen via find_package (core-only, kMsdfMulti enabled)")
      set(_CD_UI_FONT_HAS_MSDF 1)
      list(APPEND _CD_UI_FONT_MSDF_TARGETS msdfgen::msdfgen-core)
    elseif(TARGET msdfgen::msdfgen)
      # Some package variants export the combined target instead.
      message(STATUS "[cd] ui_font: msdfgen via find_package (combined target, kMsdfMulti enabled)")
      set(_CD_UI_FONT_HAS_MSDF 1)
      list(APPEND _CD_UI_FONT_MSDF_TARGETS msdfgen::msdfgen)
    else()
      message(STATUS "[cd] ui_font: msdfgen found but no known target — kMsdfMulti disabled")
    endif()
  else()
    message(STATUS "[cd] ui_font: msdfgen not found — kMsdfMulti will degrade to kMsdf")
  endif()
endif()

# --- Stb_truetype: reuse asset_image's FetchContent --------------------
FetchContent_GetProperties(stb_image)
if(NOT stb_image_POPULATED)
  FetchContent_Declare(
    stb_image
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f0569113c93ad095470c54bf34a17b36646bbbb5
    GIT_SHALLOW    FALSE
  )
  FetchContent_MakeAvailable(stb_image)
endif()

# --- Library ---------------------------------------------------------------
set(_CD_UI_FONT_SOURCES src/Font.cpp)
if(_CD_UI_FONT_HAS_FT)
  list(APPEND _CD_UI_FONT_SOURCES src/FreeTypeBackend.cpp)
endif()
if(_CD_UI_FONT_HAS_HB)
  list(APPEND _CD_UI_FONT_SOURCES src/HarfBuzzShaping.cpp)
endif()
if(_CD_UI_FONT_HAS_MSDF)
  list(APPEND _CD_UI_FONT_SOURCES src/MsdfgenBackend.cpp)
endif()

cd_add_library(ui_font
  TYPE STATIC
  SOURCES
    ${_CD_UI_FONT_SOURCES}
  PUBLIC_DEPS
    cd::core
)

target_include_directories(cd_ui_font SYSTEM PRIVATE
  ${stb_image_SOURCE_DIR}
)

if(_CD_UI_FONT_HAS_FT AND _CD_UI_FONT_FT_TARGETS)
  target_link_libraries(cd_ui_font PRIVATE ${_CD_UI_FONT_FT_TARGETS})
  target_compile_definitions(cd_ui_font PUBLIC CD_UI_FONT_HAVE_FREETYPE=1)
  foreach(_t ${_CD_UI_FONT_FT_TARGETS})
    if(TARGET ${_t})
      cd_mark_target_system_includes(${_t})
    endif()
  endforeach()
endif()

if(_CD_UI_FONT_HAS_HB AND _CD_UI_FONT_HB_TARGETS)
  target_link_libraries(cd_ui_font PRIVATE ${_CD_UI_FONT_HB_TARGETS})
  target_compile_definitions(cd_ui_font PUBLIC CD_UI_FONT_HAVE_HARFBUZZ=1)
  foreach(_t ${_CD_UI_FONT_HB_TARGETS})
    if(TARGET ${_t})
      cd_mark_target_system_includes(${_t})
    endif()
  endforeach()
endif()

if(_CD_UI_FONT_HAS_MSDF AND _CD_UI_FONT_MSDF_TARGETS)
  target_link_libraries(cd_ui_font PRIVATE ${_CD_UI_FONT_MSDF_TARGETS})
  target_compile_definitions(cd_ui_font PUBLIC CD_UI_FONT_HAVE_MSDFGEN=1)
  foreach(_t ${_CD_UI_FONT_MSDF_TARGETS})
    if(TARGET ${_t})
      cd_mark_target_system_includes(${_t})
    endif()
  endforeach()
endif()

# stb_truetype + the existing skyline packer use a few old-style idioms;
# silence those on the cd_ui_font sources without weakening the global
# warning policy for the rest of the engine.
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
