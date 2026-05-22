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

# bc7enc — single-file BC7 encoder by richgel999, MIT. We pull master
# (the repo is small and stable) and compile bc7enc.c into our library
# directly. No CMake hook from upstream; we list the .c manually.
FetchContent_Declare(
  bc7enc
  GIT_REPOSITORY https://github.com/richgel999/bc7enc.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(stb_image bc7enc)

# bc7enc's upstream CMakeLists.txt registers a `bc7enc` demo executable
# that link-checks against POSIX libm (`-lm.lib` on lld-link → fatal).
# We never run their CLI; only the bc7enc.c TU is consumed via
# target_sources below. Quarantine the upstream exe target.
if(TARGET bc7enc)
  set_target_properties(bc7enc PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

cd_add_library(asset_image
  TYPE STATIC
  SOURCES
    src/Image.cpp
    src/Bc7Compress.cpp
  PUBLIC_DEPS
    cd::core
)

# stb + bc7enc headers as SYSTEM PRIVATE so their warnings never trip
# our -Werror policy. The pragma push/pop inside each .cpp covers the
# remaining macro-expanded warnings that survive the SYSTEM downgrade.
target_include_directories(cd_asset_image SYSTEM PRIVATE
  ${stb_image_SOURCE_DIR}
  ${bc7enc_SOURCE_DIR}
)

# bc7enc ships a .c file we have to compile ourselves (no upstream CMake).
# Adding it directly to cd_asset_image's sources keeps the lib self-contained.
target_sources(cd_asset_image PRIVATE ${bc7enc_SOURCE_DIR}/bc7enc.c)
# bc7enc.c has -Wunused-function (static helpers) + the usual C-style-cast
# noise. SYSTEM includes don't help for a .c file we compile directly, so
# we explicitly disable warnings for THIS source file. clang-cl uses Unix
# warning flags despite the MSVC compatibility veneer, so the MSVC branch
# only fires for genuine cl.exe.
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_source_files_properties(${bc7enc_SOURCE_DIR}/bc7enc.c PROPERTIES
    COMPILE_OPTIONS "/wd4100;/wd4244;/wd4267;/wd4505;/wd4996")
else()
  # Disable ALL warnings for the vendored TU. -w on Clang/GCC silences
  # every group; -Werror is also dropped via -Wno-error=* fallback so
  # we don't leak a single warning into the project's strict policy.
  set_source_files_properties(${bc7enc_SOURCE_DIR}/bc7enc.c PROPERTIES
    COMPILE_OPTIONS "-w")
endif()

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
