# CHROMODYNAMIC Emscripten Toolchain (CMake)
# Phase 1 Design: Stub for WebAssembly cross-compilation
# Status: Configuration template (pre-integration T4.5)
#
# Usage:
#   cmake --preset ninja-web-debug -DCMAKE_TOOLCHAIN_FILE=CMakeModules/emscripten.cmake
#
# Environment:
#   - EMSDK: Path to Emscripten SDK installation
#   - EMSCRIPTEN: Path to Emscripten runtime (set by EMSDK or manually)
#
# Reference:
#   - https://emscripten.org/docs/compiling/Building-Projects.html

# Validate Emscripten environment
if(NOT DEFINED EMSCRIPTEN_PREFIX AND NOT DEFINED ENV{EMSCRIPTEN})
    message(WARNING "Emscripten not found. Set EMSDK environment variable or EMSCRIPTEN_PREFIX.")
endif()

set(EMSCRIPTEN_PREFIX "$ENV{EMSCRIPTEN}" CACHE PATH "Emscripten installation path")
if(NOT EMSCRIPTEN_PREFIX)
    set(EMSCRIPTEN_PREFIX "$ENV{EMSDK}/upstream/emscripten")
endif()

# System identification
set(CMAKE_SYSTEM_NAME Emscripten CACHE STRING "Emscripten system name")
set(CMAKE_SYSTEM_VERSION 1 CACHE STRING "Emscripten system version")

# Compiler detection: emcc (C) and em++ (C++)
find_program(
    CMAKE_C_COMPILER
    NAMES emcc
    HINTS "${EMSCRIPTEN_PREFIX}"
    NO_DEFAULT_PATH
    REQUIRED
)

find_program(
    CMAKE_CXX_COMPILER
    NAMES em++
    HINTS "${EMSCRIPTEN_PREFIX}"
    NO_DEFAULT_PATH
    REQUIRED
)

find_program(
    CMAKE_AR
    NAMES emar
    HINTS "${EMSCRIPTEN_PREFIX}"
    NO_DEFAULT_PATH
    REQUIRED
)

find_program(
    CMAKE_RANLIB
    NAMES emranlib
    HINTS "${EMSCRIPTEN_PREFIX}"
    NO_DEFAULT_PATH
    REQUIRED
)

# Force static library linking (WASM modules link-only)
set(CMAKE_AR "${CMAKE_AR}" CACHE FILEPATH "Emscripten archiver")
set(CMAKE_RANLIB "${CMAKE_RANLIB}" CACHE FILEPATH "Emscripten ranlib")

# Disable shared libraries for Emscripten
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Emscripten: no shared libs" FORCE)

# Common Emscripten flags
set(COMMON_EMSCRIPTEN_FLAGS
    -fPIC
    -sPIC
    -sWASM=1
    -sWASM_BIGINT=1
    -sFILESYSTEM=1
)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_EMSCRIPTEN_FLAGS}" CACHE STRING "Emscripten C flags")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_EMSCRIPTEN_FLAGS}" CACHE STRING "Emscripten CXX flags")

# Linker flags (stub; executable linking happens in platform/web/CMakeLists.txt)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COMMON_EMSCRIPTEN_FLAGS}" CACHE STRING "Emscripten linker flags")

# Skip compiler checks (Emscripten cross-compiler behaves differently)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

# Disable features not supported in WASM
set(CMAKE_HAVE_PTHREAD_H FALSE CACHE BOOL "No pthread in WASM")

# Platform-specific defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Default to Debug for Emscripten")
endif()

message(STATUS "Emscripten Toolchain:")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  WASM output target")
message(STATUS "  Filesystem: IDBFS (post-T4.5)")
