# =============================================================================
# tests/find_package_smoke -- downstream find_package(CHROMODYNAMIC) smoke
# Phase 10 / Sprint 4 / Wave 121.
# Marathon Run 24 X8 extension: also imports cd::editor_panel (a
# header-only INTERFACE library) to exercise the INTERFACE path
# through the install tree.
#
# This subproject is NOT part of the main engine build.  It is consumed
# from outside: a downstream user installs CHROMODYNAMIC into a prefix,
# then configures THIS project with -DCMAKE_PREFIX_PATH=<that prefix>
# to validate the install tree is consumable.
#
# Drive it from the engine build dir:
#
#   cmake --install build/ninja-base --prefix build/ninja-base/install-staging --config Debug
#   cmake -S tests/find_package_smoke -B build/find-package-smoke -DCMAKE_PREFIX_PATH=<absolute path to install staging dir>
#   cmake --build build/find-package-smoke
#   ./build/find-package-smoke/cd_find_package_smoke
#
# Exit 0 means: find_package resolved, cd::core / cd::diag / cd::log /
# cd::editor_panel imported, the binary links + runs against the
# installed library tree.
# =============================================================================

cmake_minimum_required(VERSION 3.28)
project(cd_find_package_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CHROMODYNAMIC REQUIRED)

message(STATUS "Found CHROMODYNAMIC ${CHROMODYNAMIC_VERSION}")
foreach(_check IN ITEMS cd::core cd::diag cd::log cd::editor_panel)
  if(NOT TARGET ${_check})
    message(FATAL_ERROR "Expected imported target ${_check} missing")
  endif()
  message(STATUS "  imported: ${_check}")
endforeach()

add_executable(cd_find_package_smoke main.cpp)
target_link_libraries(cd_find_package_smoke PRIVATE
  cd::core cd::diag cd::log cd::editor_panel
)
