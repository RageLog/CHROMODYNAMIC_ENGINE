# =============================================================================
# examples/external_app/CMakeLists.txt
#
# Template for a downstream project consuming CHROMODYNAMIC via
# find_package. Copy this directory into a fresh repo of your own;
# everything below is project-shape boilerplate plus the actual
# find_package + target_link_libraries calls.
#
# See README.md for the step-by-step bring-up + the CI workflow shim.
# =============================================================================
cmake_minimum_required(VERSION 3.28)

project(my_game_inventory
    VERSION 0.1.0
    DESCRIPTION "External-app downstream-consumer demo for CHROMODYNAMIC"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Match engine SemVer pin. SameMajorVersion policy in
# CHROMODYNAMICConfigVersion.cmake means 0.25.* is accepted; bump this
# to "1.0" once a real engine v1.0 is published. Reject silently-
# wrong versions early so downstream debugging doesn't chase ABI
# mismatches that find_package would have caught.
find_package(CHROMODYNAMIC 0.25 REQUIRED)
message(STATUS "linking CHROMODYNAMIC ${CHROMODYNAMIC_VERSION}")

# Fail fast if expected targets are missing — the engine install tree
# excludes 7 libraries (see docs/CONSUMING.md §3). If you depend on
# any of those, build them from source via add_subdirectory of the
# engine tree instead.
foreach(_required IN ITEMS
    cd::core
    cd::diag
    cd::log
    cd::events
    cd::ecs
    cd::asset_json
    cd::imgdiff
    cd::asset_image)
    if(NOT TARGET ${_required})
        message(FATAL_ERROR
            "Required imported target ${_required} is missing. "
            "Check the install tree under CMAKE_PREFIX_PATH or see "
            "docs/CONSUMING.md for the EXCLUDE_FROM_INSTALL cut list."
        )
    endif()
endforeach()

add_executable(my_game_inventory
    src/main.cpp
    src/Inventory.cpp
)
target_include_directories(my_game_inventory PRIVATE src)
target_link_libraries(my_game_inventory PRIVATE
    cd::core
    cd::diag
    cd::log
    cd::events
    cd::ecs
    cd::asset_json
    cd::imgdiff
    cd::asset_image
)
