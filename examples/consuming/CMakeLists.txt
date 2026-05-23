# =============================================================================
# examples/consuming/ — starter template for find_package(CHROMODYNAMIC)
#
# This sub-project is NOT part of the engine's own build. It's a copy-
# pasteable demonstration of what a downstream project's CMakeLists.txt
# looks like when linking against an installed copy of the engine.
#
# Usage (from anywhere outside this repo, after `cmake --install` of the
# engine into a prefix):
#
#   mkdir my_app
#   cd my_app
#   cp /path/to/CHROMODYNAMIC_ENGINE/examples/consuming/CMakeLists.txt .
#   cp /path/to/CHROMODYNAMIC_ENGINE/examples/consuming/main.cpp .
#   cmake -S . -B build -G Ninja \
#         -DCMAKE_PREFIX_PATH=/path/to/install-prefix \
#         -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#   ./build/cd_consuming_smoke
#
# See docs/CONSUMING.md for the longer-form walkthrough.
# =============================================================================
cmake_minimum_required(VERSION 3.28)
project(cd_consuming_smoke
    VERSION 0.1.0
    DESCRIPTION "Downstream-consumer smoke for CHROMODYNAMIC"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Accept 0.25.x via SameMajorVersion policy in CHROMODYNAMICConfigVersion.
# Bump to "1.0" once the engine ships a real v1.0 (Phase 11 closure).
find_package(CHROMODYNAMIC 0.25 REQUIRED)

message(STATUS "Linking against CHROMODYNAMIC ${CHROMODYNAMIC_VERSION}")
foreach(_expected IN ITEMS cd::core cd::diag cd::log cd::imgdiff cd::asset_image)
    if(NOT TARGET ${_expected})
        message(FATAL_ERROR "Expected imported target ${_expected} is missing")
    endif()
endforeach()

add_executable(cd_consuming_smoke main.cpp)
target_link_libraries(cd_consuming_smoke PRIVATE
    cd::core
    cd::diag
    cd::log
    cd::imgdiff
    cd::asset_image
)
