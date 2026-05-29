# engine/render/spirv_cross_glue — cd::spirv_cross_glue (B-infra1)
#
# Cross-compiles SPIR-V binaries to HLSL (D3D12), MSL (Metal), or GLSL
# (round-trip / OpenGL). This is the canonical translation layer that unblocks
# the M4 D3D12 parity sprint: Vulkan-style GLSL shaders authored once, emitted
# as DXIL-compatible HLSL via SPIRV-Cross, then fed to DXC.
#
# Dependency resolution (same 3-tier pattern as cd::shader / glslang):
#   1. vcpkg find_package (CI canonical when CD_DISABLE_VCPKG=OFF)
#   2. FetchContent at sdk-1.3.268.0 (offline-friendly; always available)
#
# CMake targets exposed by SPIRV-Cross:
#   spirv-cross-core   — parser + IR
#   spirv-cross-glsl   — GLSL backend
#   spirv-cross-hlsl   — HLSL backend
#   spirv-cross-msl    — Metal Shading Language backend
#
# DAG position: cd_spirv_cross_glue → cd::core (foundation only)
# No dependency on cd::shader, cd::rhi, or any higher layer.

include(FetchContent)

# --- SPIRV-Cross resolution ---
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED               OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC               ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI                  OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS         OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL          ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL          ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL           ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP           OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT       OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API         OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL          OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL         ON  CACHE BOOL "" FORCE)

# Tier 1: vcpkg (only when toolchain is active)
find_package(spirv_cross_core CONFIG QUIET)
if(spirv_cross_core_FOUND)
  message(STATUS "[cd] spirv_cross: vcpkg/find_package")
  set(_CD_SPIRV_CROSS_VIA_VCPKG TRUE)
else()
  # Tier 2: FetchContent at the Khronos SDK 1.3.268 tag
  message(STATUS "[cd] spirv_cross: FetchContent (vulkan-sdk-1.3.268.0)")
  FetchContent_Declare(
    SPIRV-Cross
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
    GIT_TAG        vulkan-sdk-1.3.268.0
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(SPIRV-Cross)
  set(_CD_SPIRV_CROSS_VIA_VCPKG FALSE)
endif()

# Silence vendor warnings on SPIRV-Cross targets
foreach(_t spirv-cross-core spirv-cross-glsl spirv-cross-hlsl spirv-cross-msl
           spirv-cross-cpp spirv-cross-reflect spirv-cross-util)
  if(TARGET ${_t})
    if(MSVC)
      target_compile_options(${_t} PRIVATE /W0)
    else()
      target_compile_options(${_t} PRIVATE -w)
    endif()
  endif()
endforeach()

# vcpkg uses namespaced targets (spirv-cross-core::spirv-cross-core);
# FetchContent uses plain targets (spirv-cross-core). Normalise here.
if(_CD_SPIRV_CROSS_VIA_VCPKG)
  set(_SPIRV_CORE  spirv-cross-core::spirv-cross-core)
  set(_SPIRV_GLSL  spirv-cross-glsl::spirv-cross-glsl)
  set(_SPIRV_HLSL  spirv-cross-hlsl::spirv-cross-hlsl)
  set(_SPIRV_MSL   spirv-cross-msl::spirv-cross-msl)
else()
  set(_SPIRV_CORE  spirv-cross-core)
  set(_SPIRV_GLSL  spirv-cross-glsl)
  set(_SPIRV_HLSL  spirv-cross-hlsl)
  set(_SPIRV_MSL   spirv-cross-msl)
endif()

# --- Library ---
cd_add_library(spirv_cross_glue
  TYPE STATIC
  EXCLUDE_FROM_INSTALL   # vendored SPIRV-Cross via FetchContent
  SOURCES
    src/Translate.cpp
  PUBLIC_DEPS
    cd::core
)

target_link_libraries(cd_spirv_cross_glue
  PRIVATE
    ${_SPIRV_CORE}
    ${_SPIRV_GLSL}
    ${_SPIRV_HLSL}
    ${_SPIRV_MSL}
)

# Mark SPIRV-Cross includes SYSTEM so their headers don't trigger our
# strict warning set (-Wall -Werror) on included paths.
foreach(_spirv_target ${_SPIRV_CORE} ${_SPIRV_GLSL} ${_SPIRV_HLSL} ${_SPIRV_MSL})
  if(TARGET ${_spirv_target})
    cd_mark_target_system_includes(${_spirv_target})
  endif()
endforeach()

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
