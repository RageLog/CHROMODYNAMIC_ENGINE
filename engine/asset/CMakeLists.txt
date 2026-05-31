# engine/asset — cd::asset + all sub-libraries (phase419)
#
# Layout:
#   include/cd/asset/            — public interface headers + sub-lib headers
#     gltf/ image/ json/ ktx2/ obj/ pak/ streaming/ cdmesh/ cdtex/ wav/
#   src/<sub>/                   — implementation files
#   tests/<sub>/                 — per-sub-library tests
#   tests/test_asset.cpp         — interface-level tests
#
# Targets:
#   cd::asset           — INTERFACE abstraction (IAssetLoader etc.)
#   cd::asset_gltf      — glTF 2.0 importer (tinygltf)
#   cd::asset_image     — PNG/JPG/TGA/BMP/HDR decoder (stb_image + bc7enc)
#   cd::asset_json      — hand-rolled JSON subset parser
#   cd::asset_ktx2      — KTX2 BC7/RGBA8 reader
#   cd::asset_obj       — Wavefront .obj loader
#   cd::asset_pak       — single-file bundle format (header-only)
#   cd::asset_streaming — priority-queue async I/O (header-only)
#   cd::asset_cdmesh    — cooked binary mesh (.cdmesh)
#   cd::asset_cdtex     — cooked BC7 texture (.cdtex)
#   cd::asset_wav       — RIFF/WAVE PCM loader

# ── cd::asset (INTERFACE — header-only abstraction) ────────────────────────
cd_add_library(asset
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::io cd::vfs cd::math
)

# ── cd::asset_gltf ─────────────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
  tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG        v2.9.3
  GIT_SHALLOW    TRUE
)

set(TINYGLTF_HEADER_ONLY OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tinygltf)

if(TARGET tinygltf)
  if(MSVC)
    target_compile_options(tinygltf PRIVATE /W0)
  else()
    target_compile_options(tinygltf PRIVATE -w)
  endif()
endif()

cd_add_library(asset_gltf
  TYPE STATIC
  SOURCES
    src/gltf/GltfLoader.cpp
    src/gltf/SceneLoader.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::anim
  PRIVATE_DEPS
    cd::asset
)

target_include_directories(cd_asset_gltf SYSTEM PRIVATE
  ${tinygltf_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(cd_asset_gltf PRIVATE
    /wd4100 /wd4189 /wd4244 /wd4267 /wd4456 /wd4458 /wd4505 /wd4702 /wd4996
  )
else()
  target_compile_options(cd_asset_gltf PRIVATE
    -Wno-error=conversion
    -Wno-error=shadow
    -Wno-error=unused-parameter
    -Wno-error=unused-variable
    -Wno-error=old-style-cast
    -Wno-error=deprecated-literal-operator
    -Wno-error=cast-align
    -Wno-error=double-promotion
    -Wno-error=sign-conversion
    -Wno-error=missing-declarations
    -Wno-error=format
  )
endif()

# ── cd::asset_image ────────────────────────────────────────────────────────
FetchContent_Declare(
  stb_image
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        f0569113c93ad095470c54bf34a17b36646bbbb5
  GIT_SHALLOW    FALSE
)

FetchContent_Declare(
  bc7enc
  GIT_REPOSITORY https://github.com/richgel999/bc7enc.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(stb_image bc7enc)

if(TARGET bc7enc)
  set_target_properties(bc7enc PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

cd_add_library(asset_image
  TYPE STATIC
  SOURCES
    src/image/Image.cpp
    src/image/ImageWrite.cpp
    src/image/Bc7Compress.cpp
  PUBLIC_DEPS
    cd::core
)

target_include_directories(cd_asset_image SYSTEM PRIVATE
  ${stb_image_SOURCE_DIR}
  ${bc7enc_SOURCE_DIR}
)

target_sources(cd_asset_image PRIVATE ${bc7enc_SOURCE_DIR}/bc7enc.c)

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_source_files_properties(${bc7enc_SOURCE_DIR}/bc7enc.c PROPERTIES
    COMPILE_OPTIONS "/wd4100;/wd4244;/wd4267;/wd4505;/wd4996")
else()
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

# ── cd::asset_json ─────────────────────────────────────────────────────────
cd_add_library(asset_json
  TYPE STATIC
  SOURCES
    src/json/Json.cpp
  PUBLIC_DEPS
    cd::core
)

# ── cd::asset_ktx2 ─────────────────────────────────────────────────────────
cd_add_library(asset_ktx2
  TYPE STATIC
  SOURCES
    src/ktx2/Ktx2.cpp
  PUBLIC_DEPS
    cd::core
)

# ── cd::asset_obj ──────────────────────────────────────────────────────────
cd_add_library(asset_obj
  TYPE STATIC
  SOURCES
    src/obj/ObjLoader.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
)

# ── cd::asset_pak (header-only) ────────────────────────────────────────────
cd_add_library(asset_pak
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
)

# ── cd::asset_streaming (header-only) ──────────────────────────────────────
cd_add_library(asset_streaming
  TYPE INTERFACE
  PUBLIC_DEPS cd::core
)

# ── cd::asset_cdmesh ───────────────────────────────────────────────────────
cd_add_library(asset_cdmesh
  TYPE STATIC
  SOURCES
    src/cdmesh/CdMesh.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
)

# ── cd::asset_cdtex ────────────────────────────────────────────────────────
cd_add_library(asset_cdtex
  TYPE STATIC
  SOURCES
    src/cdtex/CdTex.cpp
  PUBLIC_DEPS
    cd::core
)

# ── cd::asset_wav ──────────────────────────────────────────────────────────
cd_add_library(asset_wav
  TYPE STATIC
  SOURCES
    src/wav/Wav.cpp
  PUBLIC_DEPS
    cd::core
)

# ── cd::asset_scene_streamer ──────────────────────────────────────────────────
add_subdirectory(scene_streamer)

# ── Tests ──────────────────────────────────────────────────────────────────
if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
  add_subdirectory(tests/gltf)
  add_subdirectory(tests/image)
  add_subdirectory(tests/json)
  add_subdirectory(tests/ktx2)
  add_subdirectory(tests/obj)
  add_subdirectory(tests/pak)
  add_subdirectory(tests/streaming)
  add_subdirectory(tests/cdmesh)
  add_subdirectory(tests/cdtex)
  add_subdirectory(tests/wav)
endif()
