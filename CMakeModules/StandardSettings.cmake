# =============================================================================
# BUILD ACCELERATION
# =============================================================================

# --- vcpkg Binary Cache ---
# Set VCPKG_BINARY_SOURCES env var to activate. Examples:
#   File-based (local):  export VCPKG_BINARY_SOURCES="clear;files,C:/vcpkg-cache,readwrite"
#   NuGet (CI):          export VCPKG_BINARY_SOURCES="clear;nuget,<feedUrl>,readwrite"
# Without binary cache, vcpkg recompiles all dependencies on clean builds.
# See: https://learn.microsoft.com/en-us/vcpkg/users/binarycaching
if(NOT DEFINED ENV{VCPKG_BINARY_SOURCES})
    message(STATUS "[vcpkg] VCPKG_BINARY_SOURCES not set — dependency cache disabled. "
        "Set it to e.g. 'clear;files,C:/vcpkg-cache,readwrite' for faster clean builds.")
endif()

# --- sccache / ccache auto-detection (compiler cache: 5-10x faster incremental) ---
# Priority: sccache > ccache (sccache supports distributed/cloud caching on Windows)
find_program(SCCACHE_PROGRAM sccache)
find_program(CCACHE_PROGRAM ccache)

if(SCCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
    message(STATUS "[Build] sccache enabled: ${SCCACHE_PROGRAM}")
elseif(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "[Build] ccache enabled: ${CCACHE_PROGRAM}")
else()
    message(STATUS "[Build] No compiler cache found. Install sccache for faster rebuilds: cargo install sccache")
endif()

if(NOT WIN32)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# --- Unity Build (combine source files for faster compilation) ---
option(ENABLE_UNITY_BUILD "Enable Unity Build for faster compilation" ON)

if(ENABLE_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
    message(STATUS "[Build] Unity Build enabled (batch size: 16)")
endif()

# =============================================================================
# OUTPUT DIRECTORIES
# =============================================================================
if(DFH_OUTPUT_DIR)
    set(DFH_OUTPUT_ROOT "${DFH_OUTPUT_DIR}")
elseif(DEFINED ENV{DFH_OUTPUT_DIR})
    set(DFH_OUTPUT_ROOT "$ENV{DFH_OUTPUT_DIR}")
else()
    set(DFH_OUTPUT_ROOT "${CMAKE_SOURCE_DIR}/outputs")
endif()
set(DFH_OUTPUT_ROOT "${DFH_OUTPUT_ROOT}" CACHE INTERNAL "DtForHil output root directory" FORCE)

if(CMAKE_CONFIGURATION_TYPES)
    # Multi-config generators: outputs/<config>/{bin,lib,symbols}
    foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
        string(TOLOWER ${CONFIG_TYPE} CONFIG_TYPE_LOWER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${DFH_OUTPUT_ROOT}/${CONFIG_TYPE_LOWER}/bin)
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${DFH_OUTPUT_ROOT}/${CONFIG_TYPE_LOWER}/lib)
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${DFH_OUTPUT_ROOT}/${CONFIG_TYPE_LOWER}/lib)
        if(MSVC)
            set(CMAKE_PDB_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${DFH_OUTPUT_ROOT}/${CONFIG_TYPE_LOWER}/symbols)
            set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${DFH_OUTPUT_ROOT}/${CONFIG_TYPE_LOWER}/symbols)
        endif()
    endforeach()

    # Fallback for generators that query non-config-specific output variables.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DFH_OUTPUT_ROOT}/debug/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${DFH_OUTPUT_ROOT}/debug/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${DFH_OUTPUT_ROOT}/debug/lib)
    if(MSVC)
        set(CMAKE_PDB_OUTPUT_DIRECTORY ${DFH_OUTPUT_ROOT}/debug/symbols)
        set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY ${DFH_OUTPUT_ROOT}/debug/symbols)
    endif()

    set(OUTPUT_DIR "${DFH_OUTPUT_ROOT}/debug")
else()
    # Single-config generators: outputs/<build_type>/{bin,lib,symbols}
    if(CMAKE_BUILD_TYPE)
        string(TOLOWER "${CMAKE_BUILD_TYPE}" DFH_BUILD_TYPE_LOWER)
    else()
        set(DFH_BUILD_TYPE_LOWER "release")
    endif()

    set(OUTPUT_DIR "${DFH_OUTPUT_ROOT}/${DFH_BUILD_TYPE_LOWER}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
    if(MSVC)
        set(CMAKE_PDB_OUTPUT_DIRECTORY ${OUTPUT_DIR}/symbols)
        set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY ${OUTPUT_DIR}/symbols)
    endif()
endif()

# =============================================================================
# COMPILER WARNINGS (Cross-compiler compatible)
# =============================================================================
add_library(project_warnings INTERFACE)

if(WIN32)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
endif()

if(MSVC)
    target_compile_options(project_warnings INTERFACE
        /W4 # High warning level
        /permissive- # Strict conformance
        /utf-8 # UTF-8 source files
        /Zc:__cplusplus # Correct __cplusplus macro
        /EHsc # Exception handling

        # Suppress DLL interface warnings for STL types (safe when using same runtime)
        /wd4251 # 'type' needs dll-interface for clients of class
        /wd4275 # non dll-interface class used as base for dll-interface class
    )

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(project_warnings INTERFACE
            /Zc:preprocessor # Standard-conforming preprocessor
            /MP # Multi-processor compilation
        )
    endif()

    # /WX: warnings-as-errors always (CI and local). Zero-warning policy.
    target_compile_options(project_warnings INTERFACE /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(project_warnings INTERFACE
        -Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Wcast-align -Wunused -Woverloaded-virtual -Wpedantic
        -Wconversion -Wsign-conversion -Wformat=2
        # -Werror: warnings-as-errors always (CI and local). Zero-warning policy.
        -Werror
    )
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    message(STATUS "[Build] Defaulting to Release build")
endif()

# Compiler-specific optimization flags
if(NOT MSVC)
    # GCC/Clang flags
    # -fno-omit-frame-pointer: keep frame pointers so profilers (perf/VTune) work in Release
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG -fno-omit-frame-pointer")
else()
    # MSVC Release: /O2 /Oi (intrinsics) /Gy (function-level linking for /OPT:REF)
    # /GL is added separately below only when LTO is active to avoid duplicate symbol issues.
    # /MD (dynamic CRT) is CMake default for MSVC; /MDd for Debug — kept as-is.
    add_compile_options(
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/Oi>
        $<$<CONFIG:RelWithDebInfo>:/Gy>
    )
    add_link_options(
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:RelWithDebInfo>:/OPT:REF>
        $<$<CONFIG:RelWithDebInfo>:/OPT:ICF>
    )
endif()

# --- LTO (Link-Time Optimization) — Release only ---
# Debug keeps LTO OFF to preserve ASAN/UBSAN compatibility and fast incremental builds.
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_output)

if(_ipo_supported)
    # Apply LTO only to Release/RelWithDebInfo configurations; never Debug.
    # CMake's INTERPROCEDURAL_OPTIMIZATION_<CONFIG> allows per-config control.
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
    message(STATUS "[Build] LTO enabled for Release/RelWithDebInfo")
else()
    message(STATUS "[Build] LTO not supported by current toolchain: ${_ipo_output}")
endif()

message(STATUS "[Build] Build type: ${CMAKE_BUILD_TYPE}")
