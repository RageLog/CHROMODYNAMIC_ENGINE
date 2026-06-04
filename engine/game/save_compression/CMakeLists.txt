# engine/game/save_compression — cd::game_save_compression (Phase 661/749)
#
# Sprint-1 (Phase 661): RLE compress/decompress for save-game byte streams.
# Sprint-2 (Phase 749): LZ4 path via vcpkg / FetchContent fallback.
#
# Naming rationale: the library is called save_compression rather than save_v2
# because it does NOT replace SaveSystem.  Callers compress their byte blob
# before passing it to SaveSystem::save() and decompress after
# SaveSystem::load() returns.  The split keeps I/O and codec concerns
# orthogonal and enables codec swap without touching slot-management machinery.
#
# LZ4 dependency resolution (3-tier, mirrors top-level GTest pattern):
#   Tier 1: vcpkg find_package(lz4 CONFIG)  — canonical CI path
#   Tier 2: FetchContent from github.com/lz4/lz4 tag v1.10.0
#   If neither is available, library still compiles but compress_lz4 /
#   decompress_lz4 are compiled out (CD_SAVE_COMPRESSION_HAS_LZ4=0).
#
# Dependencies (CLAUDE.md §7): cd::core + optional lz4.
# Sits at the bottom of the gameplay DAG so non-save callers (replay headers,
# telemetry snapshots) can use it without pulling in slot-management machinery.

include(FetchContent)

# --- LZ4 dependency (Tier 1: vcpkg, Tier 2: FetchContent) -----------------
set(_CD_SC_LZ4_TARGET "")

find_package(lz4 CONFIG QUIET)
if(lz4_FOUND AND TARGET lz4::lz4)
  message(STATUS "[cd] save_compression: LZ4 via vcpkg/find_package (${lz4_VERSION})")
  set(_CD_SC_LZ4_TARGET lz4::lz4)
else()
  message(STATUS "[cd] save_compression: LZ4 via FetchContent (v1.10.0)")
  # lz4's CMakeLists lives in the build_cmake subdirectory of the repo.
  # We use FetchContent with SOURCE_SUBDIR to point CMake at the right place.
  FetchContent_Declare(
    lz4_fetch
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.10.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  build/cmake
    EXCLUDE_FROM_ALL
  )
  FetchContent_GetProperties(lz4_fetch)
  if(NOT lz4_fetch_POPULATED)
    FetchContent_MakeAvailable(lz4_fetch)
  endif()
  # The FetchContent build exposes target `lz4_static` (or `lz4_shared`).
  if(TARGET lz4_static)
    if(NOT TARGET lz4::lz4)
      add_library(lz4::lz4 ALIAS lz4_static)
    endif()
    set(_CD_SC_LZ4_TARGET lz4::lz4)
    # Suppress 3rd-party warnings on lz4_static
    if(TARGET lz4_static)
      get_target_property(_lz4_type lz4_static TYPE)
      if(NOT _lz4_type STREQUAL "INTERFACE_LIBRARY")
        if(MSVC)
          target_compile_options(lz4_static PRIVATE /W0)
        else()
          target_compile_options(lz4_static PRIVATE -w)
        endif()
      endif()
    endif()
  elseif(TARGET lz4::lz4)
    set(_CD_SC_LZ4_TARGET lz4::lz4)
  endif()
endif()

if(_CD_SC_LZ4_TARGET)
  message(STATUS "[cd] save_compression: LZ4 enabled (${_CD_SC_LZ4_TARGET})")
  set(_CD_SC_HAS_LZ4 1)
else()
  message(WARNING "[cd] save_compression: LZ4 NOT available — compress_lz4/decompress_lz4 compiled out")
  set(_CD_SC_HAS_LZ4 0)
endif()

# ---------------------------------------------------------------------------

cd_add_library(game_save_compression
  TYPE STATIC
  SOURCES
    src/SaveCompression.cpp
  PUBLIC_DEPS
    cd::core
)

# Propagate LZ4 availability as a compile definition so the header/source
# can #ifdef-guard the LZ4 API surface.
target_compile_definitions(cd_game_save_compression
  PUBLIC  CD_SAVE_COMPRESSION_HAS_LZ4=${_CD_SC_HAS_LZ4}
)

if(_CD_SC_LZ4_TARGET)
  target_link_libraries(cd_game_save_compression PRIVATE ${_CD_SC_LZ4_TARGET})
  # Expose lz4 includes as SYSTEM so our -Wpedantic / -Wconversion flags
  # do not fire on lz4.h internals.
  target_include_directories(cd_game_save_compression SYSTEM PRIVATE
    $<TARGET_PROPERTY:${_CD_SC_LZ4_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
  )
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
