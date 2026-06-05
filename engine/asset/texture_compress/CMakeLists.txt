# engine/asset/texture_compress — cd::asset_texture_compress (Phase 650 / 750)
#
# Offline texture compression pipeline: compress raw RGBA8 pixels into
# GPU-native block-compressed formats for desktop (BC1/BC3/BC5/BC7) and
# mobile (ASTC 4×4 / 8×8).
#
# Sprint-1 (Phase 650): API surface + BC1 stub encoder.
# Sprint-2 (Phase 750): real BC7 via bc7enc_rdo (FetchContent) + ASTC via
#   astc-encoder/ARM (FetchContent). Both encoders are optional: if network
#   or cmake fetch fails the library still compiles with Sprint-1 BC1 only.
#
# Encoder dependency resolution (3-tier, mirrors save_compression/lz4):
#   bc7enc_rdo:
#     Tier 1: vcpkg  — bc7enc is NOT in the vcpkg registry; Tier 1 skipped.
#     Tier 2: FetchContent from github.com/richgel999/bc7enc_rdo (HEAD)
#     If unavailable: encode_bc7() returns nullopt; CD_TC_HAS_BC7ENC=0.
#   astc-encoder (ARM):
#     Tier 1: vcpkg  — astc-encoder is NOT in the vcpkg registry; Tier 1 skipped.
#     Tier 2: FetchContent from github.com/ARM-software/astc-encoder tag 4.7.0
#     If unavailable: encode_astc() returns nullopt; CD_TC_HAS_ASTCENC=0.
#
# Moment: game ships with 8 GB → 2 GB texture footprint; mobile build runs on
# devices without 8 GB VRAM.

include(FetchContent)

# ============================================================================
# bc7enc_rdo — BC7 encoder (Tier 2: FetchContent, no CMake library target)
# ============================================================================
# bc7enc_rdo's CMakeLists only produces an executable, not a library target.
# We fetch the sources and manually create a static library from bc7enc.cpp
# (the only file needed for the block-encoding API we use).
# ============================================================================

set(_CD_TC_BC7ENC_TARGET "")
set(_CD_TC_HAS_BC7ENC 0)

FetchContent_Declare(
  bc7enc_rdo
  GIT_REPOSITORY https://github.com/richgel999/bc7enc_rdo.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_GetProperties(bc7enc_rdo)
if(NOT bc7enc_rdo_POPULATED)
  message(STATUS "[cd] texture_compress: fetching bc7enc_rdo ...")
  # bc7enc_rdo's CMakeLists only produces an executable (not a library target).
  # We must use FetchContent_Populate (NOT MakeAvailable) so we can create our
  # own bc7enc_rdo_lib target from bc7enc.cpp without building the executable.
  # CMP0169 OLD: allow single-arg FetchContent_Populate for this special case.
  if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
  endif()
  FetchContent_Populate(bc7enc_rdo)
endif()

if(bc7enc_rdo_POPULATED)
  set(_bc7enc_src "${bc7enc_rdo_SOURCE_DIR}/bc7enc.cpp")
  set(_bc7enc_hdr "${bc7enc_rdo_SOURCE_DIR}/bc7enc.h")
  if(EXISTS "${_bc7enc_src}" AND EXISTS "${_bc7enc_hdr}")
    # Create a minimal static library: just bc7enc.cpp (block encoder/init).
    # bc7decomp, lodepng, test, etc. are NOT needed for our use-case.
    if(NOT TARGET bc7enc_rdo_lib)
      add_library(bc7enc_rdo_lib STATIC "${_bc7enc_src}")
      target_include_directories(bc7enc_rdo_lib PUBLIC "${bc7enc_rdo_SOURCE_DIR}")
      # Suppress 3rd-party compiler warnings
      if(MSVC)
        target_compile_options(bc7enc_rdo_lib PRIVATE /W0)
      else()
        target_compile_options(bc7enc_rdo_lib PRIVATE -w)
      endif()
      # bc7enc.cpp uses C-style code in C++ TU — needs /std:c++17 minimum but
      # avoids C++23 features; it compiles fine under any C++ standard.
      set_target_properties(bc7enc_rdo_lib PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED OFF   # soft: don't error if lower, it still builds
      )
    endif()
    set(_CD_TC_BC7ENC_TARGET bc7enc_rdo_lib)
    set(_CD_TC_HAS_BC7ENC 1)
    message(STATUS "[cd] texture_compress: BC7 encoder ENABLED (bc7enc_rdo)")
  else()
    message(WARNING "[cd] texture_compress: bc7enc_rdo fetched but sources missing — BC7 disabled")
  endif()
else()
  message(WARNING "[cd] texture_compress: bc7enc_rdo NOT available — encode_bc7 compiled out")
endif()

# ============================================================================
# astc-encoder (ARM) — ASTC 4×4 / 8×8 encoder (Tier 2: FetchContent)
# ============================================================================
# astc-encoder has proper CMake support. We select the ISA-none variant
# (portable, no SIMD) to avoid requiring AVX2/SSE4.1 at configure time.
# The astcenc-none-static target is the result.
# ============================================================================

set(_CD_TC_ASTCENC_TARGET "")
set(_CD_TC_HAS_ASTCENC 0)

FetchContent_Declare(
  astc_encoder
  GIT_REPOSITORY https://github.com/ARM-software/astc-encoder.git
  GIT_TAG        4.7.0
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_GetProperties(astc_encoder)
if(NOT astc_encoder_POPULATED)
  message(STATUS "[cd] texture_compress: fetching astc-encoder 4.7.0 ...")
  # Configure astc-encoder options before FetchContent_MakeAvailable
  set(ASTCENC_ISA_NONE  ON  CACHE BOOL "astcenc: portable (no SIMD)" FORCE)
  set(ASTCENC_ISA_SSE2  OFF CACHE BOOL "astcenc: SSE2 off"           FORCE)
  set(ASTCENC_ISA_SSE41 OFF CACHE BOOL "astcenc: SSE4.1 off"         FORCE)
  set(ASTCENC_ISA_AVX2  OFF CACHE BOOL "astcenc: AVX2 off"           FORCE)
  set(ASTCENC_ISA_NEON  OFF CACHE BOOL "astcenc: NEON off"           FORCE)
  set(ASTCENC_CLI       OFF CACHE BOOL "astcenc: no CLI tool"         FORCE)
  set(ASTCENC_SHAREDLIB OFF CACHE BOOL "astcenc: static only"        FORCE)
  set(ASTCENC_UNITTEST  OFF CACHE BOOL "astcenc: no unit tests"       FORCE)
  FetchContent_MakeAvailable(astc_encoder)
endif()

# astc-encoder creates target: astcenc-none-static
if(TARGET astcenc-none-static)
  set(_CD_TC_ASTCENC_TARGET astcenc-none-static)
  set(_CD_TC_HAS_ASTCENC 1)
  message(STATUS "[cd] texture_compress: ASTC encoder ENABLED (astcenc-none-static)")
  # Suppress 3rd-party warnings on the astcenc target
  get_target_property(_astcenc_type astcenc-none-static TYPE)
  if(NOT _astcenc_type STREQUAL "INTERFACE_LIBRARY")
    if(MSVC)
      target_compile_options(astcenc-none-static PRIVATE /W0)
    else()
      target_compile_options(astcenc-none-static PRIVATE -w)
    endif()
  endif()
else()
  message(WARNING "[cd] texture_compress: astcenc-none-static NOT available — encode_astc compiled out")
endif()

# ============================================================================
# cd::asset_texture_compress library
# ============================================================================

cd_add_library(asset_texture_compress
  TYPE STATIC
  SOURCES
    src/TextureCompress.cpp
  PUBLIC_DEPS
    cd::core
)

# Propagate availability as compile definitions so the source can #ifdef-guard.
target_compile_definitions(cd_asset_texture_compress
  PUBLIC
    CD_TC_HAS_BC7ENC=${_CD_TC_HAS_BC7ENC}
    CD_TC_HAS_ASTCENC=${_CD_TC_HAS_ASTCENC}
)

# Link encoders as PRIVATE (implementation detail; callers only see TextureCompress.hpp).
if(_CD_TC_BC7ENC_TARGET)
  target_link_libraries(cd_asset_texture_compress PRIVATE ${_CD_TC_BC7ENC_TARGET})
  # Expose bc7enc headers as SYSTEM PRIVATE so -Wpedantic/-Wconversion don't
  # fire on the 3rd-party headers when included from TextureCompress.cpp.
  target_include_directories(cd_asset_texture_compress SYSTEM PRIVATE
    $<TARGET_PROPERTY:${_CD_TC_BC7ENC_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
  )
endif()

if(_CD_TC_ASTCENC_TARGET)
  target_link_libraries(cd_asset_texture_compress PRIVATE ${_CD_TC_ASTCENC_TARGET})
  target_include_directories(cd_asset_texture_compress SYSTEM PRIVATE
    $<TARGET_PROPERTY:${_CD_TC_ASTCENC_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
  )
endif()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
