# engine/world/audio_spatial — Phase 693/751
# cd::audio::spatial — 3D positional audio sidecar to cd::audio.
#
# Sprint-1 (Phase 693): ITD/ILD panning + 1/r distance attenuation + Doppler.
# Sprint-2 (Phase 751): HrtfMixer — real HRTF convolution via OpenAL Soft.
#
# Sidecar relationship:
#   cd::audio            — 2D mixer / playback backend (WASAPI / CoreAudio / ALSA)
#   cd::audio::spatial   — WHERE sounds come from (positional math + HRTF convolution)
#
# OpenAL Soft dependency resolution (3-tier, mirrors save_compression/lz4 pattern):
#   Tier 1: vcpkg find_package(OpenAL CONFIG)  — canonical CI path
#   Tier 2: FetchContent from github.com/kcat/openal-soft tag 1.24.2
#   If neither resolves: CD_AUDIO_SPATIAL_HAS_OPENAL=0 — HrtfMixer compiles as
#   a no-op stub and Sprint-1 SpatialMixer remains the operative path.
#
# LGPL note: openal-soft is LGPL-2.0-or-later.  Linked as a SHARED library
# (vcpkg default on Windows) so CHROMODYNAMIC is not statically affected.
# See docs/ADR/ADR-20260605-openal-soft-lgpl.md.

include(FetchContent)

# ---------------------------------------------------------------------------
# OpenAL Soft dependency (Tier 1: vcpkg, Tier 2: FetchContent)
# ---------------------------------------------------------------------------
set(_CD_AS_OPENAL_TARGET "")

find_package(OpenAL CONFIG QUIET)
if(OpenAL_FOUND AND TARGET OpenAL::OpenAL)
  message(STATUS "[cd] audio_spatial: OpenAL via vcpkg/find_package (${OpenAL_VERSION})")
  set(_CD_AS_OPENAL_TARGET OpenAL::OpenAL)
else()
  message(STATUS "[cd] audio_spatial: OpenAL not found via vcpkg — trying FetchContent (1.24.2)")
  FetchContent_Declare(
    openal_soft_fetch
    GIT_REPOSITORY https://github.com/kcat/openal-soft.git
    GIT_TAG        1.24.2
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
  )
  set(ALSOFT_EXAMPLES   OFF CACHE BOOL "" FORCE)
  set(ALSOFT_TESTS      OFF CACHE BOOL "" FORCE)
  set(ALSOFT_UTILS      OFF CACHE BOOL "" FORCE)
  set(ALSOFT_INSTALL    OFF CACHE BOOL "" FORCE)
  set(LIBTYPE           STATIC CACHE STRING "" FORCE)
  FetchContent_GetProperties(openal_soft_fetch)
  if(NOT openal_soft_fetch_POPULATED)
    FetchContent_MakeAvailable(openal_soft_fetch)
  endif()
  # openal-soft FetchContent exposes target "OpenAL" (no namespace).
  # We alias it to match the vcpkg target name.
  if(TARGET OpenAL AND NOT TARGET OpenAL::OpenAL)
    add_library(OpenAL::OpenAL ALIAS OpenAL)
    set(_CD_AS_OPENAL_TARGET OpenAL::OpenAL)
  elseif(TARGET OpenAL::OpenAL)
    set(_CD_AS_OPENAL_TARGET OpenAL::OpenAL)
  elseif(TARGET openal)
    if(NOT TARGET OpenAL::OpenAL)
      add_library(OpenAL::OpenAL ALIAS openal)
    endif()
    set(_CD_AS_OPENAL_TARGET OpenAL::OpenAL)
  endif()

  if(_CD_AS_OPENAL_TARGET)
    # Suppress 3rd-party warnings on the fetched target
    get_target_property(_oal_type OpenAL TYPE)
    if(_oal_type AND NOT _oal_type STREQUAL "INTERFACE_LIBRARY")
      if(MSVC)
        target_compile_options(OpenAL PRIVATE /W0)
      else()
        target_compile_options(OpenAL PRIVATE -w)
      endif()
    endif()
  endif()
endif()

if(_CD_AS_OPENAL_TARGET)
  message(STATUS "[cd] audio_spatial: OpenAL HRTF enabled (${_CD_AS_OPENAL_TARGET})")
  set(_CD_AS_HAS_OPENAL 1)
else()
  message(WARNING "[cd] audio_spatial: OpenAL NOT available — HrtfMixer compiled as no-op stub; Sprint-1 SpatialMixer is operative path")
  set(_CD_AS_HAS_OPENAL 0)
endif()

# ---------------------------------------------------------------------------

cd_add_library(audio_spatial
  TYPE STATIC
  SOURCES
    src/AudioSpatial.cpp
    src/HrtfMixer.cpp
  PUBLIC_DEPS cd::core
)

# Propagate OpenAL availability as a compile definition so the header/source
# can #if-guard the OpenAL API surface.
target_compile_definitions(cd_audio_spatial
  PUBLIC  CD_AUDIO_SPATIAL_HAS_OPENAL=${_CD_AS_HAS_OPENAL}
)

if(_CD_AS_OPENAL_TARGET)
  target_link_libraries(cd_audio_spatial PRIVATE ${_CD_AS_OPENAL_TARGET})
  # Expose OpenAL includes as SYSTEM so our -Wpedantic / -Wconversion flags
  # do not fire on AL/ internals.
  target_include_directories(cd_audio_spatial SYSTEM PRIVATE
    $<TARGET_PROPERTY:${_CD_AS_OPENAL_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
  )
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
