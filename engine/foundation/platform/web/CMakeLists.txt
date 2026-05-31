# CHROMODYNAMIC Web Platform (Emscripten) — CMakeLists.txt
# Phase 1 Design: Stub for Emscripten cross-compilation
# Status: Configuration template (pre-integration T4.5)
#
# Usage:
#   cmake --preset ninja-web-debug
#   cmake --build --preset ninja-web-debug
#
# Output:
#   - output.wasm (WebAssembly module)
#   - output.js (Emscripten runtime + loader)
#   - index.html (browser entry point)

if(NOT CD_PLATFORM_WEB)
    # Web platform not requested; skip this target
    return()
endif()

# ===== Web Platform Library =====
add_library(cd_platform_web STATIC)

target_sources(
    cd_platform_web
    PRIVATE
        main_web.cpp
)

target_include_directories(
    cd_platform_web
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    cd_platform_web
    PUBLIC
        cd::core
        cd::platform  # Base platform abstraction
)

# Emscripten compiler flags (only if CMAKE_TOOLCHAIN_FILE targets Emscripten)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_compile_options(
        cd_platform_web
        PRIVATE
            -fPIC
            -sPIC
            $<$<CONFIG:Debug>:-g4>
    )

    target_link_options(
        cd_platform_web
        PRIVATE
            -sPIC
            -sWASM=1
            -sWASM_BIGINT=1
            -sFILESYSTEM=1
            -siDBFS_DEBUG=0
            # Post-T4.5: Add -sEXPORTED_FUNCTIONS=["_main", ...] as needed
    )
endif()

# ===== Web Sample Application (stub) =====
# Post-T4.5: Integrate with cd::sample framework (hello_webgpu target)
if(CD_ENABLE_SAMPLES AND CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    add_executable(chromodynamic_web)

    target_sources(
        chromodynamic_web
        PRIVATE
            main_web.cpp
    )

    target_include_directories(
        chromodynamic_web
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(
        chromodynamic_web
        PRIVATE
            cd::core
            cd::math
            cd::platform
            # Post-T4.5: cd::rhi::webgpu (when T2.7 lands)
            # Post-T4.5: cd::sample (framework)
    )

    # Emscripten linker settings for executable
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_link_options(
            chromodynamic_web
            PRIVATE
                -sPIC
                -sWASM=1
                -sWASM_BIGINT=1
                -sFILESYSTEM=1
                -siDBFS_DEBUG=0
                --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/index.html
                # Post-T4.5: -sEXPORTED_FUNCTIONS=["_main", "_tick_callback"]
                # Post-T4.5: -sEXPORT_NAME=ChromodynamicEngine
                # Post-T4.5: -sINITIAL_MEMORY=67108864  (64 MB for heap)
                # Post-T4.5: -sMAXIMUM_MEMORY=268435456 (256 MB max growth)
        )

        # Copy index.html to build directory for reference
        add_custom_command(
            TARGET chromodynamic_web POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/index.html
                ${CMAKE_BINARY_DIR}/index.html
            COMMENT "Copying index.html to build directory"
        )
    endif()

    set_target_properties(
        chromodynamic_web
        PROPERTIES
            OUTPUT_NAME chromodynamic_web
    )
endif()

# ===== Testing =====
# Post-T4.5: Add integration tests for WebGPU feature detection, IDBFS mount, etc.
if(CD_ENABLE_TESTING)
    # Stub: Web platform tests require browser environment (defer to integration testing)
    # add_executable(test_platform_web ...)
endif()
