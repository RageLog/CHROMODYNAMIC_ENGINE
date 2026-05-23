# CHROMODYNAMIC Engine — Top-level CMakeLists.txt
# Phase 2 Sprint S2.0 — Build System Bootstrap
# References: ADR-005 (Foundation), ADR-014 (CI/CD), ADR-016 (Vendor Matrix)

# Fixed, project-wide minimum. Keep this number HIGH and IDENTICAL to every
# other cmake_minimum_required() in the repo (CMakeModules/util.cmake,
# CMakeModules/VersionFromGit.cmake, Dependencies/CMakeLists.txt). CMake 4.x
# refuses VERSION < 3.5 as fatal; a low number anywhere in our own tree
# breaks the whole configure. 3.28 buys us BLOCK/ENDBLOCK, presets v8,
# and modern FetchContent without needing per-call workarounds.
cmake_minimum_required(VERSION 3.28)

# Policy-version floor for FetchContent'd third-party projects whose own
# `cmake_minimum_required(VERSION X)` is older than what CMake 4.x still
# accepts without complaint. Concretely the deps we pull right now:
#
#   tinygltf      VERSION 3.6     → emits "Compatibility with CMake < 3.10
#                                   will be removed" under CMake 4.x.
#   volk          VERSION 3.10    → silent at 3.10, future-proofed at 3.15.
#   vma           VERSION 3.15..  → already uses min..max syntax, fine.
#   googletest    VERSION 3.13    → silent.
#   glslang       (managed)       → silent.
#
# We pick the highest floor needed (3.10 today) and keep headroom by going
# to 3.15 — that buys silence for the deprecation cadence CMake follows
# (each major release bumps the warning threshold by ~5 minor versions).
# If a future dep emits "< 3.X will be removed", bump this one number and
# the warning disappears repo-wide.
#
# Note: CMAKE_POLICY_VERSION_MINIMUM is honoured by `cmake_minimum_required`
# itself — when the call's version is lower than this variable, the
# variable wins. It also propagates into add_subdirectory()'d projects,
# including FetchContent_MakeAvailable targets.
# FORCE is intentional: this floor is a build-system invariant, not a
# user-tunable knob. If we drop FORCE, a stale cache from an older
# configure (when the floor was 3.5) sticks around and the deprecation
# warnings reappear after every cmake_minimum_required bump in a vendored
# dep.
set(CMAKE_POLICY_VERSION_MINIMUM 3.15 CACHE STRING
    "Floor for vendored deps' cmake_minimum_required (silences <X deprecation warnings)" FORCE)

# --- Vcpkg toolchain (manifest mode) ---
# Set CD_DISABLE_VCPKG=ON (or run with `-DCD_DISABLE_VCPKG=ON`) to bypass vcpkg
# entirely and use vendored fallbacks (Dependencies/googletest, etc.).
option(CD_DISABLE_VCPKG "Bypass vcpkg toolchain; use vendored fallbacks only" OFF)
if(NOT CD_DISABLE_VCPKG)
  if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
  endif()
endif()
set(VCPKG_INSTALL_OPTIONS "--no-print-usage")

# --- Policies ---
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# --- C++ Standard ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Hint: Ninja recommendation ---
if(NOT CMAKE_GENERATOR MATCHES "Ninja")
  message(STATUS "[cd] Hint: use Ninja for faster builds: cmake --preset ninja-debug")
endif()

# --- Project ---
project(CHROMODYNAMIC
  VERSION 0.69.0
  DESCRIPTION "CHROMODYNAMIC — cross-platform, cross-API hybrid 2D+3D engine + framework"
  HOMEPAGE_URL "https://github.com/CRFLT/CHROMODYNAMIC"
  LANGUAGES CXX C
)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --- Output directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- Options (ADR-005 §A + ADR-016) ---
option(CD_BUILD_SHARED_LIBS  "Build engine libraries as shared (default static)" OFF)
option(CD_ENABLE_TESTING     "Enable unit tests (GoogleTest)"                    ON)
option(CD_ENABLE_SAMPLES     "Build sample applications"                         ON)
option(CD_ENABLE_WARNINGS    "Apply zero-warning compiler flags"                 ON)
option(CD_WARNINGS_AS_ERRORS "Treat warnings as errors"                          ON)
option(CD_ENABLE_PCH         "Enable Pre-Compiled Headers"                       ON)
option(CD_ENABLE_LTO         "Enable LTO/IPO for Release"                        OFF)
option(CD_USE_VENDORED_DEPS  "Allow vendored fallback for missing vcpkg deps"    ON)
option(CD_ENABLE_CLANG_TIDY  "Run clang-tidy as build step (Sprint S2.0+ stub)"  OFF)
option(CD_ENABLE_CLANG_FORMAT "Run clang-format as build step (Sprint S2.0+ stub)" OFF)
option(CD_ENABLE_DOXYGEN     "Generate Doxygen API documentation (Sprint S2.0+ stub)" OFF)
option(CD_ENABLE_UNITY_BUILD "Enable CMake unity build (faster compile)"         OFF)
option(CD_ENABLE_COVERAGE    "Enable code coverage instrumentation (GCC/Clang)"  OFF)
# Install pipeline — default ON for top-level builds, OFF for nested subdirectory builds.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(CD_ENABLE_INSTALL "Generate install() + find_package(CHROMODYNAMIC) artifacts" ON)
else()
  option(CD_ENABLE_INSTALL "Generate install() + find_package(CHROMODYNAMIC) artifacts" OFF)
endif()
# Legacy DfH-style alias kept for migration convenience: ENABLE_SANITIZERS=ON → asan
if(DEFINED ENABLE_SANITIZERS AND ENABLE_SANITIZERS AND CD_SANITIZER STREQUAL "none")
  set(CD_SANITIZER "asan" CACHE STRING "" FORCE)
endif()

# CMake unity build (opt-in)
if(CD_ENABLE_UNITY_BUILD)
  set(CMAKE_UNITY_BUILD ON)
endif()

# Test/sanitizer setup
if(CD_ENABLE_TESTING)
  enable_testing()
  include(CTest)
endif()

# --- CMake module path ---
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")

include(CDProject)             # cd_add_library, cd_add_test, cd_add_sample
include(CDWarnings)            # cd_apply_warnings
include(CDSanitizers)          # cd_apply_sanitizers + CD_SANITIZER var
include(CDCoverage)            # cd_apply_coverage + CD_ENABLE_COVERAGE
include(CDCompilerDetection)   # CD_HAS_CXX26_* probes
include(CDVendor)              # cd_mark_target_system_includes
include(CDDoxygen)             # cd_setup_doxygen → `cd_docs` custom target
include(CDSmoke)               # cd_setup_smoke   → `smoke` custom target
include(CDGolden)              # cd_setup_golden  → `smoke-golden-*` custom targets
include(CDBench)               # cd_setup_bench   → `smoke-bench*` custom targets
include(CDInstall)             # cd_finalize_install → CHROMODYNAMICConfig export

# --- Threads (required by concurrency) ---
find_package(Threads REQUIRED)

# --- LTO (Release) ---
if(CD_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _cd_ipo_ok OUTPUT _cd_ipo_err)
  if(_cd_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "[cd] IPO/LTO enabled for Release")
  else()
    message(STATUS "[cd] IPO/LTO not supported: ${_cd_ipo_err}")
  endif()
endif()

# --- GTest dependency (3-tier resolution) ---
#   1. vcpkg find_package (CI canonical)
#   2. Vendored Dependencies/googletest (if submodule populated)
#   3. FetchContent fallback (local sanity, offline-friendly when warm cache)
if(CD_ENABLE_TESTING)
  find_package(GTest CONFIG QUIET)
  if(GTest_FOUND)
    message(STATUS "[cd] GTest: vcpkg/find_package")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Dependencies/googletest/CMakeLists.txt")
    message(STATUS "[cd] GTest: vendored Dependencies/googletest")
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    add_subdirectory(Dependencies/googletest EXCLUDE_FROM_ALL)
    if(NOT TARGET GTest::gtest)
      add_library(GTest::gtest ALIAS gtest)
    endif()
    if(NOT TARGET GTest::gtest_main)
      add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
  else()
    message(STATUS "[cd] GTest: FetchContent (v1.15.2)")
    include(FetchContent)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
      GIT_SHALLOW TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(googletest)
    if(NOT TARGET GTest::gtest)
      add_library(GTest::gtest ALIAS gtest)
    endif()
    if(NOT TARGET GTest::gtest_main)
      add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
  endif()

  # 3rd-party gtest is not subject to our zero-warning policy. Strip -Werror
  # and add specific warning suppressions known to fire in gtest 1.15.x.
  foreach(_gtest_target gtest gtest_main)
    if(TARGET ${_gtest_target})
      if(MSVC)
        target_compile_options(${_gtest_target} PRIVATE
          /W0
          /wd4244 /wd4267 /wd4996
        )
      else()
        target_compile_options(${_gtest_target} PRIVATE
          -w
          -Wno-character-conversion
          -Wno-deprecated-declarations
        )
      endif()
      # ASan/UBSan/TSan/MSan instrument 3rd-party deps so static linkage
      # against the engine stays consistent.
      cd_apply_sanitizers(${_gtest_target})
    endif()
  endforeach()

  # Mark gtest includes SYSTEM for downstream consumers (test TUs). vcpkg's
  # config file already does this; the call is a no-op there. For the
  # FetchContent / vendored tiers it's the actual fix that keeps gtest's own
  # pedantic-warning output from tripping our test-binary /WX.
  cd_mark_target_system_includes(GTest::gtest)
  cd_mark_target_system_includes(GTest::gtest_main)
endif()

# fmt is optional Phase 1 (cd::log integration Sprint S2.2)
find_package(fmt CONFIG QUIET)
if(fmt_FOUND)
  message(STATUS "[cd] fmt found (optional)")
endif()

# --- Engine library tree ---
add_subdirectory(engine)

# --- Samples ---
if(CD_ENABLE_SAMPLES)
  add_subdirectory(samples)
endif()

# --- Offline tools (cookers, packagers) ---
option(CD_ENABLE_TOOLS "Build the offline command-line tools (cd_cook_mesh, …)" ON)
if(CD_ENABLE_TOOLS)
  add_subdirectory(tools)
endif()

# --- IDE compile database (sync compile_commands.json → source root for clangd) ---
# Works for Ninja / Ninja Multi-Config (sync) and VS generators (sub-configure).
include(CompileDatabase)

# --- Coverage aggregation targets (no-op when CD_ENABLE_COVERAGE=OFF) ---
cd_install_coverage_targets()

# --- Doxygen API docs (no-op when CD_ENABLE_DOXYGEN=OFF) ---
cd_setup_doxygen()

# --- Smoke test driver target ---
cd_setup_smoke()

# --- Golden-image (Phase 11 Track A) capture + compare targets ---
cd_setup_golden()

# --- Bench (Phase 12.A) capture + regression-gate targets ---
cd_setup_bench()

# --- .clangd regeneration (per-preset driver / std-flag / build-dir) ---
# Renders .clangd from .clangd.in so clangd parses with the same toolchain
# the active preset uses (clang-cl on MSVC, g++ on MinGW, clang++ on Clang).
include(Clangd)

# --- Install pipeline finalization (no-op when CD_ENABLE_INSTALL=OFF) ---
cd_finalize_install()

# --- Summary ---
message(STATUS "")
message(STATUS "[cd] ====== CHROMODYNAMIC ${PROJECT_VERSION} configuration ======")
message(STATUS "[cd] Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "[cd] Shared libs       : ${CD_BUILD_SHARED_LIBS}")
message(STATUS "[cd] Testing           : ${CD_ENABLE_TESTING}")
message(STATUS "[cd] Samples           : ${CD_ENABLE_SAMPLES}")
message(STATUS "[cd] Warnings as error : ${CD_WARNINGS_AS_ERRORS}")
message(STATUS "[cd] PCH               : ${CD_ENABLE_PCH}")
message(STATUS "[cd] LTO               : ${CD_ENABLE_LTO}")
message(STATUS "[cd] Sanitizer         : ${CD_SANITIZER}")
message(STATUS "[cd] Install           : ${CD_ENABLE_INSTALL}")
get_property(_cd_libs GLOBAL PROPERTY CD_ALL_LIBRARIES)
list(LENGTH _cd_libs _cd_lib_count)
message(STATUS "[cd] Libraries declared: ${_cd_lib_count}")
message(STATUS "[cd] ==============================================")
message(STATUS "")
