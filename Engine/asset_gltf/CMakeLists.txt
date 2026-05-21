# engine/asset_gltf — cd::asset_gltf
#
# glTF 2.0 importer. Wraps tinygltf (single-header MIT). tinygltf ships its
# own copies of stb_image (decode) and nlohmann_json (parser), so the only
# external requirement is the upstream repo at FetchContent time.
#
# This library is intentionally separate from cd::asset (INTERFACE-only) so
# the heavy parser deps stay opt-in: anyone linking cd::asset gets the loader
# interfaces but not tinygltf.

include(FetchContent)

# Pinned to a recent stable release that compiles cleanly on MSVC + clang-cl
# + GCC 13 with C++23. Newer releases routinely bump nlohmann_json which is
# the dep most likely to break under -Werror.
FetchContent_Declare(
  tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG        v2.9.3
  GIT_SHALLOW    TRUE
)

# Disable tinygltf's own build artefacts — we only want the headers.
set(TINYGLTF_HEADER_ONLY OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tinygltf)

# tinygltf adds itself as a target; suppress its warning noise so our -Werror
# policy does not bounce on third-party code.
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
    src/GltfLoader.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
  PRIVATE_DEPS
    cd::asset
)

# Inject tinygltf headers as a SYSTEM private include. The SYSTEM marker is
# the only reliable way to silence the parser-warning storm tinygltf (and
# its inlined nlohmann_json copy) triggers under our strict -Werror flag
# set — clang/gcc both demote diagnostics from system headers, and MSVC
# does the same via /external:I emitted by CMake's SYSTEM include. The
# pragma push/pop inside GltfLoader.cpp covers a few more corner cases
# (errors emitted from macros expanded outside the system header itself).
target_include_directories(cd_asset_gltf SYSTEM PRIVATE
  ${tinygltf_SOURCE_DIR}
)

# Belt-and-suspenders compile-option suppressions for the wrapper TU. Even
# with SYSTEM headers, code expanded from macros inside our own .cpp can
# still trip -Werror; these knock the remaining offenders out.
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
